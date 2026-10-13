// aura/config.h — конфигурация сервера из переменных окружения.
#pragma once

#include <string>
#include <vector>

namespace aura {

struct Config {
    std::string host = "0.0.0.0";              // AURA_HOST
    int port = 9000;                           // AURA_PORT
    std::string databaseUrl;                   // AURA_DATABASE_URL (postgres://...)
    std::string embeddedDbPath = ".aura-data.json";  // AURA_EMBEDDED_DB (режим без PostgreSQL)

    std::string aiServiceUrl = "http://127.0.0.1:8000";  // AURA_AI_URL
    std::string aiServiceToken;                          // AURA_AI_TOKEN
    int aiTimeoutMs = 15000;                             // AURA_AI_TIMEOUT_MS

    // Режим разработки (AURA_DEV_MODE=1): разрешает драйвер почты dev (коды в
    // ответах API) и слабый/пустой AURA_JWT_SECRET (тогда генерируется случайный
    // секрет на время жизни процесса). Вне dev-режима сервер с такими настройками
    // не стартует — см. Config::validate().
    bool devMode = false;                                // AURA_DEV_MODE

    std::string jwtSecret;                               // AURA_JWT_SECRET (>= 32 байт; обязателен вне dev)
    std::string twoFactorKey;                            // AURA_2FA_KEY (ключ AES-256-GCM для TOTP-секретов; обязателен вне dev)
    long long tokenTtlSeconds = 60 * 60 * 24 * 7;        // AURA_TOKEN_TTL
    long long refreshTtlSeconds = 60 * 60 * 24 * 30;     // AURA_REFRESH_TTL
    long long verifyTtlSeconds = 60 * 60 * 24;           // AURA_VERIFY_TTL (код подтверждения email)
    long long resetTtlSeconds = 60 * 60;                 // AURA_RESET_TTL (код сброса пароля, не больше 1 ч)
    // dev — код в ответе API (только при AURA_DEV_MODE=1); log — в лог пишется
    // лишь факт выдачи кода (без самого кода); smtp — настоящие письма.
    std::string mailDriver = "log";                      // AURA_MAIL_DRIVER: dev|log|smtp
    // SMTP (AURA_MAIL_DRIVER=smtp): коды подтверждения и сброса уходят письмом.
    std::string smtpHost;                                // AURA_SMTP_HOST
    int smtpPort = 587;                                  // AURA_SMTP_PORT
    std::string smtpUser;                                // AURA_SMTP_USER
    std::string smtpPassword;                            // AURA_SMTP_PASSWORD
    std::string smtpFrom;                                // AURA_SMTP_FROM («Aura <no-reply@домен>»)
    std::string smtpTls = "starttls";                    // AURA_SMTP_TLS: starttls|tls|none
    std::string smtpCaFile;                              // AURA_SMTP_CA_FILE (свой корневой сертификат)
    int smtpTimeoutMs = 15000;                           // AURA_SMTP_TIMEOUT_MS
    std::string toolsMode = "sandbox";                   // AURA_TOOLS_MODE: sandbox|http
    std::string emailApiUrl;                             // AURA_EMAIL_API_URL
    std::string mapsApiUrl;                              // AURA_MAPS_API_URL

    std::size_t maxFrameBytes = 1024 * 1024;             // AURA_MAX_FRAME
    std::size_t maxPreAuthFrameBytes = 64 * 1024;        // AURA_MAX_PREAUTH_FRAME (кадр до входа)
    std::size_t maxConnections = 1024;                   // AURA_MAX_CONNECTIONS
    std::size_t maxConnectionsPerIp = 20;                // AURA_MAX_CONNECTIONS_PER_IP (0 = без лимита)
    int maxLoginAttempts = 8;                            // AURA_MAX_LOGIN_ATTEMPTS (на email)
    int maxIpAttempts = 30;                              // AURA_MAX_IP_ATTEMPTS (неудачи с одного IP, 0 = без лимита)
    int loginWindowSec = 300;                            // AURA_LOGIN_WINDOW
    int maxTwoFactorAttempts = 5;                        // AURA_MAX_2FA_ATTEMPTS (неверные TOTP на аккаунт)
    int twoFactorWindowSec = 900;                        // AURA_2FA_WINDOW
    int maxCodeAttempts = 5;                             // AURA_MAX_CODE_ATTEMPTS (неверные коды email/сброса → код сгорает)
    int mailPerEmailPerHour = 3;                         // AURA_MAIL_PER_EMAIL_HOUR (resendCode/forgotPassword на адрес)
    int mailPerIpPerHour = 20;                           // AURA_MAIL_PER_IP_HOUR
    int registerPerIpPerHour = 10;                       // AURA_REGISTER_PER_IP_HOUR (0 = без лимита)
    int pingIntervalSec = 25;                            // AURA_PING_INTERVAL
    int socketTimeoutSec = 120;                          // AURA_SOCKET_TIMEOUT
    int authTimeoutSec = 30;                             // AURA_AUTH_TIMEOUT (неавторизованное соединение закрывается)
    int authRecheckSec = 15;                             // AURA_AUTH_RECHECK (перепроверка сессии открытого соединения)
    int schedulerIntervalMs = 30000;                     // AURA_SCHEDULER_INTERVAL_MS (0 = выключен)

    // Интеграции (этап 9): Google OAuth 2.0 + PKCE. URL по умолчанию —
    // реальные эндпоинты Google (прод идёт за TLS-прокси); e2e подменяет
    // их локальным mock-сервером.
    std::string googleClientId;                          // AURA_GOOGLE_CLIENT_ID
    std::string googleClientSecret;                      // AURA_GOOGLE_CLIENT_SECRET
    std::string googleAuthUrl = "https://accounts.google.com/o/oauth2/v2/auth";  // AURA_GOOGLE_AUTH_URL
    std::string googleTokenUrl = "https://oauth2.googleapis.com/token";          // AURA_GOOGLE_TOKEN_URL
    std::string googleRevokeUrl = "https://oauth2.googleapis.com/revoke";        // AURA_GOOGLE_REVOKE_URL
    std::string googleCalendarUrl = "https://www.googleapis.com/calendar/v3";    // AURA_GOOGLE_CALENDAR_URL
    std::string googleGmailUrl = "https://gmail.googleapis.com/gmail/v1";        // AURA_GOOGLE_GMAIL_URL
    int oauthStateTtlSec = 600;                          // AURA_OAUTH_STATE_TTL — жизнь одноразового state

    // Push-уведомления (этап 13). Драйверы:
    //   dev     — писать payload в лог (разработка/e2e без шлюза);
    //   webhook — POST на AURA_PUSH_WEBHOOK_URL (push-шлюз доставляет в APNs;
    //             сам шлюз терминирует TLS и хранит ключ APNs).
    // Реальный APNs требует HTTP/2+TLS и JWT ES256, которых нет в net.h/crypto
    // намеренно — поэтому доставка только через внешний шлюз (честно, без имитации).
    std::string pushDriver = "dev";                      // AURA_PUSH_DRIVER: dev|webhook
    std::string pushWebhookUrl;                          // AURA_PUSH_WEBHOOK_URL
    std::string apnsTopic = "ai.aura.app";               // AURA_APNS_TOPIC (bundle id iOS)
    std::string apnsUrl = "https://api.push.apple.com";  // AURA_APNS_URL (для шлюза)
    // FCM (Android, этап 11): та же схема — сервер не хранит ключи Google и не
    // терминирует TLS; шлюз подписывает OAuth2-токен сервисного аккаунта и
    // форвардит тело fcm.message в FCM HTTP v1 API.
    std::string fcmUrl = "https://fcm.googleapis.com/v1/projects/aura/messages:send";  // AURA_FCM_URL
    std::string logLevel = "info";                       // AURA_LOG_LEVEL
    // Админ-панель: email будущих администраторов через запятую/пробел.
    // Роль admin выдаётся только подтверждённому адресу (при старте и при входе).
    std::string adminEmails;                             // AURA_ADMIN_EMAILS

    static Config fromEnv();
    std::string describe() const;  // для стартового лога (без секретов)

    // Проверка безопасности перед стартом. false + error (по-русски) — сервер
    // не должен запускаться. В dev-режиме может сгенерировать случайный
    // jwtSecret (warnings получает предупреждения для лога).
    bool validate(std::string& error, std::vector<std::string>& warnings);

    // true — секрет JWT слабый: пустой, короче 32 байт, известный dev-секрет
    // или заглушка из шаблона (CHANGE-IN-PRODUCTION).
    static bool weakSecret(const std::string& secret);
};

std::string envOr(const char* name, const std::string& fallback);

}  // namespace aura
