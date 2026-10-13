// aura/mailprovider.cpp — драйверы отправки писем.
#include "aura/mailprovider.h"

#include <algorithm>

#include "aura/config.h"
#include "aura/log.h"
#include "aura/smtp.h"

namespace aura {

namespace {

// Логирует письмо целиком: локальная разработка без SMTP.
class LogMailProvider : public MailProvider {
public:
    explicit LogMailProvider(bool expose) : expose_(expose) {}

    bool exposesCodes() const override { return expose_; }

    bool sendVerificationCode(const std::string& to, const std::string& code) override {
        AURA_LOG(log::Level::Info, "mail")
            << "КОД ПОДТВЕРЖДЕНИЯ для " << to << ": " << code
            << (expose_ ? "" : " (код также вернётся в ответе API — режим dev выключен)");
        return true;
    }

    bool sendPasswordReset(const std::string& to, const std::string& code) override {
        AURA_LOG(log::Level::Info, "mail") << "КОД СБРОСА ПАРОЛЯ для " << to << ": " << code;
        return true;
    }

private:
    bool expose_;
};

// «24 часа», «1 час», «30 минут» — срок действия кода в тексте письма.
std::string ttlText(long long seconds) {
    auto word = [](long long n, const char* one, const char* few, const char* many) {
        const long long m10 = n % 10, m100 = n % 100;
        if (m10 == 1 && m100 != 11) return one;
        if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return few;
        return many;
    };
    if (seconds >= 3600 && seconds % 3600 == 0) {
        const long long h = seconds / 3600;
        return std::to_string(h) + " " + word(h, "час", "часа", "часов");
    }
    const long long m = std::max(1LL, seconds / 60);
    return std::to_string(m) + " " + word(m, "минуту", "минуты", "минут");
}

// HTML-письмо: тёмная карточка в стиле приложения, код крупно. Только статичный
// текст и код (цифры/hex) — пользовательские данные в HTML не попадают.
std::string htmlLetter(const std::string& title, const std::string& lead, const std::string& code,
                       const std::string& note) {
    return "<!doctype html><html lang=\"ru\"><body style=\"margin:0;background:#0b0c10;padding:32px 16px;"
           "font-family:-apple-system,Segoe UI,Inter,Roboto,sans-serif;color:#e8e9ee\">"
           "<div style=\"max-width:440px;margin:0 auto;background:#12141c;border:1px solid rgba(255,255,255,.08);"
           "border-radius:16px;padding:28px\">"
           "<div style=\"font-weight:700;font-size:18px;margin-bottom:20px\">Aura</div>"
           "<h1 style=\"font-size:20px;margin:0 0 8px\">" + title + "</h1>"
           "<p style=\"color:#a1a4ae;line-height:1.5;margin:0 0 20px\">" + lead + "</p>"
           "<div style=\"font-family:SFMono-Regular,Consolas,monospace;font-size:28px;letter-spacing:4px;"
           "background:#0b0c10;border-radius:12px;padding:16px;text-align:center;word-break:break-all\">" + code +
           "</div><p style=\"color:#6c717e;font-size:13px;line-height:1.5;margin:20px 0 0\">" + note + "</p>"
           "</div></body></html>";
}

// Настоящая отправка по SMTP.
class SmtpMailProvider : public MailProvider {
public:
    explicit SmtpMailProvider(const Config& config) : verifyTtl_(config.verifyTtlSeconds), resetTtl_(config.resetTtlSeconds) {
        settings_.host = config.smtpHost;
        settings_.port = config.smtpPort;
        settings_.user = config.smtpUser;
        settings_.password = config.smtpPassword;
        settings_.from = config.smtpFrom;
        settings_.tls = config.smtpTls;
        settings_.caFile = config.smtpCaFile;
        settings_.timeoutMs = config.smtpTimeoutMs;
    }

    bool exposesCodes() const override { return false; }

    bool sendVerificationCode(const std::string& to, const std::string& code) override {
        smtp::Message message;
        message.to = to;
        message.subject = "Код подтверждения Aura: " + code;
        const std::string ttl = ttlText(verifyTtl_);
        message.text = "Здравствуйте!\r\n\r\nВаш код подтверждения email в Aura: " + code +
                       "\r\n\r\nКод действует " + ttl +
                       ". Если вы не регистрировались в Aura, просто проигнорируйте это письмо.\r\n";
        message.html = htmlLetter("Подтвердите email", "Введите этот код в приложении Aura:", code,
                                  "Код действует " + ttl + ". Если вы не регистрировались в Aura, просто проигнорируйте письмо.");
        return deliver(message, "подтверждение");
    }

    bool sendPasswordReset(const std::string& to, const std::string& code) override {
        smtp::Message message;
        message.to = to;
        message.subject = "Сброс пароля Aura";
        const std::string ttl = ttlText(resetTtl_);
        message.text = "Здравствуйте!\r\n\r\nКто-то запросил сброс пароля для вашего аккаунта Aura.\r\nКод сброса: " +
                       code + "\r\n\r\nКод действует " + ttl +
                       ". Если это были не вы — ничего не делайте, пароль останется прежним.\r\n";
        message.html = htmlLetter("Сброс пароля", "Введите этот код в приложении Aura, чтобы задать новый пароль:", code,
                                  "Код действует " + ttl + ". Если это были не вы — ничего не делайте, пароль останется прежним.");
        return deliver(message, "сброс пароля");
    }

private:
    bool deliver(const smtp::Message& message, const char* kind) {
        std::string error;
        if (smtp::send(settings_, message, error)) {
            AURA_LOG(log::Level::Info, "mail") << "письмо (" << kind << ") отправлено " << message.to;
            return true;
        }
        AURA_LOG(log::Level::Error, "mail") << "письмо (" << kind << ") не отправлено " << message.to << ": " << error;
        return false;
    }

    smtp::Settings settings_;
    long long verifyTtl_;
    long long resetTtl_;
};

}  // namespace

std::unique_ptr<MailProvider> makeMailProvider(const Config& config) {
    if (config.mailDriver == "smtp") return std::make_unique<SmtpMailProvider>(config);
    if (config.mailDriver == "dev") return std::make_unique<LogMailProvider>(true);
    // "log" и неизвестные значения — безопасный режим без раскрытия кодов.
    return std::make_unique<LogMailProvider>(false);
}

}  // namespace aura
