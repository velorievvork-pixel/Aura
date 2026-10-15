// selfvoy/config.cpp
#include "selfvoy/config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

#include "selfvoy/crypto.h"

namespace selfvoy {

namespace {
constexpr char kPrefix[] = "SELFVOY_";
// Префикс до переименования продукта: читается, только если SELFVOY_X не задана.
constexpr char kLegacyPrefix[] = "AURA_";

// Сырое значение SELFVOY_X (nullptr/пусто — не задана) с запасным AURA_X.
const char* lookupEnv(const char* name, std::vector<std::string>* legacyNames) {
    const char* value = std::getenv(name);
    if (value && *value) return value;
    const std::string key = name;
    const std::string prefix = kPrefix;
    if (key.rfind(prefix, 0) != 0) return value;
    const std::string legacy = kLegacyPrefix + key.substr(prefix.size());
    const char* old = std::getenv(legacy.c_str());
    if (!old || !*old) return value;
    if (legacyNames && std::find(legacyNames->begin(), legacyNames->end(), legacy) == legacyNames->end()) {
        legacyNames->push_back(legacy);
    }
    return old;
}

int intOr(const char* name, int fallback, std::vector<std::string>* legacyNames) {
    const char* value = lookupEnv(name, legacyNames);
    if (!value || !*value) return fallback;
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}
bool flagOn(const char* name, std::vector<std::string>* legacyNames) {
    const char* value = lookupEnv(name, legacyNames);
    if (!value) return false;
    std::string text = value;
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text == "1" || text == "true" || text == "yes" || text == "on";
}

std::string upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return text;
}

}  // namespace

std::string envOr(const char* name, const std::string& fallback, std::vector<std::string>* legacyNames) {
    const char* value = lookupEnv(name, legacyNames);
    if (!value || !*value) return fallback;
    return value;
}

Config Config::fromEnv() {
    Config config;
    std::vector<std::string>* legacy = &config.legacyEnvNames;
    config.host = envOr("SELFVOY_HOST", config.host, legacy);
    config.port = intOr("SELFVOY_PORT", config.port, legacy);
    config.databaseUrl = envOr("SELFVOY_DATABASE_URL", "", legacy);
    config.embeddedDbPath = envOr("SELFVOY_EMBEDDED_DB", config.embeddedDbPath, legacy);
    config.aiServiceUrl = envOr("SELFVOY_AI_URL", config.aiServiceUrl, legacy);
    config.aiServiceToken = envOr("SELFVOY_AI_TOKEN", "", legacy);
    config.aiTimeoutMs = intOr("SELFVOY_AI_TIMEOUT_MS", config.aiTimeoutMs, legacy);
    config.devMode = flagOn("SELFVOY_DEV_MODE", legacy);
    config.jwtSecret = envOr("SELFVOY_JWT_SECRET", config.jwtSecret, legacy);
    config.twoFactorKey = envOr("SELFVOY_2FA_KEY", config.twoFactorKey, legacy);
    config.tokenTtlSeconds = intOr("SELFVOY_TOKEN_TTL", static_cast<int>(config.tokenTtlSeconds), legacy);
    config.refreshTtlSeconds = intOr("SELFVOY_REFRESH_TTL", static_cast<int>(config.refreshTtlSeconds), legacy);
    config.verifyTtlSeconds = intOr("SELFVOY_VERIFY_TTL", static_cast<int>(config.verifyTtlSeconds), legacy);
    config.resetTtlSeconds = intOr("SELFVOY_RESET_TTL", static_cast<int>(config.resetTtlSeconds), legacy);
    config.mailDriver = envOr("SELFVOY_MAIL_DRIVER", config.mailDriver, legacy);
    config.smtpHost = envOr("SELFVOY_SMTP_HOST", "", legacy);
    config.smtpPort = intOr("SELFVOY_SMTP_PORT", config.smtpPort, legacy);
    config.smtpUser = envOr("SELFVOY_SMTP_USER", "", legacy);
    config.smtpPassword = envOr("SELFVOY_SMTP_PASSWORD", "", legacy);
    config.smtpFrom = envOr("SELFVOY_SMTP_FROM", "", legacy);
    config.smtpTls = envOr("SELFVOY_SMTP_TLS", config.smtpTls, legacy);
    config.smtpCaFile = envOr("SELFVOY_SMTP_CA_FILE", "", legacy);
    config.smtpTimeoutMs = intOr("SELFVOY_SMTP_TIMEOUT_MS", config.smtpTimeoutMs, legacy);
    config.toolsMode = envOr("SELFVOY_TOOLS_MODE", config.toolsMode, legacy);
    config.emailApiUrl = envOr("SELFVOY_EMAIL_API_URL", "", legacy);
    config.mapsApiUrl = envOr("SELFVOY_MAPS_API_URL", "", legacy);
    config.maxFrameBytes = static_cast<std::size_t>(intOr("SELFVOY_MAX_FRAME", static_cast<int>(config.maxFrameBytes), legacy));
    config.maxPreAuthFrameBytes = static_cast<std::size_t>(
        std::max(1024, intOr("SELFVOY_MAX_PREAUTH_FRAME", static_cast<int>(config.maxPreAuthFrameBytes), legacy)));
    config.maxConnections =
        static_cast<std::size_t>(intOr("SELFVOY_MAX_CONNECTIONS", static_cast<int>(config.maxConnections), legacy));
    config.maxConnectionsPerIp = static_cast<std::size_t>(
        std::max(0, intOr("SELFVOY_MAX_CONNECTIONS_PER_IP", static_cast<int>(config.maxConnectionsPerIp), legacy)));
    config.maxLoginAttempts = intOr("SELFVOY_MAX_LOGIN_ATTEMPTS", config.maxLoginAttempts, legacy);
    config.maxIpAttempts = intOr("SELFVOY_MAX_IP_ATTEMPTS", config.maxIpAttempts, legacy);
    config.loginWindowSec = intOr("SELFVOY_LOGIN_WINDOW", config.loginWindowSec, legacy);
    config.maxTwoFactorAttempts = intOr("SELFVOY_MAX_2FA_ATTEMPTS", config.maxTwoFactorAttempts, legacy);
    config.twoFactorWindowSec = intOr("SELFVOY_2FA_WINDOW", config.twoFactorWindowSec, legacy);
    config.maxCodeAttempts = intOr("SELFVOY_MAX_CODE_ATTEMPTS", config.maxCodeAttempts, legacy);
    config.mailPerEmailPerHour = intOr("SELFVOY_MAIL_PER_EMAIL_HOUR", config.mailPerEmailPerHour, legacy);
    config.mailPerIpPerHour = intOr("SELFVOY_MAIL_PER_IP_HOUR", config.mailPerIpPerHour, legacy);
    config.registerPerIpPerHour = intOr("SELFVOY_REGISTER_PER_IP_HOUR", config.registerPerIpPerHour, legacy);
    config.pingIntervalSec = intOr("SELFVOY_PING_INTERVAL", config.pingIntervalSec, legacy);
    config.socketTimeoutSec = intOr("SELFVOY_SOCKET_TIMEOUT", config.socketTimeoutSec, legacy);
    config.authTimeoutSec = intOr("SELFVOY_AUTH_TIMEOUT", config.authTimeoutSec, legacy);
    config.authRecheckSec = intOr("SELFVOY_AUTH_RECHECK", config.authRecheckSec, legacy);
    config.schedulerIntervalMs = intOr("SELFVOY_SCHEDULER_INTERVAL_MS", config.schedulerIntervalMs, legacy);
    config.googleClientId = envOr("SELFVOY_GOOGLE_CLIENT_ID", config.googleClientId, legacy);
    config.googleClientSecret = envOr("SELFVOY_GOOGLE_CLIENT_SECRET", config.googleClientSecret, legacy);
    config.googleAuthUrl = envOr("SELFVOY_GOOGLE_AUTH_URL", config.googleAuthUrl, legacy);
    config.googleTokenUrl = envOr("SELFVOY_GOOGLE_TOKEN_URL", config.googleTokenUrl, legacy);
    config.googleRevokeUrl = envOr("SELFVOY_GOOGLE_REVOKE_URL", config.googleRevokeUrl, legacy);
    config.googleCalendarUrl = envOr("SELFVOY_GOOGLE_CALENDAR_URL", config.googleCalendarUrl, legacy);
    config.googleGmailUrl = envOr("SELFVOY_GOOGLE_GMAIL_URL", config.googleGmailUrl, legacy);
    config.oauthStateTtlSec = intOr("SELFVOY_OAUTH_STATE_TTL", config.oauthStateTtlSec, legacy);
    config.pushDriver = envOr("SELFVOY_PUSH_DRIVER", config.pushDriver, legacy);
    config.pushWebhookUrl = envOr("SELFVOY_PUSH_WEBHOOK_URL", config.pushWebhookUrl, legacy);
    config.apnsTopic = envOr("SELFVOY_APNS_TOPIC", config.apnsTopic, legacy);
    config.apnsUrl = envOr("SELFVOY_APNS_URL", config.apnsUrl, legacy);
    config.fcmUrl = envOr("SELFVOY_FCM_URL", config.fcmUrl, legacy);
    config.logLevel = envOr("SELFVOY_LOG_LEVEL", config.logLevel, legacy);
    config.adminEmails = envOr("SELFVOY_ADMIN_EMAILS", "", legacy);
    config.trustedProxies = envOr("SELFVOY_TRUSTED_PROXIES", "", legacy);
    config.publicUrl = envOr("SELFVOY_PUBLIC_URL", "", legacy);
    // Пул соединений PostgreSQL читает SELFVOY_DB_POOL_SIZE сам (database_pg.cpp);
    // здесь — только чтобы устаревшее AURA_DB_POOL_SIZE попало в предупреждение.
    envOr("SELFVOY_DB_POOL_SIZE", "", legacy);
    while (!config.publicUrl.empty() && config.publicUrl.back() == '/') config.publicUrl.pop_back();
    return config;
}

std::string Config::describe() const {
    std::ostringstream out;
    out << "host=" << host << ':' << port
        << " db=" << (databaseUrl.empty() ? std::string("embedded(") + embeddedDbPath + ")" : "postgresql")
        << " ai=" << aiServiceUrl << " tools=" << toolsMode
        << " token_ttl=" << tokenTtlSeconds << 's'
        << " refresh_ttl=" << refreshTtlSeconds << 's'
        << " mail=" << mailDriver
        << (devMode ? " DEV_MODE" : "");
    return out.str();
}

bool Config::weakSecret(const std::string& secret) {
    if (secret.size() < 32) return true;
    if (secret == "selfvoy-dev-secret" || secret == "test-secret") return true;
    const std::string up = upper(secret);
    return up.find("CHANGE-IN-PRODUCTION") != std::string::npos ||
           up.find("CHANGE_IN_PRODUCTION") != std::string::npos ||
           up.find("CHANGE-ME") != std::string::npos || up.find("CHANGEME") != std::string::npos;
}

bool Config::validate(std::string& error, std::vector<std::string>& warnings) {
    if (mailDriver != "dev" && mailDriver != "log" && mailDriver != "smtp") {
        error = "SELFVOY_MAIL_DRIVER: допустимы dev, log или smtp";
        return false;
    }
    if (mailDriver == "dev" && !devMode) {
        error = "SELFVOY_MAIL_DRIVER=dev отдаёт коды подтверждения и сброса пароля прямо в ответах API "
                "(любой может сбросить чужой пароль) — разрешён только вместе с SELFVOY_DEV_MODE=1. "
                "В продакшене используйте SELFVOY_MAIL_DRIVER=smtp (или log для стенда без почты)";
        return false;
    }

    if (weakSecret(jwtSecret)) {
        if (!devMode) {
            error = jwtSecret.empty()
                        ? "SELFVOY_JWT_SECRET не задан: без секрета любой может подделать токен. "
                          "Задайте случайную строку не короче 32 байт (например, openssl rand -hex 32) "
                          "или включите SELFVOY_DEV_MODE=1 для локальной разработки"
                        : "SELFVOY_JWT_SECRET слабый (короче 32 байт, dev-значение или заглушка "
                          "CHANGE-IN-PRODUCTION): задайте случайную строку не короче 32 байт "
                          "(openssl rand -hex 32)";
            return false;
        }
        if (jwtSecret.empty()) {
            jwtSecret = crypto::randomHex(32);
            warnings.push_back("SELFVOY_DEV_MODE: SELFVOY_JWT_SECRET не задан — сгенерирован случайный секрет "
                               "на время жизни процесса (токены не переживут перезапуск)");
        } else {
            warnings.push_back("SELFVOY_DEV_MODE: слабый SELFVOY_JWT_SECRET — допустимо только для разработки");
        }
    }

    if (twoFactorKey.empty() || upper(twoFactorKey).find("CHANGE-IN-PRODUCTION") != std::string::npos) {
        if (!devMode) {
            error = "SELFVOY_2FA_KEY не задан (или оставлена заглушка): ключ шифрования TOTP-секретов "
                    "обязателен. Задайте случайную строку (openssl rand -hex 32)";
            return false;
        }
        warnings.push_back("SELFVOY_DEV_MODE: SELFVOY_2FA_KEY не задан — TOTP-секреты шифруются ключом, "
                           "выведенным из SELFVOY_JWT_SECRET");
    }

    if (!publicUrl.empty() && publicUrl.rfind("https://", 0) != 0 && publicUrl.rfind("http://", 0) != 0) {
        error = "SELFVOY_PUBLIC_URL: нужен адрес веб-клиента вида https://selfvoy.example.com";
        return false;
    }
    if (publicUrl.rfind("http://", 0) == 0 && !devMode) {
        warnings.push_back("SELFVOY_PUBLIC_URL без https: ссылки-приглашения уйдут гостям по открытому каналу");
    }

    if (resetTtlSeconds <= 0 || resetTtlSeconds > 3600) {
        warnings.push_back("SELFVOY_RESET_TTL ограничен 1 часом (шестизначный код сброса живёт недолго)");
        resetTtlSeconds = 3600;
    }
    if (devMode) {
        warnings.push_back("SELFVOY_DEV_MODE=1: режим разработки, не используйте его в продакшене");
    }
    if (!legacyEnvNames.empty()) {
        std::string names;
        for (const auto& name : legacyEnvNames) names += (names.empty() ? "" : ", ") + name;
        warnings.push_back("устаревшие переменные окружения " + names +
                           " — переименуйте их в SELFVOY_* (поддержка старых имён будет удалена в следующем релизе)");
    }
    return true;
}

}  // namespace selfvoy
