// aura/mailprovider.h — отправка писем (подтверждение email, сброс пароля).
//
// Драйверы:
//   dev — пишет письмо в лог И возвращает код в ответе API (только локальная
//         разработка; AURA_MAIL_DRIVER=dev, значение по умолчанию);
//   log — пишет письмо в лог, код наружу не отдаёт (стенд без SMTP);
//   smtp — настоящая отправка по SMTP (AURA_SMTP_HOST/PORT/USER/PASSWORD/FROM/TLS,
//          см. aura/smtp.h): письмо с кодом подтверждения или сброса, код наружу
//          не отдаётся.
#pragma once

#include <memory>
#include <string>

namespace aura {

struct Config;

class MailProvider {
public:
    virtual ~MailProvider() = default;

    // true — драйвер позволяет вернуть код в API-ответе (режим dev).
    virtual bool exposesCodes() const = 0;

    virtual bool sendVerificationCode(const std::string& to, const std::string& code) = 0;
    virtual bool sendPasswordReset(const std::string& to, const std::string& code) = 0;
};

// Фабрика по имени драйвера из Config::mailDriver.
std::unique_ptr<MailProvider> makeMailProvider(const Config& config);

}  // namespace aura
