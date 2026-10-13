// aura/config.cpp
#include "aura/config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

#include "aura/crypto.h"

namespace aura {

namespace {
int intOr(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}
bool flagOn(const char* name) {
    const char* value = std::getenv(name);
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

std::string envOr(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    return value;
}

Config Config::fromEnv() {
    Config config;
    config.host = envOr("AURA_HOST", config.host);
    config.port = intOr("AURA_PORT", config.port);
    config.databaseUrl = envOr("AURA_DATABASE_URL", "");
    config.embeddedDbPath = envOr("AURA_EMBEDDED_DB", config.embeddedDbPath);
    config.aiServiceUrl = envOr("AURA_AI_URL", config.aiServiceUrl);
    config.aiServiceToken = envOr("AURA_AI_TOKEN", "");
    config.aiTimeoutMs = intOr("AURA_AI_TIMEOUT_MS", config.aiTimeoutMs);
    config.devMode = flagOn("AURA_DEV_MODE");
    config.jwtSecret = envOr("AURA_JWT_SECRET", config.jwtSecret);
    config.twoFactorKey = envOr("AURA_2FA_KEY", config.twoFactorKey);
    config.tokenTtlSeconds = intOr("AURA_TOKEN_TTL", static_cast<int>(config.tokenTtlSeconds));
    config.refreshTtlSeconds = intOr("AURA_REFRESH_TTL", static_cast<int>(config.refreshTtlSeconds));
    config.verifyTtlSeconds = intOr("AURA_VERIFY_TTL", static_cast<int>(config.verifyTtlSeconds));
    config.resetTtlSeconds = intOr("AURA_RESET_TTL", static_cast<int>(config.resetTtlSeconds));
    config.mailDriver = envOr("AURA_MAIL_DRIVER", config.mailDriver);
    config.smtpHost = envOr("AURA_SMTP_HOST", "");
    config.smtpPort = intOr("AURA_SMTP_PORT", config.smtpPort);
    config.smtpUser = envOr("AURA_SMTP_USER", "");
    config.smtpPassword = envOr("AURA_SMTP_PASSWORD", "");
    config.smtpFrom = envOr("AURA_SMTP_FROM", "");
    config.smtpTls = envOr("AURA_SMTP_TLS", config.smtpTls);
    config.smtpCaFile = envOr("AURA_SMTP_CA_FILE", "");
    config.smtpTimeoutMs = intOr("AURA_SMTP_TIMEOUT_MS", config.smtpTimeoutMs);
    config.toolsMode = envOr("AURA_TOOLS_MODE", config.toolsMode);
    config.emailApiUrl = envOr("AURA_EMAIL_API_URL", "");
    config.mapsApiUrl = envOr("AURA_MAPS_API_URL", "");
    config.maxFrameBytes = static_cast<std::size_t>(intOr("AURA_MAX_FRAME", static_cast<int>(config.maxFrameBytes)));
    config.maxPreAuthFrameBytes = static_cast<std::size_t>(
        std::max(1024, intOr("AURA_MAX_PREAUTH_FRAME", static_cast<int>(config.maxPreAuthFrameBytes))));
    config.maxConnections =
        static_cast<std::size_t>(intOr("AURA_MAX_CONNECTIONS", static_cast<int>(config.maxConnections)));
    config.maxConnectionsPerIp = static_cast<std::size_t>(
        std::max(0, intOr("AURA_MAX_CONNECTIONS_PER_IP", static_cast<int>(config.maxConnectionsPerIp))));
    config.maxLoginAttempts = intOr("AURA_MAX_LOGIN_ATTEMPTS", config.maxLoginAttempts);
    config.maxIpAttempts = intOr("AURA_MAX_IP_ATTEMPTS", config.maxIpAttempts);
    config.loginWindowSec = intOr("AURA_LOGIN_WINDOW", config.loginWindowSec);
    config.maxTwoFactorAttempts = intOr("AURA_MAX_2FA_ATTEMPTS", config.maxTwoFactorAttempts);
    config.twoFactorWindowSec = intOr("AURA_2FA_WINDOW", config.twoFactorWindowSec);
    config.maxCodeAttempts = intOr("AURA_MAX_CODE_ATTEMPTS", config.maxCodeAttempts);
    config.mailPerEmailPerHour = intOr("AURA_MAIL_PER_EMAIL_HOUR", config.mailPerEmailPerHour);
    config.mailPerIpPerHour = intOr("AURA_MAIL_PER_IP_HOUR", config.mailPerIpPerHour);
    config.registerPerIpPerHour = intOr("AURA_REGISTER_PER_IP_HOUR", config.registerPerIpPerHour);
    config.pingIntervalSec = intOr("AURA_PING_INTERVAL", config.pingIntervalSec);
    config.socketTimeoutSec = intOr("AURA_SOCKET_TIMEOUT", config.socketTimeoutSec);
    config.authTimeoutSec = intOr("AURA_AUTH_TIMEOUT", config.authTimeoutSec);
    config.authRecheckSec = intOr("AURA_AUTH_RECHECK", config.authRecheckSec);
    config.schedulerIntervalMs = intOr("AURA_SCHEDULER_INTERVAL_MS", config.schedulerIntervalMs);
    config.googleClientId = envOr("AURA_GOOGLE_CLIENT_ID", config.googleClientId);
    config.googleClientSecret = envOr("AURA_GOOGLE_CLIENT_SECRET", config.googleClientSecret);
    config.googleAuthUrl = envOr("AURA_GOOGLE_AUTH_URL", config.googleAuthUrl);
    config.googleTokenUrl = envOr("AURA_GOOGLE_TOKEN_URL", config.googleTokenUrl);
    config.googleRevokeUrl = envOr("AURA_GOOGLE_REVOKE_URL", config.googleRevokeUrl);
    config.googleCalendarUrl = envOr("AURA_GOOGLE_CALENDAR_URL", config.googleCalendarUrl);
    config.googleGmailUrl = envOr("AURA_GOOGLE_GMAIL_URL", config.googleGmailUrl);
    config.oauthStateTtlSec = intOr("AURA_OAUTH_STATE_TTL", config.oauthStateTtlSec);
    config.pushDriver = envOr("AURA_PUSH_DRIVER", config.pushDriver);
    config.pushWebhookUrl = envOr("AURA_PUSH_WEBHOOK_URL", config.pushWebhookUrl);
    config.apnsTopic = envOr("AURA_APNS_TOPIC", config.apnsTopic);
    config.apnsUrl = envOr("AURA_APNS_URL", config.apnsUrl);
    config.fcmUrl = envOr("AURA_FCM_URL", config.fcmUrl);
    config.logLevel = envOr("AURA_LOG_LEVEL", config.logLevel);
    config.adminEmails = envOr("AURA_ADMIN_EMAILS", "");
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
    if (secret == "aura-dev-secret" || secret == "test-secret") return true;
    const std::string up = upper(secret);
    return up.find("CHANGE-IN-PRODUCTION") != std::string::npos ||
           up.find("CHANGE_IN_PRODUCTION") != std::string::npos ||
           up.find("CHANGE-ME") != std::string::npos || up.find("CHANGEME") != std::string::npos;
}

bool Config::validate(std::string& error, std::vector<std::string>& warnings) {
    if (mailDriver != "dev" && mailDriver != "log" && mailDriver != "smtp") {
        error = "AURA_MAIL_DRIVER: допустимы dev, log или smtp";
        return false;
    }
    if (mailDriver == "dev" && !devMode) {
        error = "AURA_MAIL_DRIVER=dev отдаёт коды подтверждения и сброса пароля прямо в ответах API "
                "(любой может сбросить чужой пароль) — разрешён только вместе с AURA_DEV_MODE=1. "
                "В продакшене используйте AURA_MAIL_DRIVER=smtp (или log для стенда без почты)";
        return false;
    }

    if (weakSecret(jwtSecret)) {
        if (!devMode) {
            error = jwtSecret.empty()
                        ? "AURA_JWT_SECRET не задан: без секрета любой может подделать токен. "
                          "Задайте случайную строку не короче 32 байт (например, openssl rand -hex 32) "
                          "или включите AURA_DEV_MODE=1 для локальной разработки"
                        : "AURA_JWT_SECRET слабый (короче 32 байт, dev-значение или заглушка "
                          "CHANGE-IN-PRODUCTION): задайте случайную строку не короче 32 байт "
                          "(openssl rand -hex 32)";
            return false;
        }
        if (jwtSecret.empty()) {
            jwtSecret = crypto::randomHex(32);
            warnings.push_back("AURA_DEV_MODE: AURA_JWT_SECRET не задан — сгенерирован случайный секрет "
                               "на время жизни процесса (токены не переживут перезапуск)");
        } else {
            warnings.push_back("AURA_DEV_MODE: слабый AURA_JWT_SECRET — допустимо только для разработки");
        }
    }

    if (twoFactorKey.empty() || upper(twoFactorKey).find("CHANGE-IN-PRODUCTION") != std::string::npos) {
        if (!devMode) {
            error = "AURA_2FA_KEY не задан (или оставлена заглушка): ключ шифрования TOTP-секретов "
                    "обязателен. Задайте случайную строку (openssl rand -hex 32)";
            return false;
        }
        warnings.push_back("AURA_DEV_MODE: AURA_2FA_KEY не задан — TOTP-секреты шифруются ключом, "
                           "выведенным из AURA_JWT_SECRET");
    }

    if (resetTtlSeconds <= 0 || resetTtlSeconds > 3600) {
        warnings.push_back("AURA_RESET_TTL ограничен 1 часом (шестизначный код сброса живёт недолго)");
        resetTtlSeconds = 3600;
    }
    if (devMode) {
        warnings.push_back("AURA_DEV_MODE=1: режим разработки, не используйте его в продакшене");
    }
    return true;
}

}  // namespace aura
