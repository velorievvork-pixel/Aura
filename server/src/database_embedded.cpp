// aura/database_embedded.cpp — файловое хранилище для режима без PostgreSQL.
//
// Повторяет семантику схемы (те же таблицы, те же ограничения) и используется:
//   * в юнит-тестах сервера;
//   * в демо-режиме (AURA_DATABASE_URL не задан).
// Данные держатся в памяти и сбрасываются в JSON-файл после каждой записи.
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "aura/idatabase.h"
#include "aura/json.h"
#include "aura/log.h"

namespace aura {

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return lower(haystack).find(lower(needle)) != std::string::npos;
}

// UUID в каноническом виде (с дефисами) и 32 hex — один и тот же идентификатор.
std::string plainUuid(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const char ch : value) {
        if (ch != '-') out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

// Отметки времени сравниваются по секундам: «YYYY-MM-DDTHH:MM:SS» (первые 19
// символов) — у сессий и у created_at разная дробная часть.
std::string isoSecondsAgo(long long seconds) {
    const auto moment = std::chrono::system_clock::now() - std::chrono::seconds(seconds);
    const std::time_t time = std::chrono::system_clock::to_time_t(moment);
    std::tm tm{};
    gmtime_r(&time, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &tm);
    return buffer;
}

bool sessionLiveJson(const Json& item, const std::string& nowSeconds) {
    if (!item.getString("revoked_at").empty()) return false;
    const std::string expiresAt = item.getString("expires_at");
    return expiresAt.empty() || expiresAt.substr(0, 19) > nowSeconds;
}

class EmbeddedDatabase : public IDatabase {
public:
    explicit EmbeddedDatabase(std::string path) : path_(std::move(path)) {}

    std::string name() const override { return "embedded:" + path_; }

    bool connect(std::string& error) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ifstream file(path_);
        if (!file) {
            if (::access(path_.c_str(), F_OK) == 0) {
                error = "файл встроенной БД " + path_ + " существует, но не читается (права доступа?)";
                return false;
            }
            state_ = Json::object();
            return true;
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        std::string parseError;
        const Json parsed = Json::parse(buffer.str(), &parseError);
        if (!parsed.isObject()) {
            // Раньше повреждённый файл молча заменялся пустой базой — при
            // следующей записи все аккаунты и данные терялись безвозвратно.
            error = "файл встроенной БД " + path_ + " повреждён (" +
                    (parseError.empty() ? std::string("ожидался JSON-объект") : parseError) +
                    "): восстановите его из резервной копии или удалите вручную";
            return false;
        }
        state_ = parsed;
        // Файл содержит хэши паролей и токенов — только владельцу процесса.
        ::chmod(path_.c_str(), 0600);
        return true;
    }

    bool healthy() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return true;
    }

    // ------------------------------------------------------------ users
    DatabaseError createUser(const std::string& email,
                             const std::string& passwordHash,
                             const std::string& displayName,
                             long long& outId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        Json& users = table("users");
        for (const auto& item : users.items()) {
            if (lower(item.getString("email")) == lower(email)) {
                return DatabaseError::failure("пользователь с таким email уже существует");
            }
        }
        const long long id = nextId("users");
        Json user = Json::object();
        user.set("id", Json(id));
        user.set("email", Json(email));
        user.set("password_hash", Json(passwordHash));
        user.set("display_name", Json(displayName));
        user.set("avatar_url", Json(""));
        user.set("bio", Json(""));
        user.set("timezone", Json("Europe/Moscow"));
        user.set("is_active", Json(true));
        user.set("email_verified", Json(false));  // подтверждается через auth.verifyEmail
        user.set("created_at", Json(isoNow()));
        users.push(user);

        // Триггер users_create_defaults: строка настроек создаётся сразу.
        Json preferences = Json::object();
        preferences.set("user_id", Json(id));
        preferences.set("diet", Json::array());
        preferences.set("transport", Json("walk"));
        Json hours = Json::array();
        for (const int hour : {10, 11, 12, 16, 17, 18, 19}) hours.push(Json(hour));
        preferences.set("preferred_hours", hours);
        Json work = Json::array();
        for (int hour = 9; hour <= 18; ++hour) work.push(Json(hour));
        preferences.set("work_hours", work);
        preferences.set("budget_limit", Json(0));
        preferences.set("city", Json(""));
        preferences.set("theme", Json("graphite"));
        preferences.set("birthday", Json(""));
        preferences.set("allergies", Json::array());
        preferences.set("onboarded", Json(false));
        Json notifications = Json::object();
        notifications.set("push", Json(true));
        notifications.set("email", Json(false));
        preferences.set("notifications", notifications);
        Json privacy = Json::object();
        privacy.set("share_free_slots_only", Json(true));
        privacy.set("hide_event_titles", Json(true));
        privacy.set("allow_place_choice", Json(true));
        preferences.set("privacy", privacy);
        preferences.set("updated_at", Json(isoNow()));
        table("user_preferences").push(preferences);

        flush();
        outId = id;
        return DatabaseError::success();
    }

    std::optional<UserRecord> findUserByEmail(const std::string& email) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& item : table("users").items()) {
            if (lower(item.getString("email")) == lower(email)) return toUser(item);
        }
        return std::nullopt;
    }

    std::optional<UserRecord> findUserById(long long id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return findUserByIdLocked(id);
    }

    std::vector<UserRecord> searchUsers(const std::string& query, int limit) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<UserRecord> result;
        for (const auto& item : table("users").items()) {
            if (query.empty() || contains(item.getString("display_name"), query)) {
                result.push_back(toUser(item));
                if (static_cast<int>(result.size()) >= limit) break;
            }
        }
        return result;
    }

    DatabaseError touchUser(long long userId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : mutableTable("users")) {
            if (item.getInt("id") == userId) {
                item.set("last_seen_at", Json(isoNow()));
                flush();
                return DatabaseError::success();
            }
        }
        return DatabaseError::failure("пользователь не найден");
    }

    DatabaseError updateProfile(long long userId, const Json& fields) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : mutableTable("users")) {
            if (item.getInt("id") != userId) continue;
            for (const char* key : {"display_name", "bio", "avatar_url"}) {
                if (fields.contains(key)) item.set(key, Json(fields.getString(key)));
            }
            flush();
            return DatabaseError::success();
        }
        return DatabaseError::failure("пользователь не найден");
    }

    // --------------------------------------------------------- sessions
    DatabaseError createSession(const SessionRecord& record) override {
        std::lock_guard<std::mutex> lock(mutex_);
        Json session = Json::object();
        session.set("id", Json(record.id));
        session.set("user_id", Json(record.userId));
        session.set("jwt_id", Json(record.jwtId));
        session.set("device", Json(record.device));
        session.set("remote_addr", Json(record.remoteAddr));
        // Как DEFAULT now() в PostgreSQL: AuthManager время создания не задаёт,
        // и без этого sessions.list отдавал пустой created_at.
        session.set("created_at", Json(record.createdAt.empty() ? isoNow() : record.createdAt));
        session.set("expires_at", Json(record.expiresAt));
        session.set("revoked_at", Json(""));
        table("sessions").push(session);
        flush();
        return DatabaseError::success();
    }

    DatabaseError revokeSession(const std::string& jwtId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : mutableTable("sessions")) {
            if (item.getString("jwt_id") == jwtId) {
                item.set("revoked_at", Json(isoNow()));
                flush();
            }
        }
        return DatabaseError::success();
    }

    std::optional<SessionRecord> findSessionByJwt(const std::string& jwtId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& item : table("sessions").items()) {
            if (item.getString("jwt_id") != jwtId) continue;
            // Как и в PostgreSQL: отозванные и истёкшие сессии не находятся.
            if (!item.getString("revoked_at").empty()) return std::nullopt;
            const std::string expiresAt = item.getString("expires_at");
            if (!expiresAt.empty() && expiresAt <= isoNow()) return std::nullopt;
            return toSession(item);
        }
        return std::nullopt;
    }

    DatabaseError updatePasswordHash(long long userId, const std::string& hash) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : mutableTable("users")) {
            if (item.getInt("id") == userId) {
                item.set("password_hash", Json(hash));
                flush();
                return DatabaseError::success();
            }
        }
        return DatabaseError::failure("пользователь не найден");
    }

    DatabaseError setEmailVerified(long long userId, bool verified) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : mutableTable("users")) {
            if (item.getInt("id") == userId) {
                item.set("email_verified", Json(verified));
                flush();
                return DatabaseError::success();
            }
        }
        return DatabaseError::failure("пользователь не найден");
    }

    DatabaseError setUserActive(long long userId, bool active) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : mutableTable("users")) {
            if (item.getInt("id") == userId) {
                item.set("is_active", Json(active));
                flush();
                return DatabaseError::success();
            }
        }
        return DatabaseError::failure("пользователь не найден");
    }

    std::vector<SessionRecord> listSessions(long long userId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<SessionRecord> result;
        for (const auto& item : table("sessions").items()) {
            if (item.getInt("user_id") != userId) continue;
            if (!item.getString("revoked_at").empty()) continue;
            result.push_back(toSession(item));
        }
        return result;
    }

    DatabaseError revokeSessionById(long long userId, const std::string& sessionId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : mutableTable("sessions")) {
            if (item.getString("id") == sessionId && item.getInt("user_id") == userId) {
                item.set("revoked_at", Json(isoNow()));
                flush();
                return DatabaseError::success();
            }
        }
        return DatabaseError::failure("сессия не найдена");
    }

    DatabaseError revokeAllSessions(long long userId, const std::string& exceptJwtId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : mutableTable("sessions")) {
            if (item.getInt("user_id") != userId) continue;
            if (!exceptJwtId.empty() && item.getString("jwt_id") == exceptJwtId) continue;
            if (!item.getString("revoked_at").empty()) continue;
            item.set("revoked_at", Json(isoNow()));
        }
        flush();
        return DatabaseError::success();
    }

    // ------------------------------------------------------- auth tokens
    DatabaseError createAuthToken(const AuthTokenRecord& record) override {
        std::lock_guard<std::mutex> lock(mutex_);
        Json token = Json::object();
        token.set("id", Json(record.id));
        token.set("user_id", Json(record.userId));
        token.set("purpose", Json(record.purpose));
        token.set("token_hash", Json(record.tokenHash));
        token.set("created_at", Json(record.createdAt));
        token.set("expires_at", Json(record.expiresAt));
        token.set("used_at", Json(""));
        table("auth_tokens").push(token);
        flush();
        return DatabaseError::success();
    }

    std::optional<AuthTokenRecord> findAuthToken(const std::string& purpose,
                                                 const std::string& tokenHash) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& item : table("auth_tokens").items()) {
            if (item.getString("purpose") != purpose) continue;
            if (item.getString("token_hash") != tokenHash) continue;
            return toAuthToken(item);
        }
        return std::nullopt;
    }

    DatabaseError useAuthToken(const std::string& id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : mutableTable("auth_tokens")) {
            if (item.getString("id") == id) {
                item.set("used_at", Json(isoNow()));
                flush();
                return DatabaseError::success();
            }
        }
        return DatabaseError::failure("код не найден");
    }

    DatabaseError deleteAuthTokens(long long userId, const std::string& purpose) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& tokens = mutableTable("auth_tokens");
        tokens.erase(std::remove_if(tokens.begin(), tokens.end(),
                                    [&](const Json& item) {
                                        return item.getInt("user_id") == userId &&
                                               item.getString("purpose") == purpose;
                                    }),
                     tokens.end());
        flush();
        return DatabaseError::success();
    }

    // ---------------------------------------------------- refresh tokens
    DatabaseError createRefreshToken(const RefreshTokenRecord& record) override {
        std::lock_guard<std::mutex> lock(mutex_);
        Json token = Json::object();
        token.set("id", Json(record.id));
        token.set("user_id", Json(record.userId));
        token.set("family_id", Json(record.familyId));
        token.set("token_hash", Json(record.tokenHash));
        token.set("device", Json(record.device));
        token.set("remote_addr", Json(record.remoteAddr));
        token.set("created_at", Json(record.createdAt));
        token.set("expires_at", Json(record.expiresAt));
        token.set("revoked_at", Json(""));
        token.set("replaced_by", Json(""));
        table("refresh_tokens").push(token);
        flush();
        return DatabaseError::success();
    }

    std::optional<RefreshTokenRecord> findRefreshToken(const std::string& tokenHash) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& item : table("refresh_tokens").items()) {
            if (item.getString("token_hash") == tokenHash) return toRefreshToken(item);
        }
        return std::nullopt;
    }

    DatabaseError revokeRefreshToken(const std::string& id, const std::string& replacedBy) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : mutableTable("refresh_tokens")) {
            if (item.getString("id") == id) {
                if (!item.getString("revoked_at").empty()) {
                    return DatabaseError::failure("refresh-токен уже отозван");
                }
                item.set("revoked_at", Json(isoNow()));
                item.set("replaced_by", Json(replacedBy));
                flush();
                return DatabaseError::success();
            }
        }
        return DatabaseError::failure("refresh-токен не найден");
    }

    DatabaseError revokeRefreshFamily(const std::string& familyId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (familyId.empty()) return DatabaseError::success();
        for (auto& item : mutableTable("refresh_tokens")) {
            if (plainUuid(item.getString("family_id")) != plainUuid(familyId)) continue;
            if (!item.getString("revoked_at").empty()) continue;
            item.set("revoked_at", Json(isoNow()));
        }
        flush();
        return DatabaseError::success();
    }

    DatabaseError revokeAllUserRefreshTokens(long long userId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : mutableTable("refresh_tokens")) {
            if (item.getInt("user_id") != userId) continue;
            if (!item.getString("revoked_at").empty()) continue;
            item.set("revoked_at", Json(isoNow()));
        }
        flush();
        return DatabaseError::success();
    }

    DatabaseError revokeUserRefreshTokensExcept(long long userId, const std::string& keepFamilyId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string keep = plainUuid(keepFamilyId);
        for (auto& item : mutableTable("refresh_tokens")) {
            if (item.getInt("user_id") != userId) continue;
            if (!item.getString("revoked_at").empty()) continue;
            if (!keep.empty() && plainUuid(item.getString("family_id")) == keep) continue;
            item.set("revoked_at", Json(isoNow()));
        }
        flush();
        return DatabaseError::success();
    }

    // --------------------------------------------------------- audit log
    DatabaseError insertAudit(long long userId,
                              const std::string& kind,
                              const Json& detail,
                              const std::string& remoteAddr) override {
        std::lock_guard<std::mutex> lock(mutex_);
        Json entry = Json::object();
        entry.set("id", Json(nextId("audit_logs")));
        entry.set("user_id", Json(userId));
        entry.set("kind", Json(kind));
        entry.set("detail", detail.isObject() ? detail : Json::object());
        entry.set("remote_addr", Json(remoteAddr));
        entry.set("created_at", Json(isoNow()));
        table("audit_logs").push(entry);
        flush();
        return DatabaseError::success();
    }

    std::vector<AuditRecord> listAudit(long long userId, int limit) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<AuditRecord> result;
        const auto& logs = table("audit_logs").items();
        // Последние события первыми.
        for (auto it = logs.rbegin(); it != logs.rend(); ++it) {
            if (it->getInt("user_id") != userId) continue;
            AuditRecord record;
            record.id = it->getInt("id");
            record.userId = userId;
            record.kind = it->getString("kind");
            record.detail = it->get("detail");
            record.remoteAddr = it->getString("remote_addr");
            record.createdAt = it->getString("created_at");
            result.push_back(record);
            if (static_cast<int>(result.size()) >= limit) break;
        }
        return result;
    }

    // ------------------------------------------------------------ 2FA
    DatabaseError upsertTwoFactor(const TwoFactorRecord& record) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& rows = mutableTable("two_factor_settings");
        for (auto& item : rows) {
            if (item.getInt("user_id") == record.userId) {
                item.set("secret_encrypted", Json(record.secretEncrypted));
                item.set("enabled", Json(record.enabled));
                item.set("last_used_counter", Json(record.lastUsedCounter));
                item.set("enabled_at", Json(record.enabledAt));
                item.set("updated_at", Json(isoNow()));
                flush();
                return DatabaseError::success();
            }
        }
        Json row = Json::object();
        row.set("user_id", Json(record.userId));
        row.set("secret_encrypted", Json(record.secretEncrypted));
        row.set("enabled", Json(record.enabled));
        row.set("last_used_counter", Json(record.lastUsedCounter));
        row.set("created_at", Json(record.createdAt));
        row.set("enabled_at", Json(record.enabledAt));
        row.set("updated_at", Json(isoNow()));
        rows.push_back(row);
        flush();
        return DatabaseError::success();
    }

    std::optional<TwoFactorRecord> findTwoFactor(long long userId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& item : table("two_factor_settings").items()) {
            if (item.getInt("user_id") == userId) return toTwoFactor(item);
        }
        return std::nullopt;
    }

    DatabaseError setTwoFactorEnabled(long long userId, bool enabled) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : mutableTable("two_factor_settings")) {
            if (item.getInt("user_id") == userId) {
                item.set("enabled", Json(enabled));
                if (enabled && item.getString("enabled_at").empty()) {
                    item.set("enabled_at", Json(isoNow()));
                }
                item.set("updated_at", Json(isoNow()));
                flush();
                return DatabaseError::success();
            }
        }
        return DatabaseError::failure("настройка 2FA не найдена");
    }

    DatabaseError updateTwoFactorCounter(long long userId, long long counter) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : mutableTable("two_factor_settings")) {
            if (item.getInt("user_id") == userId) {
                item.set("last_used_counter", Json(counter));
                flush();
                return DatabaseError::success();
            }
        }
        return DatabaseError::failure("настройка 2FA не найдена");
    }

    DatabaseError deleteTwoFactor(long long userId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& rows = mutableTable("two_factor_settings");
        rows.erase(std::remove_if(rows.begin(), rows.end(),
                                   [&](const Json& item) { return item.getInt("user_id") == userId; }),
                   rows.end());
        flush();
        return DatabaseError::success();
    }

    DatabaseError replaceRecoveryCodes(long long userId,
                                       const std::vector<RecoveryCodeRecord>& codes) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& rows = mutableTable("recovery_codes");
        rows.erase(std::remove_if(rows.begin(), rows.end(),
                                   [&](const Json& item) { return item.getInt("user_id") == userId; }),
                   rows.end());
        for (const auto& code : codes) {
            Json row = Json::object();
            row.set("id", Json(code.id));
            row.set("user_id", Json(userId));
            row.set("code_hash", Json(code.codeHash));
            row.set("created_at", Json(code.createdAt));
            row.set("used_at", Json(""));
            rows.push_back(row);
        }
        flush();
        return DatabaseError::success();
    }

    std::optional<RecoveryCodeRecord> findRecoveryCode(long long userId,
                                                       const std::string& codeHash) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& item : table("recovery_codes").items()) {
            if (item.getInt("user_id") != userId) continue;
            if (item.getString("code_hash") != codeHash) continue;
            // Резервный код одноразовый: использованный не возвращаем.
            if (!item.getString("used_at").empty()) continue;
            return toRecoveryCode(item);
        }
        return std::nullopt;
    }

    DatabaseError useRecoveryCode(const std::string& id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : mutableTable("recovery_codes")) {
            if (item.getString("id") == id) {
                item.set("used_at", Json(isoNow()));
                flush();
                return DatabaseError::success();
            }
        }
        return DatabaseError::failure("резервный код не найден");
    }

    int countUnusedRecoveryCodes(long long userId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        int count = 0;
        for (const auto& item : table("recovery_codes").items()) {
            if (item.getInt("user_id") != userId) continue;
            if (item.getString("used_at").empty()) ++count;
        }
        return count;
    }

    DatabaseError addTrustedDevice(const TrustedDeviceRecord& record) override {
        std::lock_guard<std::mutex> lock(mutex_);
        Json row = Json::object();
        row.set("id", Json(record.id));
        row.set("user_id", Json(record.userId));
        row.set("device_hash", Json(record.deviceHash));
        row.set("device", Json(record.device));
        row.set("remote_addr", Json(record.remoteAddr));
        row.set("created_at", Json(record.createdAt));
        row.set("expires_at", Json(record.expiresAt));
        row.set("last_used_at", Json(record.lastUsedAt));
        row.set("revoked_at", Json(""));
        table("trusted_devices").push(row);
        flush();
        return DatabaseError::success();
    }

    std::optional<TrustedDeviceRecord> findTrustedDevice(long long userId,
                                                         const std::string& deviceHash) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string now = isoNow();
        for (const auto& item : table("trusted_devices").items()) {
            if (item.getInt("user_id") != userId) continue;
            if (item.getString("device_hash") != deviceHash) continue;
            // Доверие действует, пока устройство не отозвано и срок не истёк.
            if (!item.getString("revoked_at").empty()) continue;
            if (item.getString("expires_at") <= now) continue;
            return toTrustedDevice(item);
        }
        return std::nullopt;
    }

    std::vector<TrustedDeviceRecord> listTrustedDevices(long long userId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string now = isoNow();
        std::vector<TrustedDeviceRecord> result;
        for (const auto& item : table("trusted_devices").items()) {
            if (item.getInt("user_id") != userId) continue;
            if (!item.getString("revoked_at").empty()) continue;
            if (item.getString("expires_at") <= now) continue;
            result.push_back(toTrustedDevice(item));
        }
        return result;
    }

    DatabaseError touchTrustedDevice(long long userId, const std::string& deviceHash) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : mutableTable("trusted_devices")) {
            if (item.getInt("user_id") != userId) continue;
            if (item.getString("device_hash") != deviceHash) continue;
            item.set("last_used_at", Json(isoNow()));
            flush();
            return DatabaseError::success();
        }
        return DatabaseError::failure("доверенное устройство не найдено");
    }

    DatabaseError revokeTrustedDevice(long long userId, const std::string& id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : mutableTable("trusted_devices")) {
            if (item.getInt("user_id") != userId) continue;
            if (item.getString("id") != id) continue;
            item.set("revoked_at", Json(isoNow()));
            flush();
            return DatabaseError::success();
        }
        return DatabaseError::failure("доверенное устройство не найдено");
    }

    DatabaseError revokeAllTrustedDevices(long long userId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : mutableTable("trusted_devices")) {
            if (item.getInt("user_id") != userId) continue;
            if (!item.getString("revoked_at").empty()) continue;
            item.set("revoked_at", Json(isoNow()));
        }
        flush();
        return DatabaseError::success();
    }

    // ------------------------------------------------------------ chats
    DatabaseError createChat(const std::string& kind,
                             const std::string& title,
                             long long createdBy,
                             const std::vector<long long>& members,
                             long long& outId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const long long id = nextId("chats");
        Json chat = Json::object();
        chat.set("id", Json(id));
        chat.set("kind", Json(kind));
        chat.set("title", Json(title));
        chat.set("created_by", Json(createdBy));
        chat.set("created_at", Json(isoNow()));
        table("chats").push(chat);

        for (const long long userId : members) {
            Json member = Json::object();
            member.set("chat_id", Json(id));
            member.set("user_id", Json(userId));
            member.set("role", Json(userId == createdBy ? "owner" : "member"));
            member.set("last_read_at", Json(""));
            member.set("is_muted", Json(false));
            table("chat_members").push(member);
        }
        flush();
        outId = id;
        return DatabaseError::success();
    }

    std::vector<ChatRecord> listChats(long long userId, int limit) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<long long> chatIds;
        for (const auto& member : table("chat_members").items()) {
            if (member.getInt("user_id") == userId) chatIds.push_back(member.getInt("chat_id"));
        }
        std::vector<ChatRecord> result;
        for (const auto& chat : table("chats").items()) {
            const long long chatId = chat.getInt("id");
            if (std::find(chatIds.begin(), chatIds.end(), chatId) == chatIds.end()) continue;
            ChatRecord record = toChat(chat);
            attachLastMessage(record);
            result.push_back(record);
        }
        std::sort(result.begin(), result.end(), [](const ChatRecord& a, const ChatRecord& b) {
            return a.lastMessageAt > b.lastMessageAt;
        });
        if (static_cast<int>(result.size()) > limit) result.resize(static_cast<std::size_t>(limit));
        return result;
    }

    std::optional<ChatRecord> findChat(long long chatId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& chat : table("chats").items()) {
            if (chat.getInt("id") == chatId) {
                ChatRecord record = toChat(chat);
                attachLastMessage(record);
                return record;
            }
        }
        return std::nullopt;
    }

    std::optional<long long> findDirectChat(long long first, long long second) override {
        std::lock_guard<std::mutex> lock(mutex_);
        // Личный чат: kind 'direct' или 'agent' и ровно два участника — эти двое.
        for (const auto& chat : table("chats").items()) {
            const std::string kind = chat.getString("kind");
            if (kind != "direct" && kind != "agent") continue;
            const long long chatId = chat.getInt("id");
            bool hasFirst = false;
            bool hasSecond = false;
            std::size_t total = 0;
            for (const auto& member : table("chat_members").items()) {
                if (member.getInt("chat_id") != chatId) continue;
                ++total;
                if (member.getInt("user_id") == first) hasFirst = true;
                if (member.getInt("user_id") == second) hasSecond = true;
            }
            if (hasFirst && hasSecond && total == 2) return chatId;
        }
        return std::nullopt;
    }

    std::vector<MemberRecord> listMembers(long long chatId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<MemberRecord> result;
        for (const auto& member : table("chat_members").items()) {
            if (member.getInt("chat_id") != chatId) continue;
            MemberRecord record;
            record.chatId = chatId;
            record.userId = member.getInt("user_id");
            record.role = member.getString("role", "member");
            record.lastReadAt = member.getString("last_read_at");
            if (const auto user = findUserByIdLocked(record.userId)) {
                record.displayName = user->displayName;
                record.email = user->email;
            }
            result.push_back(record);
        }
        return result;
    }

    bool isMember(long long chatId, long long userId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& member : table("chat_members").items()) {
            if (member.getInt("chat_id") == chatId && member.getInt("user_id") == userId) return true;
        }
        return false;
    }

    DatabaseError markRead(long long chatId, long long userId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& member : mutableTable("chat_members")) {
            if (member.getInt("chat_id") == chatId && member.getInt("user_id") == userId) {
                member.set("last_read_at", Json(isoNow()));
                flush();
                return DatabaseError::success();
            }
        }
        return DatabaseError::failure("участник не найден");
    }

    // --------------------------------------------------------- messages
    DatabaseError insertMessage(const MessageRecord& record, long long& outId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const long long id = nextId("messages");
        Json message = Json::object();
        message.set("id", Json(id));
        message.set("chat_id", Json(record.chatId));
        message.set("sender_id", Json(record.senderId));
        message.set("kind", Json(record.kind));
        message.set("body", Json(record.body));
        message.set("payload", record.payload.isObject() ? record.payload : Json::object());
        message.set("created_at", Json(record.createdAt.empty() ? isoNow() : record.createdAt));
        table("messages").push(message);

        // Триггер messages_touch_chat
        for (auto& chat : mutableTable("chats")) {
            if (chat.getInt("id") == record.chatId) {
                chat.set("last_message_at", message.get("created_at"));
                break;
            }
        }
        flush();
        outId = id;
        return DatabaseError::success();
    }

    std::vector<MessageRecord> listMessages(long long chatId, long long beforeId, int limit) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<MessageRecord> result;
        for (const auto& message : table("messages").items()) {
            if (message.getInt("chat_id") != chatId) continue;
            if (beforeId > 0 && message.getInt("id") >= beforeId) continue;
            result.push_back(toMessage(message));
        }
        std::sort(result.begin(), result.end(),
                  [](const MessageRecord& a, const MessageRecord& b) { return a.id > b.id; });
        if (static_cast<int>(result.size()) > limit) result.resize(static_cast<std::size_t>(limit));
        std::reverse(result.begin(), result.end());  // клиенту удобнее читать сверху вниз
        for (auto& record : result) {
            if (const auto user = findUserByIdLocked(record.senderId)) {
                record.senderName = user->displayName;
            }
        }
        return result;
    }

    // ------------------------------------------------------- user_memory
    std::vector<MemoryRecord> listMemory(long long userId, int limit) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<MemoryRecord> result;
        for (const auto& entry : table("user_memory").items()) {
            if (entry.getInt("user_id") != userId) continue;
            MemoryRecord record;
            record.id = entry.getInt("id");
            record.userId = userId;
            record.kind = entry.getString("kind", "fact");
            record.text = entry.getString("text");
            record.weight = entry.getDouble("weight", 1.0);
            const Json tagArray = entry.get("tags");
            for (const auto& tag : tagArray.items()) record.tags.push_back(tag.asString());
            record.updatedAt = entry.getString("updated_at");
            result.push_back(record);
        }
        std::sort(result.begin(), result.end(),
                  [](const MemoryRecord& a, const MemoryRecord& b) { return a.weight > b.weight; });
        if (static_cast<int>(result.size()) > limit) result.resize(static_cast<std::size_t>(limit));
        return result;
    }

    DatabaseError upsertMemory(long long userId,
                               const std::string& kind,
                               const std::string& text,
                               double weight,
                               const std::vector<std::string>& tags) override {
        std::lock_guard<std::mutex> lock(mutex_);
        Json& entries = table("user_memory");
        for (auto& entry : mutableTable("user_memory")) {
            if (entry.getInt("user_id") == userId && entry.getString("text") == text) {
                entry.set("kind", Json(kind));
                entry.set("weight", Json(std::min(5.0, entry.getDouble("weight", 1.0) + 0.25)));
                Json tagArray = Json::array();
                for (const auto& tag : tags) tagArray.push(Json(tag));
                entry.set("tags", tagArray);
                entry.set("updated_at", Json(isoNow()));
                flush();
                return DatabaseError::success();
            }
        }
        Json entry = Json::object();
        entry.set("id", Json(nextId("user_memory")));
        entry.set("user_id", Json(userId));
        entry.set("kind", Json(kind));
        entry.set("text", Json(text));
        entry.set("weight", Json(weight));
        Json tagArray = Json::array();
        for (const auto& tag : tags) tagArray.push(Json(tag));
        entry.set("tags", tagArray);
        entry.set("updated_at", Json(isoNow()));
        entries.push(entry);
        flush();
        return DatabaseError::success();
    }

    // -------------------------------------------------- user_preferences
    Json getPreferences(long long userId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : table("user_preferences").items()) {
            if (entry.getInt("user_id") == userId) {
                Json copy = entry;
                copy.erase("user_id");
                return copy;
            }
        }
        return Json::object();
    }

    DatabaseError setPreferences(long long userId, const Json& preferences) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mutableTable("user_preferences")) {
            if (entry.getInt("user_id") != userId) continue;
            for (const auto& member : preferences.members()) {
                if (member.first == "user_id") continue;
                entry.set(member.first, member.second);
            }
            entry.set("updated_at", Json(isoNow()));
            flush();
            return DatabaseError::success();
        }
        return DatabaseError::failure("настройки не найдены");
    }

    // ------------------------------------------------- tool_permissions
    std::string getToolPermission(long long userId, const std::string& tool) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : table("tool_permissions").items()) {
            if (entry.getInt("user_id") == userId && entry.getString("tool") == tool) {
                return entry.getString("mode");
            }
        }
        return "";  // не задано
    }

    std::vector<ToolPermissionRecord> listToolPermissions(long long userId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ToolPermissionRecord> result;
        for (const auto& entry : table("tool_permissions").items()) {
            if (entry.getInt("user_id") != userId) continue;
            result.push_back(toToolPermission(entry));
        }
        std::sort(result.begin(), result.end(),
                  [](const ToolPermissionRecord& a, const ToolPermissionRecord& b) { return a.tool < b.tool; });
        return result;
    }

    DatabaseError setToolPermission(long long userId,
                                    const std::string& tool,
                                    const std::string& mode) override {
        if (mode != "allow" && mode != "ask" && mode != "deny") {
            return DatabaseError::failure("недопустимый режим разрешения");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mutableTable("tool_permissions")) {
            if (entry.getInt("user_id") == userId && entry.getString("tool") == tool) {
                entry.set("mode", Json(mode));
                entry.set("updated_at", Json(isoNow()));
                flush();
                return DatabaseError::success();
            }
        }
        Json entry = Json::object();
        entry.set("user_id", Json(userId));
        entry.set("tool", Json(tool));
        entry.set("mode", Json(mode));
        entry.set("updated_at", Json(isoNow()));
        table("tool_permissions").push(entry);
        flush();
        return DatabaseError::success();
    }

    // ---------------------------------------------------- pending_actions
    long long createPendingAction(const PendingActionRecord& record) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const long long id = nextId("pending_actions");
        Json entry = Json::object();
        entry.set("id", Json(id));
        entry.set("user_id", Json(record.userId));
        entry.set("chat_id", Json(record.chatId));
        entry.set("tool", Json(record.tool));
        entry.set("args", record.args.isObject() ? record.args : Json::object());
        entry.set("summary", Json(record.summary));
        entry.set("status", Json(record.status.empty() ? std::string("pending") : record.status));
        entry.set("result", Json::object());
        entry.set("created_at", Json(isoNow()));
        table("pending_actions").push(entry);
        flush();
        return id;
    }

    std::optional<PendingActionRecord> findPendingAction(long long id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : table("pending_actions").items()) {
            if (entry.getInt("id") == id) return toPendingAction(entry);
        }
        return std::nullopt;
    }

    bool claimPendingAction(long long id, long long userId, const std::string& status) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mutableTable("pending_actions")) {
            if (entry.getInt("id") != id) continue;
            if (entry.getInt("user_id") != userId || entry.getString("status") != "pending") return false;
            entry.set("status", Json(status));
            entry.set("resolved_at", Json(isoNow()));
            flush();
            return true;
        }
        return false;
    }

    std::vector<PendingActionRecord> listPendingActions(long long userId,
                                                        const std::string& status,
                                                        int limit) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<PendingActionRecord> result;
        for (const auto& entry : table("pending_actions").items()) {
            if (entry.getInt("user_id") != userId) continue;
            if (!status.empty() && entry.getString("status") != status) continue;
            result.push_back(toPendingAction(entry));
        }
        std::sort(result.begin(), result.end(),
                  [](const PendingActionRecord& a, const PendingActionRecord& b) { return a.id > b.id; });
        if (limit > 0 && static_cast<int>(result.size()) > limit) {
            result.resize(static_cast<std::size_t>(limit));
        }
        return result;
    }

    DatabaseError resolvePendingAction(long long id,
                                       const std::string& status,
                                       const Json& result) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mutableTable("pending_actions")) {
            if (entry.getInt("id") != id) continue;
            entry.set("status", Json(status));
            entry.set("result", result.isObject() ? result : Json::object());
            entry.set("resolved_at", Json(isoNow()));
            flush();
            return DatabaseError::success();
        }
        return DatabaseError::failure("отложенное действие не найдено");
    }

    // -------------------------------------------------------------- tasks
    DatabaseError createTask(const TaskRecord& record, long long& outId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (record.title.empty()) return DatabaseError::failure("пустой заголовок задачи");
        const long long id = nextId("tasks");
        Json entry = Json::object();
        entry.set("id", Json(id));
        entry.set("user_id", Json(record.userId));
        entry.set("chat_id", Json(record.chatId));
        entry.set("title", Json(record.title));
        entry.set("notes", Json(record.notes));
        entry.set("status", Json(record.status.empty() ? std::string("pending") : record.status));
        entry.set("priority", Json(record.priority));
        entry.set("due_at", Json(record.dueAt));
        entry.set("remind_at", Json(record.remindAt));
        entry.set("reminded_at", Json(record.remindedAt));
        entry.set("created_at", Json(isoNow()));
        entry.set("completed_at", Json(record.completedAt));
        table("tasks").push(entry);
        flush();
        outId = id;
        return DatabaseError::success();
    }

    std::optional<TaskRecord> findTask(long long id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : table("tasks").items()) {
            if (entry.getInt("id") == id) return toTask(entry);
        }
        return std::nullopt;
    }

    std::vector<TaskRecord> listTasks(long long userId, const std::string& status, int limit) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<TaskRecord> result;
        for (const auto& entry : table("tasks").items()) {
            if (entry.getInt("user_id") != userId) continue;
            if (!status.empty() && entry.getString("status") != status) continue;
            result.push_back(toTask(entry));
        }
        std::sort(result.begin(), result.end(),
                  [](const TaskRecord& a, const TaskRecord& b) { return a.id > b.id; });
        if (limit > 0 && static_cast<int>(result.size()) > limit) {
            result.resize(static_cast<std::size_t>(limit));
        }
        return result;
    }

    DatabaseError setTaskStatus(long long id, const std::string& status) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mutableTable("tasks")) {
            if (entry.getInt("id") != id) continue;
            entry.set("status", Json(status));
            if (status == "done") entry.set("completed_at", Json(isoNow()));
            flush();
            return DatabaseError::success();
        }
        return DatabaseError::failure("задача не найдена");
    }

    DatabaseError deleteTask(long long id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        Json::Array& entries = mutableTable("tasks");
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            if (it->getInt("id") == id) {
                entries.erase(it);
                flush();
                return DatabaseError::success();
            }
        }
        return DatabaseError::failure("задача не найдена");
    }

    std::vector<TaskRecord> listDueTasks(int limit, long long userId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string now = isoNow();
        std::vector<TaskRecord> result;
        for (const auto& entry : table("tasks").items()) {
            if (userId != 0 && entry.getInt("user_id") != userId) continue;
            if (entry.getString("status") != "pending") continue;
            if (!entry.getString("reminded_at").empty()) continue;  // уже напомнили
            const std::string remindAt = entry.getString("remind_at");
            if (remindAt.empty() || remindAt > now) continue;        // ещё не время
            result.push_back(toTask(entry));
        }
        std::sort(result.begin(), result.end(),
                  [](const TaskRecord& a, const TaskRecord& b) { return a.remindAt < b.remindAt; });
        if (limit > 0 && static_cast<int>(result.size()) > limit) {
            result.resize(static_cast<std::size_t>(limit));
        }
        return result;
    }

    DatabaseError markTaskReminded(long long id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mutableTable("tasks")) {
            if (entry.getInt("id") != id) continue;
            if (!entry.getString("reminded_at").empty()) return DatabaseError::failure("напоминание уже отправлено");
            entry.set("reminded_at", Json(isoNow()));
            flush();
            return DatabaseError::success();
        }
        return DatabaseError::failure("задача не найдена");
    }

    // ------------------------------------------- integration_connections
    DatabaseError upsertIntegrationConnection(const IntegrationConnectionRecord& record,
                                              long long& outId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mutableTable("integration_connections")) {
            if (entry.getInt("user_id") == record.userId && entry.getString("provider") == record.provider) {
                entry.set("account", Json(record.account));
                entry.set("scope", Json(record.scope));
                entry.set("token_encrypted", Json(record.tokenEncrypted));
                entry.set("status", Json(record.status.empty() ? std::string("active") : record.status));
                entry.set("last_error", Json(record.lastError));
                flush();
                outId = entry.getInt("id");
                return DatabaseError::success();
            }
        }
        const long long id = nextId("integration_connections");
        Json entry = Json::object();
        entry.set("id", Json(id));
        entry.set("user_id", Json(record.userId));
        entry.set("provider", Json(record.provider));
        entry.set("account", Json(record.account));
        entry.set("scope", Json(record.scope));
        entry.set("token_encrypted", Json(record.tokenEncrypted));
        entry.set("status", Json(record.status.empty() ? std::string("active") : record.status));
        entry.set("last_error", Json(record.lastError));
        entry.set("created_at", Json(isoNow()));
        entry.set("last_used_at", Json(record.lastUsedAt));
        table("integration_connections").push(entry);
        flush();
        outId = id;
        return DatabaseError::success();
    }

    std::optional<IntegrationConnectionRecord> findIntegrationConnection(
        long long userId, const std::string& provider) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : table("integration_connections").items()) {
            if (entry.getInt("user_id") == userId && entry.getString("provider") == provider) {
                return toIntegration(entry);
            }
        }
        return std::nullopt;
    }

    std::vector<IntegrationConnectionRecord> listIntegrationConnections(long long userId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<IntegrationConnectionRecord> result;
        for (const auto& entry : table("integration_connections").items()) {
            if (entry.getInt("user_id") != userId) continue;
            result.push_back(toIntegration(entry));
        }
        std::sort(result.begin(), result.end(),
                  [](const IntegrationConnectionRecord& a, const IntegrationConnectionRecord& b) {
                      return a.provider < b.provider;
                  });
        return result;
    }

    DatabaseError updateIntegrationToken(long long id,
                                         const std::string& tokenEncrypted,
                                         const std::string& status,
                                         const std::string& lastError) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mutableTable("integration_connections")) {
            if (entry.getInt("id") != id) continue;
            entry.set("token_encrypted", Json(tokenEncrypted));
            entry.set("status", Json(status));
            entry.set("last_error", Json(lastError));
            flush();
            return DatabaseError::success();
        }
        return DatabaseError::failure("подключение не найдено");
    }

    DatabaseError touchIntegrationUsed(long long id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mutableTable("integration_connections")) {
            if (entry.getInt("id") != id) continue;
            entry.set("last_used_at", Json(isoNow()));
            flush();
            return DatabaseError::success();
        }
        return DatabaseError::failure("подключение не найдено");
    }

    DatabaseError deleteIntegrationConnection(long long id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        Json::Array& entries = mutableTable("integration_connections");
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            if (it->getInt("id") == id) {
                entries.erase(it);
                flush();
                return DatabaseError::success();
            }
        }
        return DatabaseError::failure("подключение не найдено");
    }

    // --------------------------------------------- integration_oauth_states
    DatabaseError createOauthState(const OauthStateRecord& record) override {
        std::lock_guard<std::mutex> lock(mutex_);
        Json entry = Json::object();
        entry.set("state", Json(record.state));
        entry.set("user_id", Json(record.userId));
        entry.set("provider", Json(record.provider));
        entry.set("verifier", Json(record.verifier));
        entry.set("redirect_uri", Json(record.redirectUri));
        entry.set("created_at", Json(isoNow()));
        entry.set("expires_at", Json(record.expiresAt));
        entry.set("used_at", Json(record.usedAt));
        table("integration_oauth_states").push(entry);
        flush();
        return DatabaseError::success();
    }

    std::optional<OauthStateRecord> findOauthState(const std::string& state) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : table("integration_oauth_states").items()) {
            if (entry.getString("state") == state) return toOauthState(entry);
        }
        return std::nullopt;
    }

    DatabaseError useOauthState(const std::string& state) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mutableTable("integration_oauth_states")) {
            if (entry.getString("state") != state) continue;
            entry.set("used_at", Json(isoNow()));
            flush();
            return DatabaseError::success();
        }
        return DatabaseError::failure("состояние OAuth не найдено");
    }

    // ------------------------------------------------------------ notifications
    DatabaseError createNotification(const NotificationRecord& record, long long& outId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const long long id = nextId("notifications");
        Json entry = Json::object();
        entry.set("id", Json(id));
        entry.set("user_id", Json(record.userId));
        entry.set("kind", Json(record.kind));
        entry.set("title", Json(record.title));
        entry.set("body", Json(record.body));
        entry.set("payload", record.payload);
        entry.set("read_at", Json(std::string()));
        entry.set("created_at", Json(isoNow()));
        table("notifications").push(entry);
        flush();
        outId = id;
        return DatabaseError::success();
    }

    std::vector<NotificationRecord> listNotifications(long long userId,
                                                      bool unreadOnly,
                                                      int limit) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const int ceiling = limit > 0 ? limit : 50;
        std::vector<NotificationRecord> result;
        const Json::Array& entries = table("notifications").items();
        // Свежие первыми (аналог ORDER BY id DESC).
        for (auto it = entries.rbegin();
             it != entries.rend() && static_cast<int>(result.size()) < ceiling; ++it) {
            if (it->getInt("user_id") != userId) continue;
            if (unreadOnly && !it->getString("read_at").empty()) continue;
            result.push_back(toNotification(*it));
        }
        return result;
    }

    long long unreadNotificationCount(long long userId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        long long count = 0;
        for (const auto& entry : table("notifications").items()) {
            if (entry.getInt("user_id") != userId) continue;
            if (entry.getString("read_at").empty()) ++count;
        }
        return count;
    }

    DatabaseError markNotificationRead(long long userId, long long id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mutableTable("notifications")) {
            if (entry.getInt("id") != id || entry.getInt("user_id") != userId) continue;
            entry.set("read_at", Json(isoNow()));
            flush();
            return DatabaseError::success();
        }
        return DatabaseError::failure("уведомление не найдено");
    }

    DatabaseError markAllNotificationsRead(long long userId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        bool touched = false;
        for (auto& entry : mutableTable("notifications")) {
            if (entry.getInt("user_id") != userId) continue;
            if (!entry.getString("read_at").empty()) continue;
            entry.set("read_at", Json(isoNow()));
            touched = true;
        }
        if (touched) flush();
        return DatabaseError::success();
    }

    // ------------------------------------------------------------- push_devices
    DatabaseError registerPushDevice(long long userId,
                                     const std::string& platform,
                                     const std::string& token,
                                     long long& outId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mutableTable("push_devices")) {
            if (entry.getInt("user_id") == userId && entry.getString("token") == token) {
                entry.set("platform", Json(platform));
                entry.set("enabled", Json(true));
                flush();
                outId = entry.getInt("id");
                return DatabaseError::success();
            }
        }
        const long long id = nextId("push_devices");
        Json entry = Json::object();
        entry.set("id", Json(id));
        entry.set("user_id", Json(userId));
        entry.set("platform", Json(platform));
        entry.set("token", Json(token));
        entry.set("enabled", Json(true));
        entry.set("created_at", Json(isoNow()));
        entry.set("last_used_at", Json(std::string()));
        table("push_devices").push(entry);
        flush();
        outId = id;
        return DatabaseError::success();
    }

    std::vector<PushDeviceRecord> listPushDevices(long long userId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<PushDeviceRecord> result;
        for (const auto& entry : table("push_devices").items()) {
            if (entry.getInt("user_id") != userId) continue;
            result.push_back(toPushDevice(entry));
        }
        return result;
    }

    DatabaseError touchPushDevice(long long id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mutableTable("push_devices")) {
            if (entry.getInt("id") != id) continue;
            entry.set("last_used_at", Json(isoNow()));
            flush();
            return DatabaseError::success();
        }
        return DatabaseError::failure("устройство не найдено");
    }

    DatabaseError deletePushDevice(long long userId, long long id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        Json::Array& entries = mutableTable("push_devices");
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            if (it->getInt("id") != id || it->getInt("user_id") != userId) continue;
            entries.erase(it);
            flush();
            return DatabaseError::success();
        }
        return DatabaseError::failure("устройство не найдено");
    }

    // ------------------------------------------------------------ admin
    DatabaseError setUserRole(long long userId, const std::string& role) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : mutableTable("users")) {
            if (item.getInt("id") != userId) continue;
            item.set("role", Json(role));
            flush();
            return DatabaseError::success();
        }
        return DatabaseError::failure("пользователь не найден");
    }

    std::vector<UserRecord> adminListUsers(const std::string& query,
                                           const std::string& role,
                                           const std::string& status,
                                           int limit,
                                           int offset,
                                           long long& total) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<UserRecord> matched;
        for (const auto& item : table("users").items()) {
            const UserRecord user = toUser(item);
            if (!query.empty() && !contains(user.email, query) && !contains(user.displayName, query)) continue;
            if (!role.empty() && user.role != role) continue;
            if (status == "active" && !user.active) continue;
            if (status == "disabled" && user.active) continue;
            if (status == "unverified" && user.emailVerified) continue;
            matched.push_back(user);
        }
        std::sort(matched.begin(), matched.end(),
                  [](const UserRecord& a, const UserRecord& b) { return a.id > b.id; });
        total = static_cast<long long>(matched.size());
        std::vector<UserRecord> page;
        for (std::size_t i = static_cast<std::size_t>(std::max(0, offset));
             i < matched.size() && static_cast<int>(page.size()) < limit; ++i) {
            page.push_back(matched[i]);
        }
        return page;
    }

    Json adminStats() override {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string weekAgo = isoSecondsAgo(7LL * 24 * 3600);
        long long total = 0, verified = 0, disabled = 0, twoFactor = 0, fresh = 0, moderators = 0, admins = 0;
        for (const auto& item : table("users").items()) {
            const UserRecord user = toUser(item);
            ++total;
            if (user.emailVerified) ++verified;
            if (!user.active) ++disabled;
            if (user.createdAt.substr(0, 19) >= weekAgo) ++fresh;
            if (user.role == "moderator") ++moderators;
            if (user.role == "admin") ++admins;
        }
        for (const auto& item : table("two_factor_settings").items()) {
            if (item.getBool("enabled")) ++twoFactor;
        }
        const std::string now = isoSecondsAgo(0);
        long long sessions = 0;
        for (const auto& item : table("sessions").items()) {
            if (sessionLiveJson(item, now)) ++sessions;
        }
        Json stats = Json::object();
        stats.set("users_total", Json(total));
        stats.set("users_verified", Json(verified));
        stats.set("users_disabled", Json(disabled));
        stats.set("users_2fa", Json(twoFactor));
        stats.set("users_new_7d", Json(fresh));
        stats.set("moderators", Json(moderators));
        stats.set("admins", Json(admins));
        stats.set("sessions_active", Json(sessions));
        return stats;
    }

    std::vector<AuditRecord> listAuditPage(long long userId, long long beforeId, int limit) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<AuditRecord> result;
        const auto& logs = table("audit_logs").items();
        // id растут вместе с порядком вставки: идём с конца — самые новые первыми.
        for (auto it = logs.rbegin(); it != logs.rend(); ++it) {
            if (userId > 0 && it->getInt("user_id") != userId) continue;
            if (beforeId > 0 && it->getInt("id") >= beforeId) continue;
            AuditRecord record;
            record.id = it->getInt("id");
            record.userId = it->getInt("user_id");
            record.kind = it->getString("kind");
            record.detail = it->get("detail");
            record.remoteAddr = it->getString("remote_addr");
            record.createdAt = it->getString("created_at");
            result.push_back(record);
            if (static_cast<int>(result.size()) >= limit) break;
        }
        return result;
    }

    // ------------------------------------------------------- meeting_invites
    DatabaseError createInvite(const InviteRecord& record, long long& outId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (record.tokenHash.empty() || record.topic.empty()) {
            return DatabaseError::failure("приглашение без токена или темы");
        }
        for (const auto& entry : table("meeting_invites").items()) {
            if (entry.getString("token_hash") == record.tokenHash) {
                return DatabaseError::failure("токен приглашения уже существует");
            }
        }
        const long long id = nextId("meeting_invites");
        Json entry = Json::object();
        entry.set("id", Json(id));
        entry.set("owner_id", Json(record.ownerId));
        entry.set("token_hash", Json(record.tokenHash));
        entry.set("topic", Json(record.topic));
        entry.set("note", Json(record.note));
        entry.set("duration_minutes", Json(static_cast<long long>(record.durationMinutes)));
        entry.set("slots", record.slots.isArray() ? record.slots : Json::array());
        entry.set("status", Json(std::string("open")));
        entry.set("guest_name", Json(record.guestName));
        entry.set("guest_email", Json(std::string()));
        entry.set("guest_comment", Json(std::string()));
        entry.set("chosen", Json::object());
        entry.set("expires_at", Json(record.expiresAt));
        entry.set("created_at", Json(isoNow()));
        entry.set("responded_at", Json(std::string()));
        table("meeting_invites").push(entry);
        flush();
        outId = id;
        return DatabaseError::success();
    }

    std::optional<InviteRecord> findInvite(long long id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : table("meeting_invites").items()) {
            if (entry.getInt("id") == id) return toInvite(entry);
        }
        return std::nullopt;
    }

    std::optional<InviteRecord> findInviteByToken(const std::string& tokenHash) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tokenHash.empty()) return std::nullopt;
        for (const auto& entry : table("meeting_invites").items()) {
            if (entry.getString("token_hash") == tokenHash) return toInvite(entry);
        }
        return std::nullopt;
    }

    std::vector<InviteRecord> listInvites(long long ownerId, int limit) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<InviteRecord> result;
        const auto& entries = table("meeting_invites").items();
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            if (it->getInt("owner_id") != ownerId) continue;
            result.push_back(toInvite(*it));
            if (limit > 0 && static_cast<int>(result.size()) >= limit) break;
        }
        return result;
    }

    int countOpenInvites(long long ownerId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string now = isoNow();
        int count = 0;
        for (const auto& entry : table("meeting_invites").items()) {
            if (entry.getInt("owner_id") == ownerId && entry.getString("status") == "open" &&
                entry.getString("expires_at") > now) {
                ++count;
            }
        }
        return count;
    }

    bool closeInvite(long long id, const std::string& status, const InviteRecord& response) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mutableTable("meeting_invites")) {
            if (entry.getInt("id") != id) continue;
            if (entry.getString("status") != "open") return false;
            entry.set("status", Json(status));
            if (status != "cancelled") {
                entry.set("guest_name", Json(response.guestName));
                entry.set("guest_email", Json(response.guestEmail));
                entry.set("guest_comment", Json(response.guestComment));
                entry.set("chosen", response.chosen.isObject() ? response.chosen : Json::object());
            }
            entry.set("responded_at", Json(isoNow()));
            flush();
            return true;
        }
        return false;
    }

    // --------------------------------------------------------- booking_pages
    DatabaseError createBookingPage(const BookingPageRecord& record, long long& outId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (record.slug.empty() || record.title.empty()) return DatabaseError::failure("страница без slug или названия");
        for (const auto& entry : table("booking_pages").items()) {
            if (entry.getString("slug") == record.slug) return DatabaseError::failure("slug уже занят");
        }
        const long long id = nextId("booking_pages");
        Json entry = bookingPageJson(record);
        entry.set("id", Json(id));
        entry.set("owner_id", Json(record.ownerId));
        entry.set("created_at", Json(isoNow()));
        entry.set("updated_at", Json(isoNow()));
        table("booking_pages").push(entry);
        flush();
        outId = id;
        return DatabaseError::success();
    }

    DatabaseError updateBookingPage(const BookingPageRecord& record) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : table("booking_pages").items()) {
            if (entry.getInt("id") != record.id && entry.getString("slug") == record.slug) {
                return DatabaseError::failure("slug уже занят");
            }
        }
        for (auto& entry : mutableTable("booking_pages")) {
            if (entry.getInt("id") != record.id) continue;
            const Json fields = bookingPageJson(record);
            for (const auto& key : {"slug", "title", "description", "duration_minutes", "window_days",
                                    "min_notice_hours", "buffer_minutes", "hours", "weekdays", "allow_weekly",
                                    "active"}) {
                entry.set(key, fields.get(key));
            }
            entry.set("updated_at", Json(isoNow()));
            flush();
            return DatabaseError::success();
        }
        return DatabaseError::failure("страница записи не найдена");
    }

    DatabaseError deleteBookingPage(long long id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& pages = mutableTable("booking_pages");
        const auto before = pages.size();
        pages.erase(std::remove_if(pages.begin(), pages.end(), [id](const Json& e) { return e.getInt("id") == id; }),
                    pages.end());
        if (pages.size() == before) return DatabaseError::failure("страница записи не найдена");
        // ON DELETE SET NULL: записи остаются без страницы.
        for (auto& booking : mutableTable("bookings")) {
            if (booking.getInt("page_id") == id) booking.set("page_id", Json(0LL));
        }
        flush();
        return DatabaseError::success();
    }

    std::optional<BookingPageRecord> findBookingPage(long long id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : table("booking_pages").items()) {
            if (entry.getInt("id") == id) return toBookingPage(entry);
        }
        return std::nullopt;
    }

    std::optional<BookingPageRecord> findBookingPageBySlug(const std::string& slug) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (slug.empty()) return std::nullopt;
        for (const auto& entry : table("booking_pages").items()) {
            if (entry.getString("slug") == slug) return toBookingPage(entry);
        }
        return std::nullopt;
    }

    std::vector<BookingPageRecord> listBookingPages(long long ownerId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<BookingPageRecord> result;
        for (const auto& entry : table("booking_pages").items()) {
            if (entry.getInt("owner_id") == ownerId) result.push_back(toBookingPage(entry));
        }
        return result;
    }

    // -------------------------------------------------------------- bookings
    DatabaseError createBooking(const BookingRecord& record, long long& outId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (record.manageTokenHash.empty()) return DatabaseError::failure("запись без токена");
        for (const auto& entry : table("bookings").items()) {
            if (entry.getString("manage_token_hash") == record.manageTokenHash) {
                return DatabaseError::failure("токен записи уже существует");
            }
        }
        const long long id = nextId("bookings");
        Json entry = Json::object();
        entry.set("id", Json(id));
        entry.set("owner_id", Json(record.ownerId));
        entry.set("page_id", Json(record.pageId));
        entry.set("manage_token_hash", Json(record.manageTokenHash));
        entry.set("title", Json(record.title));
        entry.set("duration_minutes", Json(static_cast<long long>(record.durationMinutes)));
        entry.set("guest_name", Json(record.guestName));
        entry.set("guest_email", Json(record.guestEmail));
        entry.set("guest_comment", Json(record.guestComment));
        entry.set("first_start", Json(record.firstStart));
        entry.set("weekly", Json(record.weekly));
        entry.set("weeks", Json(static_cast<long long>(record.weeks)));
        entry.set("skipped", record.skipped.isArray() ? record.skipped : Json::array());
        entry.set("moved", record.moved.isObject() ? record.moved : Json::object());
        entry.set("reminded", record.reminded.isArray() ? record.reminded : Json::array());
        entry.set("status", Json(record.status.empty() ? std::string("active") : record.status));
        entry.set("created_at", Json(isoNow()));
        entry.set("updated_at", Json(isoNow()));
        table("bookings").push(entry);
        flush();
        outId = id;
        return DatabaseError::success();
    }

    DatabaseError updateBooking(const BookingRecord& record) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mutableTable("bookings")) {
            if (entry.getInt("id") != record.id) continue;
            entry.set("skipped", record.skipped.isArray() ? record.skipped : Json::array());
            entry.set("moved", record.moved.isObject() ? record.moved : Json::object());
            entry.set("reminded", record.reminded.isArray() ? record.reminded : Json::array());
            entry.set("weeks", Json(static_cast<long long>(record.weeks)));
            entry.set("status", Json(record.status));
            entry.set("updated_at", Json(isoNow()));
            flush();
            return DatabaseError::success();
        }
        return DatabaseError::failure("запись не найдена");
    }

    std::optional<BookingRecord> findBooking(long long id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : table("bookings").items()) {
            if (entry.getInt("id") == id) return toBooking(entry);
        }
        return std::nullopt;
    }

    std::optional<BookingRecord> findBookingByToken(const std::string& tokenHash) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tokenHash.empty()) return std::nullopt;
        for (const auto& entry : table("bookings").items()) {
            if (entry.getString("manage_token_hash") == tokenHash) return toBooking(entry);
        }
        return std::nullopt;
    }

    std::vector<BookingRecord> listActiveBookings(long long ownerId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<BookingRecord> result;
        for (const auto& entry : table("bookings").items()) {
            if (entry.getString("status") != "active") continue;
            if (ownerId > 0 && entry.getInt("owner_id") != ownerId) continue;
            result.push_back(toBooking(entry));
        }
        return result;
    }

    std::vector<BookingRecord> listBookings(long long ownerId, int limit) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<BookingRecord> result;
        const auto& entries = table("bookings").items();
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            if (it->getInt("owner_id") != ownerId) continue;
            result.push_back(toBooking(*it));
            if (limit > 0 && static_cast<int>(result.size()) >= limit) break;
        }
        return result;
    }

    // ------------------------------------------------------------- referrals
    std::string getReferralCode(long long userId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& user : table("users").items()) {
            if (user.getInt("id") == userId) return user.getString("referral_code");
        }
        return "";
    }

    bool setReferralCode(long long userId, const std::string& code) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& user : table("users").items()) {
            if (user.getInt("id") != userId && user.getString("referral_code") == code) return false;
        }
        for (auto& user : mutableTable("users")) {
            if (user.getInt("id") != userId) continue;
            user.set("referral_code", Json(code));
            flush();
            return true;
        }
        return false;
    }

    long long findUserIdByReferralCode(const std::string& code) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (code.empty()) return 0;
        for (const auto& user : table("users").items()) {
            if (user.getString("referral_code") == code) return user.getInt("id");
        }
        return 0;
    }

    std::string getProUntil(long long userId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& user : table("users").items()) {
            if (user.getInt("id") == userId) return user.getString("pro_until");
        }
        return "";
    }

    DatabaseError setProUntil(long long userId, const std::string& isoUntil) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& user : mutableTable("users")) {
            if (user.getInt("id") != userId) continue;
            user.set("pro_until", Json(isoUntil));
            flush();
            return DatabaseError::success();
        }
        return DatabaseError::failure("пользователь не найден");
    }

    DatabaseError createReferral(long long referrerId, long long inviteeId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (referrerId == inviteeId) return DatabaseError::failure("нельзя пригласить себя");
        for (const auto& entry : table("referrals").items()) {
            if (entry.getInt("invitee_id") == inviteeId) return DatabaseError::failure("приглашение уже учтено");
        }
        Json entry = Json::object();
        entry.set("id", Json(nextId("referrals")));
        entry.set("referrer_id", Json(referrerId));
        entry.set("invitee_id", Json(inviteeId));
        entry.set("created_at", Json(isoNow()));
        entry.set("qualified_at", Json(std::string()));
        table("referrals").push(entry);
        flush();
        return DatabaseError::success();
    }

    std::optional<ReferralRecord> findReferralByInvitee(long long inviteeId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : table("referrals").items()) {
            if (entry.getInt("invitee_id") != inviteeId) continue;
            ReferralRecord record;
            record.id = entry.getInt("id");
            record.referrerId = entry.getInt("referrer_id");
            record.inviteeId = inviteeId;
            record.createdAt = entry.getString("created_at");
            record.qualifiedAt = entry.getString("qualified_at");
            return record;
        }
        return std::nullopt;
    }

    bool qualifyReferral(long long id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : mutableTable("referrals")) {
            if (entry.getInt("id") != id) continue;
            if (!entry.getString("qualified_at").empty()) return false;
            entry.set("qualified_at", Json(isoNow()));
            flush();
            return true;
        }
        return false;
    }

    int countReferrals(long long referrerId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        int count = 0;
        for (const auto& entry : table("referrals").items()) {
            if (entry.getInt("referrer_id") == referrerId) ++count;
        }
        return count;
    }

    int countQualifiedReferrals(long long referrerId, const std::string& sinceIso) override {
        std::lock_guard<std::mutex> lock(mutex_);
        int count = 0;
        for (const auto& entry : table("referrals").items()) {
            const std::string qualified = entry.getString("qualified_at");
            if (entry.getInt("referrer_id") == referrerId && !qualified.empty() && qualified >= sinceIso) ++count;
        }
        return count;
    }

private:
    static Json bookingPageJson(const BookingPageRecord& record) {
        Json entry = Json::object();
        entry.set("slug", Json(record.slug));
        entry.set("title", Json(record.title));
        entry.set("description", Json(record.description));
        entry.set("duration_minutes", Json(static_cast<long long>(record.durationMinutes)));
        entry.set("window_days", Json(static_cast<long long>(record.windowDays)));
        entry.set("min_notice_hours", Json(static_cast<long long>(record.minNoticeHours)));
        entry.set("buffer_minutes", Json(static_cast<long long>(record.bufferMinutes)));
        entry.set("hours", record.hours.isArray() ? record.hours : Json::array());
        entry.set("weekdays", record.weekdays.isArray() ? record.weekdays : Json::array());
        entry.set("allow_weekly", Json(record.allowWeekly));
        entry.set("active", Json(record.active));
        return entry;
    }

    BookingPageRecord toBookingPage(const Json& entry) const {
        BookingPageRecord record;
        record.id = entry.getInt("id");
        record.ownerId = entry.getInt("owner_id");
        record.slug = entry.getString("slug");
        record.title = entry.getString("title");
        record.description = entry.getString("description");
        record.durationMinutes = static_cast<int>(entry.getInt("duration_minutes", 60));
        record.windowDays = static_cast<int>(entry.getInt("window_days", 14));
        record.minNoticeHours = static_cast<int>(entry.getInt("min_notice_hours", 12));
        record.bufferMinutes = static_cast<int>(entry.getInt("buffer_minutes", 0));
        record.hours = entry.get("hours").isArray() ? entry.get("hours") : Json::array();
        record.weekdays = entry.get("weekdays").isArray() ? entry.get("weekdays") : Json::array();
        record.allowWeekly = entry.getBool("allow_weekly", true);
        record.active = entry.getBool("active", true);
        record.createdAt = entry.getString("created_at");
        record.updatedAt = entry.getString("updated_at");
        return record;
    }

    BookingRecord toBooking(const Json& entry) const {
        BookingRecord record;
        record.id = entry.getInt("id");
        record.ownerId = entry.getInt("owner_id");
        record.pageId = entry.getInt("page_id");
        record.manageTokenHash = entry.getString("manage_token_hash");
        record.title = entry.getString("title");
        record.durationMinutes = static_cast<int>(entry.getInt("duration_minutes", 60));
        record.guestName = entry.getString("guest_name");
        record.guestEmail = entry.getString("guest_email");
        record.guestComment = entry.getString("guest_comment");
        record.firstStart = entry.getString("first_start");
        record.weekly = entry.getBool("weekly", false);
        record.weeks = static_cast<int>(entry.getInt("weeks", 1));
        record.skipped = entry.get("skipped").isArray() ? entry.get("skipped") : Json::array();
        record.moved = entry.get("moved").isObject() ? entry.get("moved") : Json::object();
        record.reminded = entry.get("reminded").isArray() ? entry.get("reminded") : Json::array();
        record.status = entry.getString("status", "active");
        record.createdAt = entry.getString("created_at");
        record.updatedAt = entry.getString("updated_at");
        return record;
    }

    InviteRecord toInvite(const Json& entry) const {
        InviteRecord record;
        record.id = entry.getInt("id");
        record.ownerId = entry.getInt("owner_id");
        record.tokenHash = entry.getString("token_hash");
        record.topic = entry.getString("topic");
        record.note = entry.getString("note");
        record.durationMinutes = static_cast<int>(entry.getInt("duration_minutes", 60));
        record.slots = entry.get("slots").isArray() ? entry.get("slots") : Json::array();
        record.status = entry.getString("status", "open");
        record.guestName = entry.getString("guest_name");
        record.guestEmail = entry.getString("guest_email");
        record.guestComment = entry.getString("guest_comment");
        record.chosen = entry.get("chosen").isObject() ? entry.get("chosen") : Json::object();
        record.expiresAt = entry.getString("expires_at");
        record.createdAt = entry.getString("created_at");
        record.respondedAt = entry.getString("responded_at");
        return record;
    }

    NotificationRecord toNotification(const Json& entry) const {
        NotificationRecord record;
        record.id = entry.getInt("id");
        record.userId = entry.getInt("user_id");
        record.kind = entry.getString("kind");
        record.title = entry.getString("title");
        record.body = entry.getString("body");
        record.payload = entry.get("payload");
        record.readAt = entry.getString("read_at");
        record.createdAt = entry.getString("created_at");
        return record;
    }

    PushDeviceRecord toPushDevice(const Json& entry) const {
        PushDeviceRecord record;
        record.id = entry.getInt("id");
        record.userId = entry.getInt("user_id");
        record.platform = entry.getString("platform", "apns");
        record.token = entry.getString("token");
        record.enabled = entry.getBool("enabled", true);
        record.createdAt = entry.getString("created_at");
        record.lastUsedAt = entry.getString("last_used_at");
        return record;
    }

    IntegrationConnectionRecord toIntegration(const Json& entry) const {
        IntegrationConnectionRecord record;
        record.id = entry.getInt("id");
        record.userId = entry.getInt("user_id");
        record.provider = entry.getString("provider");
        record.account = entry.getString("account");
        record.scope = entry.getString("scope");
        record.tokenEncrypted = entry.getString("token_encrypted");
        record.status = entry.getString("status", "active");
        record.lastError = entry.getString("last_error");
        record.createdAt = entry.getString("created_at");
        record.lastUsedAt = entry.getString("last_used_at");
        return record;
    }

    OauthStateRecord toOauthState(const Json& entry) const {
        OauthStateRecord record;
        record.state = entry.getString("state");
        record.userId = entry.getInt("user_id");
        record.provider = entry.getString("provider");
        record.verifier = entry.getString("verifier");
        record.redirectUri = entry.getString("redirect_uri");
        record.createdAt = entry.getString("created_at");
        record.expiresAt = entry.getString("expires_at");
        record.usedAt = entry.getString("used_at");
        return record;
    }

    TaskRecord toTask(const Json& entry) const {
        TaskRecord record;
        record.id = entry.getInt("id");
        record.userId = entry.getInt("user_id");
        record.chatId = entry.getInt("chat_id");
        record.title = entry.getString("title");
        record.notes = entry.getString("notes");
        record.status = entry.getString("status", "pending");
        record.priority = static_cast<int>(entry.getInt("priority"));
        record.dueAt = entry.getString("due_at");
        record.remindAt = entry.getString("remind_at");
        record.remindedAt = entry.getString("reminded_at");
        record.createdAt = entry.getString("created_at");
        record.completedAt = entry.getString("completed_at");
        return record;
    }

    ToolPermissionRecord toToolPermission(const Json& entry) const {
        ToolPermissionRecord record;
        record.userId = entry.getInt("user_id");
        record.tool = entry.getString("tool");
        record.mode = entry.getString("mode", "ask");
        record.updatedAt = entry.getString("updated_at");
        return record;
    }

    PendingActionRecord toPendingAction(const Json& entry) const {
        PendingActionRecord record;
        record.id = entry.getInt("id");
        record.userId = entry.getInt("user_id");
        record.chatId = entry.getInt("chat_id");
        record.tool = entry.getString("tool");
        record.args = entry.get("args").isObject() ? entry.get("args") : Json::object();
        record.summary = entry.getString("summary");
        record.status = entry.getString("status", "pending");
        record.result = entry.get("result").isObject() ? entry.get("result") : Json::object();
        record.createdAt = entry.getString("created_at");
        record.resolvedAt = entry.getString("resolved_at");
        return record;
    }

    // Версия без блокировки: вызывается из методов, которые уже держат mutex_.
    std::optional<UserRecord> findUserByIdLocked(long long id) const {
        const Json* users = state_.find("users");
        if (users == nullptr) return std::nullopt;
        for (const auto& item : users->items()) {
            if (item.getInt("id") == id) return toUser(item);
        }
        return std::nullopt;
    }

    Json& table(const std::string& name) {
        if (!state_.contains(name)) state_.set(name, Json::array());
        return state_[name];
    }

    Json::Array& mutableTable(const std::string& name) { return table(name).items(); }

    long long nextId(const std::string& name) {
        long long maxId = 0;
        for (const auto& item : table(name).items()) maxId = std::max(maxId, item.getInt("id"));
        return maxId + 1;
    }

    void flush() {
        if (path_.empty()) return;
        // Пишем во временный файл и переименовываем: иначе обрыв процесса
        // посреди записи оставляет усечённый JSON, и база не загружается вовсе.
        // Права 0600 с момента создания: в файле хэши паролей и токенов.
        const std::string temporary = path_ + ".tmp";
        const std::string data = state_.dump(2);
        const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd < 0) {
            AURA_LOG(log::Level::Warn, "db") << "не удалось сохранить " << temporary << ": " << std::strerror(errno);
            return;
        }
        ::fchmod(fd, 0600);  // файл мог остаться от старой версии с другими правами
        std::size_t written = 0;
        while (written < data.size()) {
            const ssize_t chunk = ::write(fd, data.data() + written, data.size() - written);
            if (chunk < 0) {
                if (errno == EINTR) continue;
                AURA_LOG(log::Level::Warn, "db") << "ошибка записи " << temporary << ": " << std::strerror(errno);
                ::close(fd);
                return;
            }
            written += static_cast<std::size_t>(chunk);
        }
        ::close(fd);
        if (std::rename(temporary.c_str(), path_.c_str()) != 0) {
            AURA_LOG(log::Level::Warn, "db")
                << "не удалось заменить " << path_ << ": " << std::strerror(errno);
        }
    }

    static UserRecord toUser(const Json& json) {
        UserRecord record;
        record.id = json.getInt("id");
        record.email = json.getString("email");
        record.passwordHash = json.getString("password_hash");
        record.displayName = json.getString("display_name");
        record.avatarUrl = json.getString("avatar_url");
        record.bio = json.getString("bio");
        record.timezone = json.getString("timezone", "Europe/Moscow");
        record.createdAt = json.getString("created_at");
        record.lastSeenAt = json.getString("last_seen_at");
        record.active = json.getBool("is_active", true);
        // Старые записи без поля считаем подтверждёнными (миграция схемы).
        record.emailVerified = json.getBool("email_verified", true);
        record.role = json.getString("role", "user");
        if (record.role.empty()) record.role = "user";
        return record;
    }

    static SessionRecord toSession(const Json& json) {
        SessionRecord record;
        record.id = json.getString("id");
        record.userId = json.getInt("user_id");
        record.jwtId = json.getString("jwt_id");
        record.device = json.getString("device");
        record.remoteAddr = json.getString("remote_addr");
        record.createdAt = json.getString("created_at");
        record.expiresAt = json.getString("expires_at");
        record.revokedAt = json.getString("revoked_at");
        return record;
    }

    static AuthTokenRecord toAuthToken(const Json& json) {
        AuthTokenRecord record;
        record.id = json.getString("id");
        record.userId = json.getInt("user_id");
        record.purpose = json.getString("purpose");
        record.tokenHash = json.getString("token_hash");
        record.createdAt = json.getString("created_at");
        record.expiresAt = json.getString("expires_at");
        record.usedAt = json.getString("used_at");
        return record;
    }

    static RefreshTokenRecord toRefreshToken(const Json& json) {
        RefreshTokenRecord record;
        record.id = json.getString("id");
        record.userId = json.getInt("user_id");
        record.familyId = json.getString("family_id");
        record.tokenHash = json.getString("token_hash");
        record.device = json.getString("device");
        record.remoteAddr = json.getString("remote_addr");
        record.createdAt = json.getString("created_at");
        record.expiresAt = json.getString("expires_at");
        record.revokedAt = json.getString("revoked_at");
        record.replacedBy = json.getString("replaced_by");
        return record;
    }

    static TwoFactorRecord toTwoFactor(const Json& json) {
        TwoFactorRecord record;
        record.userId = json.getInt("user_id");
        record.secretEncrypted = json.getString("secret_encrypted");
        record.enabled = json.getBool("enabled", false);
        record.lastUsedCounter = json.getInt("last_used_counter");
        record.createdAt = json.getString("created_at");
        record.enabledAt = json.getString("enabled_at");
        record.updatedAt = json.getString("updated_at");
        return record;
    }

    static RecoveryCodeRecord toRecoveryCode(const Json& json) {
        RecoveryCodeRecord record;
        record.id = json.getString("id");
        record.userId = json.getInt("user_id");
        record.codeHash = json.getString("code_hash");
        record.createdAt = json.getString("created_at");
        record.usedAt = json.getString("used_at");
        return record;
    }

    static TrustedDeviceRecord toTrustedDevice(const Json& json) {
        TrustedDeviceRecord record;
        record.id = json.getString("id");
        record.userId = json.getInt("user_id");
        record.deviceHash = json.getString("device_hash");
        record.device = json.getString("device");
        record.remoteAddr = json.getString("remote_addr");
        record.createdAt = json.getString("created_at");
        record.expiresAt = json.getString("expires_at");
        record.lastUsedAt = json.getString("last_used_at");
        record.revokedAt = json.getString("revoked_at");
        return record;
    }


    ChatRecord toChat(const Json& json) const {
        ChatRecord record;
        record.id = json.getInt("id");
        record.kind = json.getString("kind", "direct");
        record.title = json.getString("title");
        record.createdBy = json.getInt("created_by");
        record.createdAt = json.getString("created_at");
        record.lastMessageAt = json.getString("last_message_at");
        return record;
    }

    void attachLastMessage(ChatRecord& record) const {
        const Json* messages = state_.find("messages");
        if (!messages) return;
        const Json* best = nullptr;
        for (const auto& message : messages->items()) {
            if (message.getInt("chat_id") != record.id) continue;
            if (best == nullptr || message.getInt("id") > best->getInt("id")) best = &message;
        }
        if (best == nullptr) return;
        record.lastMessageBody = best->getString("body");
        record.lastMessageAt = best->getString("created_at");
        record.lastMessageSenderId = best->getInt("sender_id");
        const Json* users = state_.find("users");
        if (users) {
            for (const auto& user : users->items()) {
                if (user.getInt("id") == record.lastMessageSenderId) {
                    record.lastMessageSender = user.getString("display_name");
                    break;
                }
            }
        }
    }

    MessageRecord toMessage(const Json& json) const {
        MessageRecord record;
        record.id = json.getInt("id");
        record.chatId = json.getInt("chat_id");
        record.senderId = json.getInt("sender_id");
        record.kind = json.getString("kind", "text");
        record.body = json.getString("body");
        record.payload = json.get("payload").isObject() ? json.get("payload") : Json::object();
        record.createdAt = json.getString("created_at");
        return record;
    }

    const std::string path_;
    mutable std::mutex mutex_;
    Json state_ = Json::object();
};

}  // namespace

std::unique_ptr<IDatabase> makeEmbeddedDatabase(const std::string& path) {
    return std::make_unique<EmbeddedDatabase>(path);
}

}  // namespace aura
