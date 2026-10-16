// selfvoy/server/tests/test_main.cpp — юнит-тесты сервера без внешних зависимостей.
//
// Запуск: make test  (или ./selfvoy-tests)
//
// Проверяются: JSON, криптография (эталонные векторы), JWT, кодек WebSocket,
// протокол и сквозные сценарии через Server::handleMessage на встроенной БД.
#include <argon2.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <chrono>
#include <csignal>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <atomic>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "selfvoy/authmanager.h"
#include "selfvoy/calendarfeed.h"
#include "selfvoy/crypto.h"
#include "selfvoy/databasemanager.h"
#include "selfvoy/idatabase.h"
#include "selfvoy/invitemanager.h"
#include "selfvoy/mailprovider.h"
#include "selfvoy/json.h"
#include "selfvoy/jwt.h"
#include "selfvoy/mailprovider.h"
#include "selfvoy/memorymanager.h"
#include "selfvoy/net.h"
#include "selfvoy/protocol.h"
#include "selfvoy/schedulelock.h"
#include "selfvoy/server.h"
#include "selfvoy/slotspost.h"
#include "selfvoy/plan.h"
#include "selfvoy/smtp.h"
#include "selfvoy/telegrambot.h"
#include "selfvoy/tls.h"
#include "selfvoy/toolmanager.h"
#include "selfvoy/totp.h"
#include "selfvoy/ws.h"
#include "../src/notificationsmanager.h"  // уведомления владельцу → Telegram (рост, S1a)

#ifdef SELFVOY_WITH_LIBPQ
#include <libpq-fe.h>
#endif

#ifdef SELFVOY_HAVE_OPENSSL
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

namespace {

int g_failures = 0;
int g_checks = 0;

struct TestCase {
    std::string name;
    std::function<void()> body;
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(std::string name, std::function<void()> body) {
        registry().push_back({std::move(name), std::move(body)});
    }
};

#define TEST(name)                                             \
    void name();                                               \
    static Registrar registrar_##name(#name, name);            \
    void name()

#define CHECK(condition)                                                            \
    do {                                                                            \
        ++g_checks;                                                                 \
        if (!(condition)) {                                                         \
            ++g_failures;                                                           \
            std::cout << "  FAIL " << __FILE__ << ':' << __LINE__ << " " << #condition \
                      << "\n";                                                      \
        }                                                                           \
    } while (0)

#define CHECK_EQ(actual, expected)                                                           \
    do {                                                                                     \
        ++g_checks;                                                                          \
        const auto actualValue = (actual);                                                   \
        const auto expectedValue = (expected);                                               \
        if (!(actualValue == expectedValue)) {                                               \
            ++g_failures;                                                                    \
            std::ostringstream message;                                                      \
            message << actualValue;                                                          \
            std::ostringstream other;                                                        \
            other << expectedValue;                                                          \
            std::cout << "  FAIL " << __FILE__ << ':' << __LINE__ << " " << #actual          \
                      << " == " << #expected << " (получено '" << message.str()              \
                      << "', ожидалось '" << other.str() << "')\n";                          \
        }                                                                                    \
    } while (0)

using selfvoy::Json;

// --------------------------------------------------------------------- JSON
TEST(json_parse_and_dump) {
    std::string error;
    const Json parsed = Json::parse(
        R"({"name":"Анна","age":31,"tags":["a","b"],"nested":{"ok":true,"value":null},"ratio":0.5})", &error);
    CHECK(error.empty());
    CHECK(parsed.isObject());
    CHECK_EQ(parsed.getString("name"), std::string("Анна"));
    CHECK_EQ(parsed.getInt("age"), 31);
    CHECK_EQ(parsed.get("tags").size(), static_cast<std::size_t>(2));
    CHECK(parsed.get("nested").getBool("ok"));
    CHECK(parsed.get("nested").get("value").isNull());
    CHECK_EQ(parsed.getDouble("ratio"), 0.5);

    // Порядок ключей сохраняется
    const std::vector<std::string> keys = parsed.keys();
    CHECK_EQ(keys.front(), std::string("name"));
    CHECK_EQ(keys.back(), std::string("ratio"));

    // Round-trip
    const Json again = Json::parse(parsed.dump());
    CHECK_EQ(again.getString("name"), std::string("Анна"));
}

TEST(json_escapes_and_unicode) {
    std::string error;
    const Json parsed = Json::parse(R"({"text":"line1\nline2 \"quoted\" \u0410\u0411"})", &error);
    CHECK(error.empty());
    CHECK_EQ(parsed.getString("text"), std::string("line1\nline2 \"quoted\" АБ"));

    Json out = Json::object();
    out.set("text", Json("кавычки \" и \\ слэш"));
    const std::string dumped = out.dump();
    CHECK(dumped.find("\\\"") != std::string::npos);
    CHECK(dumped.find("\\\\") != std::string::npos);
    CHECK_EQ(Json::parse(dumped).getString("text"), std::string("кавычки \" и \\ слэш"));
}

TEST(json_errors_and_defaults) {
    std::string error;
    CHECK(Json::parse("{bad}", &error).isNull());
    CHECK(!error.empty());
    error.clear();
    CHECK(Json::parse("[1,2", &error).isNull());

    const Json empty = Json::object();
    CHECK_EQ(empty.getString("missing", "def"), std::string("def"));
    CHECK_EQ(empty.getInt("missing", 7), 7);
    CHECK(!empty.contains("missing"));
}

// ------------------------------------------------------------------ crypto
TEST(crypto_known_vectors) {
    CHECK_EQ(selfvoy::crypto::toHex(selfvoy::crypto::sha256("abc")),
             std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(selfvoy::crypto::toHex(selfvoy::crypto::sha256("")),
             std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(selfvoy::crypto::toHex(selfvoy::crypto::sha1("abc")),
             std::string("a9993e364706816aba3e25717850c26c9cd0d89d"));

    // RFC 4231, тест-кейс 1
    const std::string key(20, '\x0b');
    CHECK_EQ(selfvoy::crypto::toHex(selfvoy::crypto::hmacSha256(key, "Hi There")),
             std::string("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));

    // RFC 6070-подобный вектор для PBKDF2-HMAC-SHA256: password/salt/1 итерация
    CHECK_EQ(selfvoy::crypto::toHex(selfvoy::crypto::pbkdf2Sha256("password", "salt", 1, 32)),
             std::string("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"));
}

TEST(crypto_base64) {
    CHECK_EQ(selfvoy::crypto::base64Encode("Man"), std::string("TWFu"));
    CHECK_EQ(selfvoy::crypto::base64Encode("Ma"), std::string("TWE="));
    CHECK_EQ(selfvoy::crypto::base64Encode("M"), std::string("TQ=="));
    // UTF-8 «привет» → 0L/RgNC40LLQtdGC (проверено python3 -c base64)
    CHECK_EQ(selfvoy::crypto::base64Encode("привет"), std::string("0L/RgNC40LLQtdGC"));
    std::string decoded;
    CHECK(selfvoy::crypto::base64Decode("0L/RgNC40LLQtdGC", decoded));
    CHECK_EQ(decoded, std::string("привет"));
    CHECK_EQ(selfvoy::crypto::base64UrlEncode(std::string("\xfb\xff", 2)).find('='), std::string::npos);
}

TEST(smtp_message_format) {
    selfvoy::smtp::Settings settings;
    settings.from = "Сервис Selfvoy <no-reply@selfvoy.example>";
    selfvoy::smtp::Message message;
    message.to = "anna@example.com";
    message.subject = "Код подтверждения Selfvoy: 123456";
    message.text = "Код: 123456";
    message.html = "<b>123456</b>";
    const std::string mail =
        selfvoy::smtp::buildMessage(settings, message, "Tue, 13 Oct 2026 12:00:00 +0000", "<id@selfvoy.example>");
    CHECK(mail.find("To: <anna@example.com>\r\n") != std::string::npos);
    CHECK(mail.find("Subject: =?UTF-8?B?") != std::string::npos);
    CHECK(mail.find("From: =?UTF-8?B?") != std::string::npos);
    CHECK(mail.find("<no-reply@selfvoy.example>") != std::string::npos);
    CHECK(mail.find("multipart/alternative") != std::string::npos);
    CHECK(mail.find("Message-ID: <id@selfvoy.example>") != std::string::npos);
    // Только CRLF, без одиночных LF, и тело в base64 (не-ASCII не уходит «как есть»).
    for (std::size_t i = 0; i < mail.size(); ++i) {
        if (mail[i] == '\n') CHECK(i > 0 && mail[i - 1] == '\r');
    }
    std::string decoded;
    CHECK(selfvoy::crypto::base64Decode(selfvoy::crypto::base64Encode("Код: 123456"), decoded));
    CHECK(mail.find(selfvoy::crypto::base64Encode("Код: 123456")) != std::string::npos);

    CHECK(!selfvoy::smtp::safeHeaderValue("x\r\nBcc: evil@example.com"));
    CHECK_EQ(selfvoy::smtp::addressOf("Selfvoy <no-reply@selfvoy.example>"), std::string("no-reply@selfvoy.example"));
    CHECK_EQ(selfvoy::smtp::addressOf(" a@b.c "), std::string("a@b.c"));
}

TEST(smtp_dialogue_with_fake_server) {
    // Поддельный SMTP-сервер на 127.0.0.1: проверяем порядок команд, AUTH и DATA.
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK_EQ(::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof addr), 0);
    CHECK_EQ(::listen(listener, 1), 0);
    socklen_t len = sizeof addr;
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len);
    const int port = ntohs(addr.sin_port);

    std::vector<std::string> commands;
    std::string data;
    std::thread fake([&] {
        const int fd = ::accept(listener, nullptr, nullptr);
        std::string buffer;
        auto line = [&]() {
            for (;;) {
                const auto eol = buffer.find("\r\n");
                if (eol != std::string::npos) {
                    std::string out = buffer.substr(0, eol);
                    buffer.erase(0, eol + 2);
                    return out;
                }
                char chunk[1024];
                const ssize_t n = ::recv(fd, chunk, sizeof chunk, 0);
                if (n <= 0) return std::string("<eof>");
                buffer.append(chunk, static_cast<std::size_t>(n));
            }
        };
        auto say = [&](const std::string& text) { ::send(fd, text.data(), text.size(), MSG_NOSIGNAL); };
        say("220 fake ESMTP\r\n");
        for (;;) {
            const std::string cmd = line();
            commands.push_back(cmd);
            if (cmd.rfind("EHLO", 0) == 0) say("250-fake\r\n250 AUTH LOGIN PLAIN\r\n");
            else if (cmd.rfind("AUTH PLAIN", 0) == 0) say("235 ok\r\n");
            else if (cmd.rfind("MAIL FROM", 0) == 0 || cmd.rfind("RCPT TO", 0) == 0) say("250 ok\r\n");
            else if (cmd == "DATA") {
                say("354 go\r\n");
                for (std::string l = line(); l != "." && l != "<eof>"; l = line()) data += l + "\n";
                say("250 queued\r\n");
            } else if (cmd == "QUIT") {
                say("221 bye\r\n");
                break;
            } else {
                say("500 ?\r\n");
                if (cmd == "<eof>") break;
            }
        }
        ::close(fd);
    });

    selfvoy::smtp::Settings settings;
    settings.host = "127.0.0.1";
    settings.port = port;
    settings.tls = "none";  // localhost: AUTH без TLS разрешён только для ретранслятора на этой машине
    settings.user = "mailer";
    settings.password = "secret-pass";
    settings.from = "Selfvoy <no-reply@selfvoy.example>";
    settings.timeoutMs = 5000;
    selfvoy::smtp::Message message;
    message.to = "anna@example.com";
    message.subject = "Код подтверждения Selfvoy: 654321";
    message.text = "Ваш код: 654321";
    std::string error;
    const bool sent = selfvoy::smtp::send(settings, message, error);
    fake.join();
    ::close(listener);

    CHECK(sent);
    CHECK_EQ(error, std::string());
    CHECK(commands.size() >= 6);
    CHECK_EQ(commands[0], std::string("EHLO selfvoy.example"));
    CHECK_EQ(commands[1], std::string("AUTH PLAIN ") +
                              selfvoy::crypto::base64Encode(std::string("\0mailer\0secret-pass", 19)));
    CHECK_EQ(commands[2], std::string("MAIL FROM:<no-reply@selfvoy.example>"));
    CHECK_EQ(commands[3], std::string("RCPT TO:<anna@example.com>"));
    CHECK(data.find(selfvoy::crypto::base64Encode("Ваш код: 654321")) != std::string::npos);

    // Подстановка заголовка в адресе отклоняется до соединения.
    message.to = "anna@example.com\r\nBcc: evil@example.com";
    CHECK(!selfvoy::smtp::send(settings, message, error));
    CHECK(error.find("недопустимый") != std::string::npos);
}

TEST(crypto_password_hash) {
    // Новый пароль хэшируется в Argon2id.
    const std::string hash = selfvoy::crypto::hashPassword("selfvoy1234");
    CHECK(hash.rfind("argon2id$v=19$m=", 0) == 0);
    CHECK(selfvoy::crypto::verifyPassword("selfvoy1234", hash));
    CHECK(!selfvoy::crypto::verifyPassword("selfvoy1235", hash));
    CHECK(!selfvoy::crypto::verifyPassword("selfvoy1234", "argon2id$v=19$m=65536,t=3,p=4$bad"));
    CHECK(!selfvoy::crypto::passwordNeedsRehash(hash));
    CHECK(selfvoy::crypto::constantTimeEquals("secret", "secret"));
    CHECK(!selfvoy::crypto::constantTimeEquals("secret", "secrets"));
}

TEST(crypto_argon2id_vector) {
    // Детерминированный Argon2id с фиксированной солью: пароль "password",
    // соль "somesalt" (8 байт), m=64 KiB, t=3, p=2. Ожидаемый 32-байтный тег
    // вычислен эталонной libargon2 и зафиксирован здесь как known-answer.
    const std::string password = "password";
    const std::string saltRaw = "somesalt";
    std::string tag(32, '\0');
    const int rc = argon2id_hash_raw(3, 64, 2, password.data(), password.size(),
                                     saltRaw.data(), saltRaw.size(), tag.data(), tag.size());
    CHECK_EQ(rc, ARGON2_OK);
    const std::string tagHex = selfvoy::crypto::toHex(tag);
    // Known-answer: тег сверен с независимой реализацией (argon2-cffi, Python)
    // для этих же параметров. Совпадение подтверждает корректность libargon2.
    CHECK_EQ(tagHex,
             std::string("c7904b6301d03676acbd4dd657486c42509ccc6d0113fce6495ef4b693392fbf"));
    // Тег детерминирован: повторный вызов даёт тот же результат.
    std::string tag2(32, '\0');
    argon2id_hash_raw(3, 64, 2, password.data(), password.size(),
                      saltRaw.data(), saltRaw.size(), tag2.data(), tag2.size());
    CHECK_EQ(tagHex, selfvoy::crypto::toHex(tag2));
    // Неверный пароль меняет тег.
    std::string tagBad(32, '\0');
    argon2id_hash_raw(3, 64, 2, "Password", 8, saltRaw.data(), saltRaw.size(),
                      tagBad.data(), tagBad.size());
    CHECK(tagHex != selfvoy::crypto::toHex(tagBad));

    // Тот же тег проверяется через argon2idVerify (собираем encoded-строку).
    const std::string encoded = "argon2id$v=19$m=64,t=3,p=2$" +
                                selfvoy::crypto::toHex(saltRaw) + "$" + tagHex;
    CHECK(selfvoy::crypto::argon2idVerify("password", encoded));
    CHECK(!selfvoy::crypto::argon2idVerify("password2", encoded));
}

TEST(crypto_password_migration) {
    // Старый PBKDF2-хэш остаётся проверяемым (прозрачная миграция на входе).
    const std::string salt = "aabbccddeeff0011";
    const std::string legacyHash = "pbkdf2$1000$" + salt + "$" +
        selfvoy::crypto::toHex(selfvoy::crypto::pbkdf2Sha256("selfvoy1234", salt, 1000, 32));
    CHECK(selfvoy::crypto::verifyPassword("selfvoy1234", legacyHash));
    CHECK(!selfvoy::crypto::verifyPassword("selfvoy1235", legacyHash));
    CHECK(selfvoy::crypto::passwordNeedsRehash(legacyHash));
}

namespace {
// Тестовый хелпер: hex → байты (вне класса crypto, только для тестов).
std::string unhex(const std::string& hex) {
    const auto nibble = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return 0;
    };
    std::string out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<char>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
    }
    return out;
}
}  // namespace

TEST(crypto_hmac_sha1_rfc2202) {
    // RFC 2202, case 1: ключ 20×0x0b, данные "Hi There".
    const std::string key(20, '\x0b');
    CHECK_EQ(selfvoy::crypto::toHex(selfvoy::crypto::hmacSha1(key, "Hi There")),
             std::string("b617318655057264e28bc0b6fb378c8ef146be00"));
    // RFC 2202, case 2.
    CHECK_EQ(selfvoy::crypto::toHex(selfvoy::crypto::hmacSha1("Jefe", "what do ya want for nothing?")),
             std::string("effcdf6ae5eb2fa2d27416d5f184df9c259a7c79"));
}

TEST(crypto_base32_rfc4648) {
    CHECK_EQ(selfvoy::crypto::base32Encode(""), std::string(""));
    CHECK_EQ(selfvoy::crypto::base32Encode("f"), std::string("MY======"));
    CHECK_EQ(selfvoy::crypto::base32Encode("fo"), std::string("MZXQ===="));
    CHECK_EQ(selfvoy::crypto::base32Encode("foo"), std::string("MZXW6==="));
    CHECK_EQ(selfvoy::crypto::base32Encode("foob"), std::string("MZXW6YQ="));
    CHECK_EQ(selfvoy::crypto::base32Encode("fooba"), std::string("MZXW6YTB"));
    CHECK_EQ(selfvoy::crypto::base32Encode("foobar"), std::string("MZXW6YTBOI======"));
}

TEST(crypto_aes256_gcm) {
    const std::string key = unhex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");

    // Known-answer: вектор вычислен независимой библиотекой
    // (Python cryptography/OpenSSL) для nonce=000102…0b, aad="aura-2fa-secret"
    // (исторический AAD TOTP-секретов, при переименовании в Selfvoy не меняется).
    const std::string blob =
        unhex("000102030405060708090a0b0d40854c9cd6864bc809c7c082b9203d8ce94805d58e969523c40d978a79c599");
    const std::string aad = "aura-2fa-secret";
    std::string plain;
    CHECK(selfvoy::crypto::aes256GcmDecrypt(key, blob, aad, plain));
    CHECK_EQ(plain, std::string("JBSWY3DPEHPK3PXP"));

    // Подмены отклоняются: другой AAD, другой ключ, испорченный байт.
    CHECK(!selfvoy::crypto::aes256GcmDecrypt(key, blob, "wrong-aad", plain));
    std::string badKey = key;
    badKey[0] = '\x01';
    CHECK(!selfvoy::crypto::aes256GcmDecrypt(badKey, blob, aad, plain));
    std::string tampered = blob;
    tampered[13] = static_cast<char>(tampered[13] ^ 0x01);
    CHECK(!selfvoy::crypto::aes256GcmDecrypt(key, tampered, aad, plain));

    // Roundtrip: шифрование → расшифровка, включая длинный текст.
    std::string blob2;
    CHECK(selfvoy::crypto::aes256GcmEncrypt(key, "some longer secret value 12345", aad, blob2));
    std::string back;
    CHECK(selfvoy::crypto::aes256GcmDecrypt(key, blob2, aad, back));
    CHECK_EQ(back, std::string("some longer secret value 12345"));

    // Второй known-answer: пустой AAD, короткий текст.
    const std::string blob3 = unhex("00000000000000000000000066d51ec39250ea9203b6e1e6789b7a0f3082");
    CHECK(selfvoy::crypto::aes256GcmDecrypt(key, blob3, "", back));
    CHECK_EQ(back, std::string("hi"));

    // Ключ неверной длины отклоняется.
    CHECK(!selfvoy::crypto::aes256GcmEncrypt("short", "x", "", blob2));
    CHECK(!selfvoy::crypto::aes256GcmDecrypt("short", blob, aad, plain));
}

TEST(totp_rfc6238_vectors) {
    // RFC 6238, приложение B (SHA-1, секрет — ASCII "12345678901234567890").
    // В таблице RFC коды 8-значные; Selfvoy использует 6 цифр — сверяем последние 6.
    const std::string secret = "12345678901234567890";
    CHECK_EQ(selfvoy::totp::codeAt(secret, 59), std::string("287082"));
    CHECK_EQ(selfvoy::totp::codeAt(secret, 1111111109), std::string("081804"));
    CHECK_EQ(selfvoy::totp::codeAt(secret, 1111111111), std::string("050471"));
    CHECK_EQ(selfvoy::totp::codeAt(secret, 1234567890), std::string("005924"));
    CHECK_EQ(selfvoy::totp::codeAt(secret, 2000000000), std::string("279037"));  // RFC: 69279037
    CHECK_EQ(selfvoy::totp::codeAt(secret, 20000000000), std::string("353130"));
    // 8-значный режим даёт ровно значения из таблицы RFC.
    CHECK_EQ(selfvoy::totp::codeAt(secret, 59, 8), std::string("94287082"));
    CHECK_EQ(selfvoy::totp::codeAt(secret, 2000000000, 8), std::string("69279037"));
    CHECK_EQ(selfvoy::totp::codeAt(secret, 20000000000, 8), std::string("65353130"));
}

TEST(totp_verify_window_and_reuse) {
    const std::string secretB32 = selfvoy::totp::generateSecretBase32();
    std::string raw;
    CHECK(selfvoy::totp::decodeSecretBase32(secretB32, raw));
    CHECK_EQ(raw.size(), std::size_t(20));

    const std::int64_t now = 900;  // счётчик 30
    const std::string currentCode = selfvoy::totp::codeAt(raw, now);
    std::int64_t matched = -1;
    CHECK(selfvoy::totp::verifyAt(raw, currentCode, now, 0, matched));
    CHECK_EQ(matched, std::int64_t(30));

    // Одноразовость: тот же код с lastUsedCounter=30 отклоняется.
    CHECK(!selfvoy::totp::verifyAt(raw, currentCode, now, 30, matched));

    // Окно ±1 период: коды соседних счётчиков принимаются.
    const std::string prev = selfvoy::totp::codeAt(raw, 29 * 30);
    CHECK(selfvoy::totp::verifyAt(raw, prev, now, 0, matched));
    CHECK_EQ(matched, std::int64_t(29));
    const std::string next = selfvoy::totp::codeAt(raw, 31 * 30);
    CHECK(selfvoy::totp::verifyAt(raw, next, now, 0, matched));
    CHECK_EQ(matched, std::int64_t(31));

    // Два периода назад — уже вне окна.
    const std::string old = selfvoy::totp::codeAt(raw, 28 * 30);
    CHECK(!selfvoy::totp::verifyAt(raw, old, now, 0, matched));

    // Неверный код, нецифровой ввод, короткая строка.
    std::string wrong = currentCode;
    wrong[0] = (wrong[0] == '9') ? '0' : static_cast<char>(wrong[0] + 1);
    CHECK(!selfvoy::totp::verifyAt(raw, wrong, now, 0, matched));
    CHECK(!selfvoy::totp::verifyAt(raw, "abcdef", now, 0, matched));
    CHECK(!selfvoy::totp::verifyAt(raw, "12345", now, 0, matched));
}

TEST(totp_secret_decode_and_uri) {
    // Канонический тестовый секрет: "JBSWY3DPEHPK3PXP" = "Hello!" + deadbeef.
    std::string raw;
    CHECK(selfvoy::totp::decodeSecretBase32("JBSWY3DPEHPK3PXP", raw));
    CHECK_EQ(selfvoy::crypto::toHex(raw), std::string("48656c6c6f21deadbeef"));
    // Строчные буквы, пробелы, дефисы — допустимы.
    CHECK(selfvoy::totp::decodeSecretBase32("jbsw y3dp-ehpk3pxp", raw));
    CHECK_EQ(selfvoy::crypto::toHex(raw), std::string("48656c6c6f21deadbeef"));
    // Недопустимый символ и слишком короткий секрет.
    CHECK(!selfvoy::totp::decodeSecretBase32("JBSWY3DPEHPK3PX!", raw));
    CHECK(!selfvoy::totp::decodeSecretBase32("MY======", raw));

    const std::string uri = selfvoy::totp::otpauthUri("JBSWY3DPEHPK3PXP", "anna@selfvoy.app", "Selfvoy");
    CHECK(uri.rfind("otpauth://totp/Selfvoy:anna%40selfvoy.app?", 0) == 0);
    CHECK(uri.find("secret=JBSWY3DPEHPK3PXP") != std::string::npos);
    CHECK(uri.find("issuer=Selfvoy") != std::string::npos);
    CHECK(uri.find("algorithm=SHA1") != std::string::npos);
    CHECK(uri.find("digits=6") != std::string::npos);
    CHECK(uri.find("period=30") != std::string::npos);
}

// --------------------------------------------------------------------- JWT
TEST(jwt_sign_and_verify) {
    Json claims = Json::object();
    claims.set("sub", Json(42));
    claims.set("email", Json("anna@example.com"));
    const std::string token = selfvoy::jwt::sign(claims, "test-secret", 60);
    CHECK_EQ(std::count(token.begin(), token.end(), '.'), 2);

    const selfvoy::jwt::Token verified = selfvoy::jwt::verify(token, "test-secret");
    CHECK(verified.valid);
    CHECK_EQ(verified.claims.getInt("sub"), 42);
    CHECK_EQ(verified.claims.getString("email"), std::string("anna@example.com"));

    CHECK(!selfvoy::jwt::verify(token, "other-secret").valid);
    const selfvoy::jwt::Token expired = selfvoy::jwt::verify(token, "test-secret", selfvoy::jwt::nowEpoch() + 3600);
    CHECK(!expired.valid);
    CHECK(!selfvoy::jwt::verify("not.a.token", "test-secret").valid);
}

// --------------------------------------------------------------- WebSocket
TEST(ws_accept_token_rfc6455) {
    // Пример из RFC 6455, раздел 1.3
    CHECK_EQ(selfvoy::ws::acceptToken("dGhlIHNhbXBsZSBub25jZQ=="),
             std::string("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
}

TEST(ws_handshake_parse) {
    const std::string request =
        "GET /ws HTTP/1.1\r\n"
        "Host: localhost:9000\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Authorization: Bearer abc.def.ghi\r\n"
        "Origin: http://localhost\r\n\r\n";
    const selfvoy::ws::Handshake handshake = selfvoy::ws::parseHandshake(request);
    CHECK(handshake.valid);
    CHECK_EQ(handshake.path, std::string("/ws"));
    CHECK_EQ(handshake.key, std::string("dGhlIHNhbXBsZSBub25jZQ=="));
    CHECK_EQ(handshake.authorization, std::string("Bearer abc.def.ghi"));
    CHECK_EQ(handshake.origin, std::string("http://localhost"));

    CHECK(!selfvoy::ws::parseHandshake("POST / HTTP/1.1\r\n\r\n").valid);
    CHECK(!selfvoy::ws::parseHandshake("GET / HTTP/1.1\r\nHost: x\r\n\r\n").valid);
    CHECK(selfvoy::ws::handshakeResponse("dGhlIHNhbXBsZSBub25jZQ==").find("101 Switching Protocols") !=
          std::string::npos);
}

TEST(ws_frame_roundtrip) {
    const std::string payload = R"({"type":"ping","payload":{"n":1}})";
    const std::string frame = selfvoy::ws::encodeFrame(selfvoy::ws::Opcode::Text, payload);
    CHECK_EQ(static_cast<int>(static_cast<unsigned char>(frame[0])), 0x81);

    // Имитируем клиентский кадр с маской
    std::string masked = frame;
    masked[1] = static_cast<char>(static_cast<unsigned char>(masked[1]) | 0x80);
    const std::size_t headerLength = 2;
    const char mask[4] = {0x12, 0x34, 0x56, 0x78};
    masked.insert(masked.begin() + static_cast<long>(headerLength), mask, mask + 4);
    for (std::size_t i = headerLength + 4; i < masked.size(); ++i) {
        masked[i] = static_cast<char>(static_cast<unsigned char>(masked[i]) ^ mask[(i - headerLength - 4) % 4]);
    }

    std::size_t consumed = 0;
    selfvoy::ws::Frame decoded;
    std::string error;
    const auto result = selfvoy::ws::decodeFrame(masked, 1024 * 1024, consumed, decoded, error);
    CHECK(result == selfvoy::ws::DecodeResult::Complete);
    CHECK_EQ(consumed, masked.size());
    CHECK_EQ(decoded.payload, payload);
    CHECK(decoded.opcode == selfvoy::ws::Opcode::Text);

    // Неполный кадр
    std::size_t consumedPartial = 0;
    selfvoy::ws::Frame partial;
    CHECK(selfvoy::ws::decodeFrame(masked.substr(0, 3), 1024, consumedPartial, partial, error) ==
          selfvoy::ws::DecodeResult::Incomplete);

    // Превышение лимита
    std::size_t consumedBig = 0;
    selfvoy::ws::Frame big;
    CHECK(selfvoy::ws::decodeFrame(masked, 10, consumedBig, big, error) ==
          selfvoy::ws::DecodeResult::ProtocolError);
}

// ---------------------------------------------------------------- protocol
TEST(protocol_envelope) {
    const Json okMessage = selfvoy::protocol::ok("req-1", [] {
        Json payload = Json::object();
        payload.set("value", Json(1));
        return payload;
    }());
    CHECK_EQ(okMessage.getString("type"), std::string("ok"));
    CHECK_EQ(okMessage.getString("id"), std::string("req-1"));
    CHECK_EQ(okMessage.get("payload").getInt("value"), 1);

    const Json errorMessage = selfvoy::protocol::error("req-2", selfvoy::protocol::code::kUnauthorized, "нужен вход");
    CHECK_EQ(errorMessage.getString("code"), std::string("unauthorized"));

    const Json eventMessage = selfvoy::protocol::event("chat.message");
    CHECK_EQ(eventMessage.getString("event"), std::string("chat.message"));

    selfvoy::protocol::Request request;
    std::string error;
    CHECK(!selfvoy::protocol::parse(Json::array(), request, error));
    CHECK(selfvoy::protocol::isPublicType("auth.login"));
    CHECK(!selfvoy::protocol::isPublicType("chat.send"));
}

// --------------------------------------------------------------------- net
TEST(net_url_parsing) {
    selfvoy::net::Url url;
    std::string error;
    CHECK(selfvoy::net::parseUrl("http://127.0.0.1:8000/v1/agent/run", url, error));
    CHECK_EQ(url.host, std::string("127.0.0.1"));
    CHECK_EQ(url.port, 8000);
    CHECK_EQ(url.path, std::string("/v1/agent/run"));

    CHECK(selfvoy::net::parseUrl("http://ai-service/healthz", url, error));
    CHECK_EQ(url.port, 80);
    CHECK_EQ(url.path, std::string("/healthz"));

    // https — порт 443 по умолчанию (TLS — selfvoy/tls.h); прочие схемы и порты вне 1–65535 — нет.
    CHECK(selfvoy::net::parseUrl("https://api.telegram.org/bot1:x/getMe", url, error));
    CHECK_EQ(url.scheme, std::string("https"));
    CHECK_EQ(url.port, 443);
    CHECK_EQ(url.host, std::string("api.telegram.org"));
    CHECK_EQ(url.path, std::string("/bot1:x/getMe"));
    CHECK(selfvoy::net::parseUrl("HTTPS://example.com:8443", url, error));
    CHECK_EQ(url.port, 8443);
    CHECK_EQ(url.path, std::string("/"));
    error.clear();
    CHECK(!selfvoy::net::parseUrl("ftp://example.com", url, error));
    CHECK(!error.empty());
    CHECK(!selfvoy::net::parseUrl("https://example.com:70000/", url, error));

    // Разбор chunked-ответа
    const std::string chunked = "5\r\nHello\r\n6\r\n World\r\n0\r\n\r\n";
    selfvoy::net::HttpResponse response;
    CHECK(selfvoy::net::parseResponseHead("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n", response));
    CHECK_EQ(response.status, 200);
    CHECK_EQ(response.header("transfer-encoding"), std::string("chunked"));
    CHECK_EQ(selfvoy::net::decodeChunked(chunked), std::string("Hello World"));
}

// -------------------------------------------------------------------- auth
TEST(auth_validation_rules) {
    CHECK_EQ(selfvoy::AuthManager::normalizeEmail("  Anna@Example.COM "), std::string("anna@example.com"));
    CHECK(!selfvoy::AuthManager::passwordError("short1").empty());
    CHECK(!selfvoy::AuthManager::passwordError("onlyletters").empty());
    CHECK(!selfvoy::AuthManager::passwordError("12345678").empty());
    CHECK(selfvoy::AuthManager::passwordError("selfvoy1234").empty());
}

// ------------------------------------------------------------- интеграция
namespace integration {

struct Fixture {
    selfvoy::Config config;
    std::string dbPath;
    std::unique_ptr<selfvoy::Server> server;
    int observerFd = -1;

    Fixture() {
        dbPath = "/tmp/selfvoy-test-" + std::to_string(::getpid()) + ".json";
        std::remove(dbPath.c_str());
        config = selfvoy::Config::fromEnv();
        config.databaseUrl.clear();  // встроенное хранилище
        config.embeddedDbPath = dbPath;
        config.port = 0;             // свободный порт
        config.jwtSecret = "test-secret";
        config.devMode = true;        // слабый секрет и mail=dev допустимы только в dev-режиме
        config.mailDriver = "dev";    // коды подтверждения возвращаются в ответе API
        config.logLevel = "warn";
        config.aiServiceUrl = "http://127.0.0.1:1";  // гарантированно недоступен
        config.aiTimeoutMs = 500;
        config.schedulerIntervalMs = 0;  // фоновый поток не нужен: tick зовём вручную
        server = std::make_unique<selfvoy::Server>(config);
        std::string error;
        if (!server->start(error)) {
            std::cout << "  не удалось поднять сервер: " << error << "\n";
            ++g_failures;
        }
    }

    ~Fixture() {
        server->stop();
        server.reset();
        if (observerFd >= 0) ::close(observerFd);
        std::remove(dbPath.c_str());
    }

    // Сессия поверх socketpair: второй конец отдаём наблюдателю.
    std::shared_ptr<selfvoy::Session> makeSession(const std::string& id = "test") {
        int fds[2] = {-1, -1};
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
            std::cout << "  socketpair failed\n";
            ++g_failures;
            return nullptr;
        }
        if (observerFd >= 0) ::close(observerFd);
        observerFd = fds[0];
        return std::make_shared<selfvoy::Session>(fds[1], id, "127.0.0.1:0", config);
    }

    Json call(const std::shared_ptr<selfvoy::Session>& session,
              const std::string& type,
              const Json& payload = Json::object()) {
        Json message = Json::object();
        message.set("id", Json("req-" + type));
        message.set("type", Json(type));
        message.set("payload", payload);
        return server->handleMessage(session, message);
    }

    Json registerUser(const std::shared_ptr<selfvoy::Session>& session,
                      const std::string& email,
                      const std::string& name) {
        Json payload = Json::object();
        payload.set("email", Json(email));
        payload.set("password", Json("selfvoy1234"));
        payload.set("display_name", Json(name));
        const Json registered = call(session, "auth.register", payload);
        if (registered.getString("type") != "ok") return registered;

        // Подтверждение email: код возвращён в ответе (драйвер почты dev).
        const std::string code = registered.get("payload").getString("email_code");
        Json verify = Json::object();
        verify.set("email", Json(email));
        verify.set("code", Json(code));
        const Json verified = call(session, "auth.verifyEmail", verify);
        if (verified.getString("type") != "ok") return verified;

        // Вход: выдаёт access+refresh и аутентифицирует соединение.
        Json login = Json::object();
        login.set("email", Json(email));
        login.set("password", Json("selfvoy1234"));
        return call(session, "auth.login", login);
    }
};

Json payloadOf(const Json& response) { return response.get("payload"); }

}  // namespace integration

using integration::Fixture;

TEST(server_handler_registry) {
    Fixture fixture;
    CHECK(fixture.server->hasHandler("auth.login"));
    CHECK(fixture.server->hasHandler("chat.send"));
    CHECK(fixture.server->hasHandler("agent.ask"));
    CHECK(fixture.server->hasHandler("memory.list"));
    CHECK(fixture.server->hasHandler("prefs.set"));
    CHECK(fixture.server->hasHandler("tool.run"));
    CHECK(!fixture.server->hasHandler("nope.nope"));
    CHECK(fixture.server->handlerTypes().size() >= 20);
    CHECK_EQ(fixture.server->database().backend().rfind("embedded", 0), static_cast<std::size_t>(0));
}

TEST(server_unknown_type_and_auth_guard) {
    Fixture fixture;
    auto session = fixture.makeSession();
    const Json unknown = fixture.call(session, "does.not.exist");
    CHECK_EQ(unknown.getString("type"), std::string("error"));
    CHECK_EQ(unknown.getString("code"), std::string("bad_request"));

    const Json guarded = fixture.call(session, "chat.list");
    CHECK_EQ(guarded.getString("code"), std::string("unauthorized"));

    // ping — публичный
    CHECK_EQ(fixture.call(session, "ping").getString("type"), std::string("ok"));
}

TEST(server_register_login_flow) {
    Fixture fixture;
    auto session = fixture.makeSession();

    // 1) Регистрация: сессия НЕ выдаётся, ждём подтверждения email.
    Json regPayload = Json::object();
    regPayload.set("email", Json("anna@example.com"));
    regPayload.set("password", Json("selfvoy1234"));
    regPayload.set("display_name", Json("Анна"));
    const Json registered = fixture.call(session, "auth.register", regPayload);
    CHECK_EQ(registered.getString("type"), std::string("ok"));
    CHECK(integration::payloadOf(registered).getBool("requires_verification"));
    CHECK(integration::payloadOf(registered).getString("token").empty());
    CHECK(!session->authenticated());
    const std::string emailCode = integration::payloadOf(registered).getString("email_code");
    CHECK_EQ(emailCode.size(), static_cast<std::size_t>(6));  // шестизначный код (dev-драйвер)

    // 2) Вход до подтверждения — отдельный код ошибки.
    auto earlySession = fixture.makeSession("early");
    Json login = Json::object();
    login.set("email", Json("anna@example.com"));
    login.set("password", Json("selfvoy1234"));
    CHECK_EQ(fixture.call(earlySession, "auth.login", login).getString("code"),
             std::string("email_not_verified"));

    // 3) Неверный код отклоняется, верный — подтверждает (идемпотентно повторно).
    Json badVerify = Json::object();
    badVerify.set("email", Json("anna@example.com"));
    badVerify.set("code", Json("000000"));
    CHECK_EQ(fixture.call(session, "auth.verifyEmail", badVerify).getString("code"),
             std::string("bad_request"));
    Json verify = Json::object();
    verify.set("email", Json("anna@example.com"));
    verify.set("code", Json(emailCode));
    CHECK(fixture.call(session, "auth.verifyEmail", verify).get("payload").getBool("verified"));
    const Json again = fixture.call(session, "auth.verifyEmail", verify);
    CHECK(again.get("payload").getBool("verified"));
    // Повтор для уже подтверждённого адреса не раскрывает данные пользователя.
    CHECK(again.get("payload").get("user").isNull());

    // 4) Дубль email отклоняется.
    auto other = fixture.makeSession("other");
    const Json duplicate = fixture.registerUser(other, "ANNA@example.com", "Анна 2");
    CHECK_EQ(duplicate.getString("code"), std::string("conflict"));

    // 5) Слабый пароль отклоняется.
    auto third = fixture.makeSession("third");
    Json weak = Json::object();
    weak.set("email", Json("bob@example.com"));
    weak.set("password", Json("123"));
    weak.set("display_name", Json("Боб"));
    CHECK_EQ(fixture.call(third, "auth.register", weak).getString("code"), std::string("bad_request"));

    // 6) Вход с верным и неверным паролем.
    auto loginSession = fixture.makeSession("login");
    const Json loggedIn = fixture.call(loginSession, "auth.login", login);
    CHECK_EQ(loggedIn.getString("type"), std::string("ok"));
    const std::string token = integration::payloadOf(loggedIn).getString("token");
    const std::string refreshToken = integration::payloadOf(loggedIn).getString("refresh_token");
    CHECK(!token.empty());
    CHECK(!refreshToken.empty());
    CHECK(loginSession->authenticated());

    auto badSession = fixture.makeSession("bad");
    login.set("password", Json("wrongpass1"));
    CHECK_EQ(fixture.call(badSession, "auth.login", login).getString("code"), std::string("unauthorized"));

    // 7) auth.me работает в аутентифицированной сессии.
    const Json me = fixture.call(loginSession, "auth.me");
    CHECK_EQ(integration::payloadOf(me).getString("display_name"), std::string("Анна"));

    // 8) Живой токен можно предъявить повторно (auth.token).
    auto tokenSession = fixture.makeSession("token");
    Json authPayload = Json::object();
    authPayload.set("token", Json(token));
    CHECK_EQ(fixture.call(tokenSession, "auth.token", authPayload).getString("type"), std::string("ok"));
    CHECK_EQ(tokenSession->userId(), loginSession->userId());

    // 9) После выхода тот же токен отозван.
    CHECK(fixture.call(loginSession, "auth.logout").get("payload").getBool("revoked"));
    CHECK(!loginSession->authenticated());
    auto staleSession = fixture.makeSession("stale");
    CHECK_EQ(fixture.call(staleSession, "auth.token", authPayload).getString("code"),
             std::string("unauthorized"));
}

TEST(server_refresh_rotation_and_reuse) {
    Fixture fixture;
    auto session = fixture.makeSession();
    const Json loggedIn = fixture.registerUser(session, "rita@example.com", "Рита");
    CHECK_EQ(loggedIn.getString("type"), std::string("ok"));
    const std::string refresh1 = integration::payloadOf(loggedIn).getString("refresh_token");
    CHECK(!refresh1.empty());

    // Ротация: старый токен заменяется новым в том же семействе.
    Json refreshReq = Json::object();
    refreshReq.set("refresh_token", Json(refresh1));
    const Json rotated = fixture.call(session, "auth.refresh", refreshReq);
    CHECK_EQ(rotated.getString("type"), std::string("ok"));
    const std::string refresh2 = integration::payloadOf(rotated).getString("refresh_token");
    const std::string access2 = integration::payloadOf(rotated).getString("token");
    CHECK(!refresh2.empty());
    CHECK(refresh1 != refresh2);
    CHECK(!access2.empty());

    // Новый refresh-токен работает.
    refreshReq.set("refresh_token", Json(refresh2));
    const Json rotated2 = fixture.call(session, "auth.refresh", refreshReq);
    CHECK_EQ(rotated2.getString("type"), std::string("ok"));
    const std::string refresh3 = integration::payloadOf(rotated2).getString("refresh_token");

    // Повторное использование ОТОЗВАННОГО refresh1 = кража:
    // всё семейство и все сессии отзываются.
    refreshReq.set("refresh_token", Json(refresh1));
    CHECK_EQ(fixture.call(session, "auth.refresh", refreshReq).getString("code"),
             std::string("unauthorized"));
    refreshReq.set("refresh_token", Json(refresh3));
    CHECK_EQ(fixture.call(session, "auth.refresh", refreshReq).getString("code"),
             std::string("unauthorized"));

    // Access-токен, выданный до детекта кражи, тоже отозван (сессии обнулены).
    auto accessSession = fixture.makeSession("acc");
    Json authPayload = Json::object();
    authPayload.set("token", Json(access2));
    CHECK_EQ(fixture.call(accessSession, "auth.token", authPayload).getString("code"),
             std::string("unauthorized"));

    // Неизвестный токен — просто unauthorized.
    refreshReq.set("refresh_token", Json("нет-такого"));
    CHECK_EQ(fixture.call(session, "auth.refresh", refreshReq).getString("code"),
             std::string("unauthorized"));
}

TEST(server_password_reset_flow) {
    Fixture fixture;
    auto session = fixture.makeSession();
    const Json loggedIn = fixture.registerUser(session, "oleg@example.com", "Олег");
    CHECK_EQ(loggedIn.getString("type"), std::string("ok"));
    const std::string accessToken = integration::payloadOf(loggedIn).getString("token");

    // Запрос кода: ответ не раскрывает существование email, код — в dev-режиме.
    Json forgot = Json::object();
    forgot.set("email", Json("oleg@example.com"));
    const Json forgotResp = fixture.call(session, "auth.forgotPassword", forgot);
    CHECK_EQ(forgotResp.getString("type"), std::string("ok"));
    const std::string resetCode = integration::payloadOf(forgotResp).getString("reset_code");
    CHECK(!resetCode.empty());
    // Для несуществующего email ответ тоже «ок», но без кода.
    Json unknown = Json::object();
    unknown.set("email", Json("nobody@example.com"));
    CHECK_EQ(fixture.call(session, "auth.forgotPassword", unknown).getString("type"),
             std::string("ok"));

    // Неверный код отклоняется.
    Json badReset = Json::object();
    badReset.set("email", Json("oleg@example.com"));
    badReset.set("code", Json("ffffffffffffffffffffffffffffffff"));
    badReset.set("new_password", Json("newpass123"));
    CHECK_EQ(fixture.call(session, "auth.resetPassword", badReset).getString("code"),
             std::string("bad_request"));

    // Верный код меняет пароль и обрывает все сессии.
    badReset.set("code", Json(resetCode));
    CHECK(fixture.call(session, "auth.resetPassword", badReset).get("payload").getBool("reset"));
    auto stale = fixture.makeSession("stale");
    Json authPayload = Json::object();
    authPayload.set("token", Json(accessToken));
    CHECK_EQ(fixture.call(stale, "auth.token", authPayload).getString("code"),
             std::string("unauthorized"));

    // Вход со старым паролем не проходит, с новым — проходит.
    Json login = Json::object();
    login.set("email", Json("oleg@example.com"));
    login.set("password", Json("selfvoy1234"));
    auto oldSession = fixture.makeSession("old");
    CHECK_EQ(fixture.call(oldSession, "auth.login", login).getString("code"),
             std::string("unauthorized"));
    login.set("password", Json("newpass123"));
    CHECK_EQ(fixture.call(oldSession, "auth.login", login).getString("type"), std::string("ok"));
}

TEST(server_sessions_management) {
    Fixture fixture;
    auto first = fixture.makeSession("first");
    const Json firstLogin = fixture.registerUser(first, "mia@example.com", "Миа");
    CHECK_EQ(firstLogin.getString("type"), std::string("ok"));
    auto second = fixture.makeSession("second");
    Json login = Json::object();
    login.set("email", Json("mia@example.com"));
    login.set("password", Json("selfvoy1234"));
    const Json secondLogin = fixture.call(second, "auth.login", login);
    CHECK_EQ(secondLogin.getString("type"), std::string("ok"));

    // Две живые сессии, текущая помечена.
    const Json list = fixture.call(second, "sessions.list");
    const Json sessions = integration::payloadOf(list).get("sessions");
    CHECK_EQ(sessions.items().size(), static_cast<std::size_t>(2));
    bool currentMarked = false;
    std::string otherId;
    for (const auto& item : sessions.items()) {
        if (item.getBool("current")) currentMarked = true;
        else otherId = item.getString("id");
    }
    CHECK(currentMarked);
    CHECK(!otherId.empty());

    // Отзыв чужой (первой) сессии: её токен больше не проходит.
    Json revoke = Json::object();
    revoke.set("session_id", Json(otherId));
    CHECK(fixture.call(second, "sessions.revoke", revoke).get("payload").getBool("revoked"));
    auto probe = fixture.makeSession("probe");
    Json authPayload = Json::object();
    authPayload.set("token", Json(integration::payloadOf(firstLogin).getString("token")));
    CHECK_EQ(fixture.call(probe, "auth.token", authPayload).getString("code"),
             std::string("unauthorized"));

    // revokeAll держит текущую сессию живой.
    CHECK(fixture.call(second, "sessions.revokeAll").get("payload").getBool("revoked"));
    const Json after = fixture.call(second, "sessions.list");
    CHECK_EQ(integration::payloadOf(after).get("sessions").items().size(), static_cast<std::size_t>(1));
    CHECK(second->authenticated());
}

TEST(server_password_migration_to_argon2id) {
    Fixture fixture;
    // Создаём пользователя со старым PBKDF2-хэшем напрямую в БД.
    const std::string salt = "0123456789abcdef";
    const std::string legacy = "pbkdf2$1000$" + salt + "$" +
        selfvoy::crypto::toHex(selfvoy::crypto::pbkdf2Sha256("selfvoy1234", salt, 1000, 32));
    long long userId = 0;
    CHECK(fixture.server->database().db()
              .createUser("legacy@example.com", legacy, "Леги", userId)
              .ok);
    CHECK(fixture.server->database().db().setEmailVerified(userId, true).ok);

    auto session = fixture.makeSession();
    Json login = Json::object();
    login.set("email", Json("legacy@example.com"));
    login.set("password", Json("selfvoy1234"));
    CHECK_EQ(fixture.call(session, "auth.login", login).getString("type"), std::string("ok"));

    // После входа хэш пересчитан в Argon2id.
    const auto user = fixture.server->database().db().findUserByEmail("legacy@example.com");
    CHECK(user.has_value());
    CHECK(user->passwordHash.rfind("argon2id$", 0) == 0);
    CHECK(selfvoy::crypto::verifyPassword("selfvoy1234", user->passwordHash));
}

TEST(server_change_password) {
    Fixture fixture;
    auto session = fixture.makeSession();
    const Json loggedIn = fixture.registerUser(session, "kai@example.com", "Кай");
    CHECK_EQ(loggedIn.getString("type"), std::string("ok"));

    // Неверный текущий пароль отклоняется.
    Json change = Json::object();
    change.set("old_password", Json("wrongold1"));
    change.set("new_password", Json("brandnew1"));
    CHECK_EQ(fixture.call(session, "auth.changePassword", change).getString("code"),
             std::string("forbidden"));

    // Верная смена: доступ сохраняется, старый пароль больше не работает.
    change.set("old_password", Json("selfvoy1234"));
    CHECK(fixture.call(session, "auth.changePassword", change).get("payload").getBool("changed"));
    auto fresh = fixture.makeSession("fresh");
    Json login = Json::object();
    login.set("email", Json("kai@example.com"));
    login.set("password", Json("selfvoy1234"));
    CHECK_EQ(fixture.call(fresh, "auth.login", login).getString("code"), std::string("unauthorized"));
    login.set("password", Json("brandnew1"));
    CHECK_EQ(fixture.call(fresh, "auth.login", login).getString("type"), std::string("ok"));
}

TEST(server_chat_flow_and_delivery) {
    Fixture fixture;
    auto anna = fixture.makeSession("anna");
    auto anya = fixture.makeSession("anya");
    fixture.registerUser(anna, "anna@example.com", "Анна");
    fixture.registerUser(anya, "anya@example.com", "Аня");

    // Чат с самим собой запрещён
    Json self = Json::object();
    self.set("contact", Json("anna@example.com"));
    CHECK_EQ(fixture.call(anna, "chat.open", self).getString("code"), std::string("bad_request"));

    // Открытие чата
    Json open = Json::object();
    open.set("contact", Json("anya@example.com"));
    const Json opened = fixture.call(anna, "chat.open", open);
    CHECK_EQ(opened.getString("type"), std::string("ok"));
    const long long chatId = integration::payloadOf(opened).get("chat").getInt("id");
    CHECK(chatId > 0);

    // Повторное открытие возвращает тот же чат
    const Json reopened = fixture.call(anna, "chat.open", open);
    CHECK_EQ(integration::payloadOf(reopened).get("chat").getInt("id"), chatId);
    CHECK(!integration::payloadOf(reopened).getBool("created"));

    // Список чатов
    const Json list = fixture.call(anna, "chat.list");
    CHECK_EQ(integration::payloadOf(list).get("chats").size(), static_cast<std::size_t>(1));

    // Отправка сообщения
    Json send = Json::object();
    send.set("chat_id", Json(chatId));
    send.set("body", Json("Привет! Обсудим стартап?"));
    const Json sent = fixture.call(anna, "chat.send", send);
    CHECK_EQ(sent.getString("type"), std::string("ok"));
    CHECK(integration::payloadOf(sent).getInt("id") > 0);
    CHECK_EQ(integration::payloadOf(sent).getString("sender_name"), std::string("Анна"));

    // История
    Json history = Json::object();
    history.set("chat_id", Json(chatId));
    const Json historyResponse = fixture.call(anya, "chat.history", history);
    const Json messages = integration::payloadOf(historyResponse).get("messages");
    CHECK_EQ(messages.size(), static_cast<std::size_t>(1));
    CHECK_EQ(messages.at(0).getString("body"), std::string("Привет! Обсудим стартап?"));

    // Чужой чат недоступен
    auto bob = fixture.makeSession("bob");
    fixture.registerUser(bob, "bob@example.com", "Боб");
    CHECK_EQ(fixture.call(bob, "chat.history", history).getString("code"), std::string("forbidden"));

    // Посторонний не может писать в чат
    Json intruder = Json::object();
    intruder.set("chat_id", Json(chatId));
    intruder.set("body", Json("я тут"));
    CHECK_EQ(fixture.call(bob, "chat.send", intruder).getString("code"), std::string("forbidden"));

    // Поиск пользователей (M11): по подстроке email больше не находит,
    // по имени — да, по точному email — да; наружу только публичная карточка.
    Json search = Json::object();
    search.set("query", Json("anya"));
    CHECK_EQ(integration::payloadOf(fixture.call(anna, "users.search", search))
                 .get("users")
                 .size(),
             static_cast<std::size_t>(0));
    search.set("query", Json("Аня"));
    const Json byName = integration::payloadOf(fixture.call(anna, "users.search", search)).get("users");
    CHECK_EQ(byName.size(), static_cast<std::size_t>(1));
    CHECK(!byName.at(0).contains("email"));
    CHECK(!byName.at(0).contains("last_seen_at"));
    CHECK(!byName.at(0).contains("email_verified"));
    CHECK(byName.at(0).getInt("id") > 0);
    CHECK_EQ(byName.at(0).getString("display_name"), std::string("Аня"));
    search.set("query", Json("anya@example.com"));
    CHECK_EQ(integration::payloadOf(fixture.call(anna, "users.search", search)).get("users").size(),
             static_cast<std::size_t>(1));
    search.set("query", Json("anya@example.co"));
    CHECK_EQ(integration::payloadOf(fixture.call(anna, "users.search", search)).get("users").size(),
             static_cast<std::size_t>(0));
    Json members = Json::object();
    members.set("chat_id", Json(chatId));
    CHECK_EQ(integration::payloadOf(fixture.call(anna, "chat.members", members))
                 .get("members")
                 .size(),
             static_cast<std::size_t>(2));
}

TEST(server_session_delivers_frames_over_socket) {
    Fixture fixture;
    auto session = fixture.makeSession("socket");
    fixture.registerUser(session, "socket@example.com", "Сокет");

    Json payload = Json::object();
    payload.set("hello", Json("мир"));
    session->send(selfvoy::protocol::event("test.event", payload));

    // Читаем кадры из второго конца socketpair, пока не встретим test.event.
    // Вход (внутри registerUser) теперь порождает событие notification.new
    // («Новый вход в Selfvoy», этап 13) — проверяем, что оно приходит первым.
    // Кадр пришёл от сервера, поэтому маска в нём запрещена (requireMask=false).
    std::string raw;
    bool sawNotification = false;
    bool sawTestEvent = false;
    Json testEvent;
    for (int attempt = 0; attempt < 8 && !sawTestEvent; ++attempt) {
        char buffer[1024];
        const ssize_t received = ::recv(fixture.observerFd, buffer, sizeof(buffer), 0);
        CHECK(received > 0);
        raw.append(buffer, static_cast<std::size_t>(received));
        while (true) {
            selfvoy::ws::Frame frame;
            std::string error;
            std::size_t consumed = 0;
            const auto result =
                selfvoy::ws::decodeFrame(raw, raw.size(), consumed, frame, error, false);
            if (result != selfvoy::ws::DecodeResult::Complete) break;
            raw.erase(0, consumed);
            const Json decoded = Json::parse(frame.payload);
            if (decoded.getString("event") == std::string("notification.new")) {
                sawNotification = true;
            } else if (decoded.getString("event") == std::string("test.event")) {
                sawTestEvent = true;
                testEvent = decoded;
                break;
            }
        }
    }
    CHECK(sawNotification);
    CHECK(sawTestEvent);
    CHECK_EQ(testEvent.get("payload").getString("hello"), std::string("мир"));
}

TEST(server_memory_and_preferences) {
    Fixture fixture;
    auto session = fixture.makeSession("memory");
    fixture.registerUser(session, "anna@example.com", "Анна");

    Json add = Json::object();
    add.set("text", Json("Люблю тихие кофейни"));
    add.set("kind", Json("preference"));
    CHECK_EQ(fixture.call(session, "memory.add", add).get("payload").getInt("saved"), 1);

    Json query = Json::object();
    query.set("query", Json("кофейни"));
    const Json listed = fixture.call(session, "memory.list", query);
    const Json entries = integration::payloadOf(listed).get("entries");
    CHECK(entries.size() >= 1);
    CHECK_EQ(entries.at(0).getString("text"), std::string("Люблю тихие кофейни"));

    // Настройки: чтение по умолчанию и запись
    const Json defaults = fixture.call(session, "prefs.get");
    CHECK_EQ(integration::payloadOf(defaults).getString("theme"), std::string("graphite"));

    Json update = Json::object();
    Json diet = Json::array();
    diet.push(Json("vegan"));
    update.set("diet", diet);
    update.set("city", Json("Керкраде"));
    update.set("lat", Json(50.861));
    update.set("lon", Json(6.064));
    update.set("budget_limit", Json(3));
    const Json updated = fixture.call(session, "prefs.set", update);
    CHECK_EQ(updated.getString("type"), std::string("ok"));
    CHECK_EQ(integration::payloadOf(updated).getString("city"), std::string("Керкраде"));
    CHECK_EQ(integration::payloadOf(updated).get("diet").size(), static_cast<std::size_t>(1));

    // Контекст для AI-сервиса содержит память и настройки
    const Json context = fixture.server->memory().context(session->userId(), "найдём кофейню");
    CHECK_EQ(context.getString("email"), std::string("anna@example.com"));
    CHECK(context.get("memory").size() >= 1);
    CHECK_EQ(context.get("preferences").getString("city"), std::string("Керкраде"));
}

TEST(server_tools_execution) {
    Fixture fixture;
    auto session = fixture.makeSession("tools");
    fixture.registerUser(session, "anna@example.com", "Анна");

    CHECK(integration::payloadOf(fixture.call(session, "tool.list")).get("tools").size() >= 6);

    // create_note
    Json note = Json::object();
    note.set("tool", Json("create_note"));
    Json noteArgs = Json::object();
    noteArgs.set("text", Json("Идея: сеть ИИ-агентов"));
    note.set("args", noteArgs);
    const Json noteResult = fixture.call(session, "tool.run", note);
    CHECK_EQ(noteResult.getString("type"), std::string("ok"));
    CHECK(!integration::payloadOf(noteResult).getString("note_id").empty());

    // find_cafe уважает диету
    Json cafe = Json::object();
    cafe.set("tool", Json("find_cafe"));
    Json cafeArgs = Json::object();
    Json diet = Json::array();
    diet.push(Json("vegan"));
    cafeArgs.set("diet", diet);
    cafeArgs.set("city", Json("Керкраде"));
    cafe.set("args", cafeArgs);
    const Json cafes = fixture.call(session, "tool.run", cafe);
    const Json results = integration::payloadOf(cafes).get("results");
    CHECK(results.size() >= 1);
    bool meatFree = true;
    for (const auto& item : results.items()) {
        const Json tagArray = item.get("tags");
        for (const auto& tag : tagArray.items()) {
            if (tag.asString() == "meat") meatFree = false;
        }
    }
    CHECK(meatFree);

    // book_table
    Json book = Json::object();
    book.set("tool", Json("book_table"));
    Json bookArgs = Json::object();
    bookArgs.set("place", Json("Кофе на полпути"));
    bookArgs.set("at", Json("2026-09-19T12:00:00Z"));
    bookArgs.set("people", Json(2));
    book.set("args", bookArgs);
    // L20: book_table опасный (default ask) — прямой tool.run не исполняет его,
    // а создаёт отложенное действие; после allow — исполняет сразу.
    const Json pendingBook = fixture.call(session, "tool.run", book);
    CHECK_EQ(pendingBook.getString("type"), std::string("ok"));
    CHECK(integration::payloadOf(pendingBook).getBool("requires_confirmation"));
    CHECK(integration::payloadOf(pendingBook).getInt("confirmation_id") > 0);
    CHECK(integration::payloadOf(pendingBook).getString("confirmation").empty());
    Json allow = Json::object();
    allow.set("tool", Json("book_table"));
    allow.set("mode", Json("allow"));
    CHECK_EQ(fixture.call(session, "permissions.set", allow).getString("type"), std::string("ok"));
    const Json booked = fixture.call(session, "tool.run", book);
    CHECK(integration::payloadOf(booked).getString("confirmation").rfind("SELFVOY-", 0) == 0);
    // deny блокирует и прямой вызов.
    allow.set("tool", Json("create_note"));
    allow.set("mode", Json("deny"));
    fixture.call(session, "permissions.set", allow);
    const Json deniedNote = fixture.call(session, "tool.run", note);
    CHECK_EQ(deniedNote.getString("code"), std::string("forbidden"));

    // Неизвестный инструмент
    Json unknown = Json::object();
    unknown.set("tool", Json("launch_rockets"));
    unknown.set("args", Json::object());
    CHECK_EQ(fixture.call(session, "tool.run", unknown).getString("type"), std::string("error"));
}

TEST(server_agent_reports_upstream_failure) {
    Fixture fixture;
    auto session = fixture.makeSession("agent");
    fixture.registerUser(session, "anna@example.com", "Анна");

    Json ask = Json::object();
    ask.set("message", Json("Хочу встретиться с Аней в эти выходные обсудить стартап"));
    const Json response = fixture.call(session, "agent.ask", ask);
    CHECK_EQ(response.getString("type"), std::string("error"));
    CHECK_EQ(response.getString("code"), std::string("upstream_error"));

    // Статус агента показывает, что сервис недоступен
    const Json status = fixture.call(session, "agent.status");
    CHECK(!integration::payloadOf(status).getBool("available"));

    // A2A без собеседника — not_found
    Json negotiate = Json::object();
    negotiate.set("target", Json("ghost@example.com"));
    CHECK_EQ(fixture.call(session, "agent.negotiate", negotiate).getString("code"),
             std::string("not_found"));
}

TEST(server_speech_transcribe) {
    Fixture fixture;
    auto session = fixture.makeSession("speech");
    fixture.registerUser(session, "sonya@example.com", "Соня");

    // Пустое аудио — bad_request (до обращения к AI-сервису).
    const Json empty = fixture.call(session, "speech.transcribe", Json::object());
    CHECK_EQ(empty.getString("type"), std::string("error"));
    CHECK_EQ(empty.getString("code"), std::string("bad_request"));

    // С аудио, но AI-сервис недоступен — upstream_error.
    Json req = Json::object();
    req.set("audio", Json("AQIDBA=="));  // base64-заглушка
    req.set("language", Json("ru"));
    const Json response = fixture.call(session, "speech.transcribe", req);
    CHECK_EQ(response.getString("type"), std::string("error"));
    CHECK_EQ(response.getString("code"), std::string("upstream_error"));
}

TEST(server_health_snapshot) {
    Fixture fixture;
    const Json health = fixture.server->health();
    CHECK_EQ(health.getString("status"), std::string("ok"));
    CHECK(health.get("database_healthy").asBool());
    CHECK(health.get("connections").isObject());
    CHECK(health.getInt("port") >= 0);
}

TEST(server_two_factor_full_flow) {
    Fixture fixture;

    // 1) Регистрация + вход (сессию выдаёт обычный вход — 2FA ещё не включена).
    auto setupSession = fixture.makeSession("2fa-setup");
    const Json registered = fixture.registerUser(setupSession, "vera@example.com", "Вера");
    CHECK_EQ(registered.getString("type"), std::string("ok"));
    CHECK(setupSession->authenticated());

    // 2) Статус до настройки: выключена, не в процессе.
    const Json statusBefore = fixture.call(setupSession, "auth.status2fa");
    CHECK_EQ(statusBefore.get("payload").getBool("enabled"), false);
    CHECK_EQ(statusBefore.get("payload").getBool("pending"), false);

    // 3) Настройка: возвращает секрет (base32) и otpauth-URI.
    const Json setup = fixture.call(setupSession, "auth.setup2fa");
    CHECK_EQ(setup.getString("type"), std::string("ok"));
    const std::string secretB32 = setup.get("payload").getString("secret");
    CHECK(!secretB32.empty());
    CHECK(setup.get("payload").getString("otpauth_uri").rfind("otpauth://totp/", 0) == 0);
    CHECK_EQ(setup.get("payload").getInt("digits"), 6);
    CHECK_EQ(setup.get("payload").getInt("period"), 30);

    // До подтверждения 2FA всё ещё выключена (нельзя «включить» непроверенный секрет).
    CHECK_EQ(fixture.call(setupSession, "auth.status2fa").get("payload").getBool("enabled"), false);
    CHECK_EQ(fixture.call(setupSession, "auth.status2fa").get("payload").getBool("pending"), true);

    // 4) Подтверждение одноразовым кодом (считаем из секрета по RFC 6238).
    std::string rawSecret;
    CHECK(selfvoy::totp::decodeSecretBase32(secretB32, rawSecret));
    Json confirm = Json::object();
    confirm.set("code", Json(selfvoy::totp::codeNow(rawSecret)));
    const Json confirmed = fixture.call(setupSession, "auth.confirm2fa", confirm);
    CHECK_EQ(confirmed.getString("type"), std::string("ok"));
    // Держим Json в именованной переменной: get() возвращает по значению,
    // а items() — ссылку внутрь него (иначе ссылка повисла бы на временном).
    const Json recoveryCodesJson = confirmed.get("payload").get("recovery_codes");
    const auto& recoveryCodes = recoveryCodesJson.items();
    CHECK_EQ(recoveryCodes.size(), std::size_t(10));
    CHECK_EQ(recoveryCodes[0].asString().size(), std::size_t(11));  // XXXXX-XXXXX
    const std::string firstRecovery = recoveryCodes[0].asString();

    // 5) Теперь включена.
    const Json statusAfter = fixture.call(setupSession, "auth.status2fa");
    CHECK_EQ(statusAfter.get("payload").getBool("enabled"), true);
    CHECK_EQ(statusAfter.get("payload").getInt("recovery_codes_left"), 10);

    // 6) Выход; новый вход требует 2FA (пароль → TOTP → сессия).
    fixture.call(setupSession, "auth.logout");
    auto loginSession = fixture.makeSession("2fa-login");
    Json login = Json::object();
    login.set("email", Json("vera@example.com"));
    login.set("password", Json("selfvoy1234"));
    const Json needsCode = fixture.call(loginSession, "auth.login", login);
    CHECK_EQ(needsCode.getString("type"), std::string("error"));
    CHECK_EQ(needsCode.getString("code"), std::string("requires_2fa"));
    CHECK(!loginSession->authenticated());

    // 7) Неверный код отклоняется.
    Json badLogin = login;
    badLogin.set("code", Json("000000"));
    CHECK_EQ(fixture.call(loginSession, "auth.login2fa", badLogin).getString("code"),
             std::string("unauthorized"));
    CHECK(!loginSession->authenticated());

    // 8) Верный TOTP-код → сессия. Текущий код уже использован при confirm2fa
    // (одноразовость), поэтому берём код следующего окна — сервер принимает его
    // (окно ±1 период) и проверка одноразовости проходит.
    const std::int64_t nextWindow = (std::time(nullptr) / selfvoy::totp::kPeriod + 1) * selfvoy::totp::kPeriod;
    Json goodLogin = login;
    goodLogin.set("code", Json(selfvoy::totp::codeAt(rawSecret, nextWindow)));
    goodLogin.set("device_id", Json("device-vera-1"));
    goodLogin.set("trust_device", Json(true));
    const Json loggedIn = fixture.call(loginSession, "auth.login2fa", goodLogin);
    CHECK_EQ(loggedIn.getString("type"), std::string("ok"));
    CHECK(loginSession->authenticated());
    CHECK(!loggedIn.get("payload").getString("token").empty());

    // 9) Доверенное устройство зарегистрировано.
    const Json devices = fixture.call(loginSession, "devices.list");
    CHECK_EQ(devices.get("payload").get("devices").items().size(), std::size_t(1));

    // 10) Вход с доверенным device_id пропускает 2FA.
    auto trustedSession = fixture.makeSession("2fa-trusted");
    Json trustedLogin = login;
    trustedLogin.set("device_id", Json("device-vera-1"));
    const Json trustedResult = fixture.call(trustedSession, "auth.login", trustedLogin);
    CHECK_EQ(trustedResult.getString("type"), std::string("ok"));
    CHECK(trustedSession->authenticated());

    // 11) Вход с НЕдоверенным device_id снова требует код.
    auto otherSession = fixture.makeSession("2fa-other");
    Json otherLogin = login;
    otherLogin.set("device_id", Json("device-vera-OTHER"));
    CHECK_EQ(fixture.call(otherSession, "auth.login", otherLogin).getString("code"),
             std::string("requires_2fa"));

    // 12) Резервный код работает и сгорает (одноразовый).
    auto recoverySession = fixture.makeSession("2fa-recovery");
    Json recoveryLogin = login;
    recoveryLogin.set("code", Json(firstRecovery));
    const Json recoveryResult = fixture.call(recoverySession, "auth.login2fa", recoveryLogin);
    CHECK_EQ(recoveryResult.getString("type"), std::string("ok"));
    CHECK(recoverySession->authenticated());
    // Осталось 9 неиспользованных кодов.
    CHECK_EQ(fixture.call(recoverySession, "auth.status2fa").get("payload").getInt("recovery_codes_left"), 9);
    // Тот же код повторно не принимается.
    auto reuseSession = fixture.makeSession("2fa-reuse");
    Json reuseLogin = login;
    reuseLogin.set("code", Json(firstRecovery));
    CHECK_EQ(fixture.call(reuseSession, "auth.login2fa", reuseLogin).getString("code"),
             std::string("unauthorized"));

    // 13) Отзыв доверенного устройства → вход с ним снова требует код.
    const std::string deviceId = devices.get("payload").get("devices").items()[0].getString("id");
    Json revoke = Json::object();
    revoke.set("id", Json(deviceId));
    CHECK_EQ(fixture.call(recoverySession, "devices.revoke", revoke).getString("type"), std::string("ok"));
    auto afterRevoke = fixture.makeSession("2fa-after-revoke");
    Json afterRevokeLogin = login;
    afterRevokeLogin.set("device_id", Json("device-vera-1"));
    CHECK_EQ(fixture.call(afterRevoke, "auth.login", afterRevokeLogin).getString("code"),
             std::string("requires_2fa"));

    // 14) Отключение 2FA требует пароль.
    Json disableBad = Json::object();
    disableBad.set("password", Json("wrongpass1"));
    CHECK_EQ(fixture.call(recoverySession, "auth.disable2fa", disableBad).getString("code"),
             std::string("unauthorized"));
    Json disableOk = Json::object();
    disableOk.set("password", Json("selfvoy1234"));
    CHECK_EQ(fixture.call(recoverySession, "auth.disable2fa", disableOk).getString("type"), std::string("ok"));
    CHECK_EQ(fixture.call(recoverySession, "auth.status2fa").get("payload").getBool("enabled"), false);

    // 15) После отключения обычный вход снова выдаёт сессию без кода.
    auto plainSession = fixture.makeSession("2fa-plain");
    Json plainLogin = Json::object();
    plainLogin.set("email", Json("vera@example.com"));
    plainLogin.set("password", Json("selfvoy1234"));
    CHECK_EQ(fixture.call(plainSession, "auth.login", plainLogin).getString("type"), std::string("ok"));
    CHECK(plainSession->authenticated());
}

// ------------------------------------------- этап 8: разрешения и подтверждения
TEST(tool_danger_classification) {
    using selfvoy::ToolManager;
    // Опасные: внешние побочные эффекты.
    CHECK(ToolManager::isDangerous("send_message"));
    CHECK(ToolManager::isDangerous("send_email"));
    CHECK(ToolManager::isDangerous("book_table"));
    // Безопасные: локальные/читают.
    CHECK(!ToolManager::isDangerous("create_note"));
    CHECK(!ToolManager::isDangerous("create_reminder"));
    CHECK(!ToolManager::isDangerous("find_cafe"));
    CHECK(!ToolManager::isDangerous("check_calendar"));
    CHECK(!ToolManager::isDangerous("suggest_time"));
    // Режим по умолчанию: опасные → ask, безопасные → allow.
    CHECK_EQ(ToolManager::defaultMode("send_email"), std::string("ask"));
    CHECK_EQ(ToolManager::defaultMode("find_cafe"), std::string("allow"));
    CHECK(ToolManager::isKnownTool("book_table"));
    CHECK(!ToolManager::isKnownTool("launch_missiles"));
}

TEST(ai_tool_permissions_and_pending_actions_db) {
    auto db = selfvoy::makeEmbeddedDatabase("");  // in-memory (без файла)
    long long userId = 0;
    CHECK(db->createUser("perm@example.com", "argon2id$x", "Перм", userId).ok);
    CHECK(userId > 0);

    // По умолчанию разрешения нет.
    CHECK_EQ(db->getToolPermission(userId, "send_email"), std::string(""));
    // Установка и чтение.
    CHECK(db->setToolPermission(userId, "send_email", "allow").ok);
    CHECK_EQ(db->getToolPermission(userId, "send_email"), std::string("allow"));
    // Недопустимый режим отклоняется.
    CHECK(!db->setToolPermission(userId, "send_email", "bogus").ok);
    CHECK_EQ(db->getToolPermission(userId, "send_email"), std::string("allow"));
    // Список разрешений.
    CHECK(db->setToolPermission(userId, "book_table", "deny").ok);
    const auto perms = db->listToolPermissions(userId);
    CHECK_EQ(perms.size(), static_cast<std::size_t>(2));

    // Отложенное действие: создание → поиск → список → разрешение.
    selfvoy::PendingActionRecord pending;
    pending.userId = userId;
    pending.tool = "send_email";
    pending.args = Json::object();
    pending.args.set("to", Json("x@example.com"));
    pending.summary = "Отправить письмо на x@example.com";
    const long long id = db->createPendingAction(pending);
    CHECK(id > 0);
    const auto found = db->findPendingAction(id);
    CHECK(found.has_value());
    CHECK_EQ(found->status, std::string("pending"));
    CHECK_EQ(found->tool, std::string("send_email"));
    CHECK_EQ(found->args.getString("to"), std::string("x@example.com"));
    CHECK_EQ(db->listPendingActions(userId, "pending", 10).size(), static_cast<std::size_t>(1));
    CHECK_EQ(db->listPendingActions(userId, "executed", 10).size(), static_cast<std::size_t>(0));

    Json outcome = Json::object();
    outcome.set("ok", Json(true));
    CHECK(db->resolvePendingAction(id, "executed", outcome).ok);
    CHECK_EQ(db->findPendingAction(id)->status, std::string("executed"));
    CHECK_EQ(db->listPendingActions(userId, "pending", 10).size(), static_cast<std::size_t>(0));
    // Несуществующее действие не разрешается.
    CHECK(!db->resolvePendingAction(99999, "executed", outcome).ok);
}

TEST(ai_permissions_ws_and_confirmation_barrier) {
    Fixture fixture;
    auto session = fixture.makeSession();
    fixture.registerUser(session, "dan@example.com", "Дан");
    const long long userId = session->userId();
    auto& db = fixture.server->database().db();

    // permissions.list: каталог с эффективными режимами и флагом опасности.
    const Json list = fixture.call(session, "permissions.list");
    CHECK_EQ(list.getString("type"), std::string("ok"));
    const Json listPayload = list.get("payload");
    const Json toolsList = listPayload.get("tools");
    bool sawEmail = false;
    for (const auto& tool : toolsList.items()) {
        if (tool.getString("tool") == "send_email") {
            sawEmail = true;
            CHECK(tool.getBool("dangerous"));
            CHECK_EQ(tool.getString("default_mode"), std::string("ask"));
            CHECK_EQ(tool.getString("mode"), std::string("ask"));  // по умолчанию
        }
    }
    CHECK(sawEmail);

    // permissions.set: валидный режим применяется, невалидный отклоняется.
    Json setReq = Json::object();
    setReq.set("tool", Json("send_email"));
    setReq.set("mode", Json("allow"));
    CHECK_EQ(fixture.call(session, "permissions.set", setReq).getString("type"), std::string("ok"));
    setReq.set("mode", Json("bogus"));
    CHECK_EQ(fixture.call(session, "permissions.set", setReq).getString("code"), std::string("bad_request"));
    setReq.set("tool", Json("nope"));
    setReq.set("mode", Json("allow"));
    CHECK_EQ(fixture.call(session, "permissions.set", setReq).getString("code"), std::string("bad_request"));

    // Барьер подтверждения: создаём отложенное опасное действие (как это
    // сделал бы агент) и подтверждаем — сервер исполняет инструмент.
    selfvoy::PendingActionRecord pending;
    pending.userId = userId;
    pending.tool = "send_email";
    pending.args = Json::object();
    pending.args.set("to", Json("x@example.com"));
    pending.args.set("body", Json("привет"));
    pending.summary = "Отправить письмо на x@example.com";
    const long long id = db.createPendingAction(pending);
    CHECK(id > 0);

    const Json pendingList = fixture.call(session, "confirmation.list");
    CHECK_EQ(pendingList.getString("type"), std::string("ok"));
    CHECK(pendingList.get("payload").get("actions").size() >= 1);

    Json approve = Json::object();
    approve.set("id", Json(id));
    const Json approved = fixture.call(session, "confirmation.approve", approve);
    CHECK_EQ(approved.getString("type"), std::string("ok"));
    CHECK_EQ(approved.get("payload").getString("status"), std::string("executed"));
    // Повторное подтверждение уже обработанного — ошибка.
    CHECK_EQ(fixture.call(session, "confirmation.approve", approve).getString("code"),
             std::string("bad_request"));

    // Отклонение: действие помечается denied, не исполняется.
    selfvoy::PendingActionRecord second;
    second.userId = userId;
    second.tool = "book_table";
    second.summary = "Забронировать столик";
    const long long id2 = db.createPendingAction(second);
    Json deny = Json::object();
    deny.set("id", Json(id2));
    const Json denied = fixture.call(session, "confirmation.deny", deny);
    CHECK_EQ(denied.get("payload").getString("status"), std::string("denied"));

    // Чужое отложенное действие недоступно (проверка владения).
    auto other = fixture.makeSession("other");
    fixture.registerUser(other, "eve@example.com", "Ева");
    Json approveOther = Json::object();
    approveOther.set("id", Json(id));
    CHECK_EQ(fixture.call(other, "confirmation.approve", approveOther).getString("code"),
             std::string("not_found"));
}

TEST(ai_tasks_crud_ws) {
    Fixture fixture;
    auto session = fixture.makeSession();
    fixture.registerUser(session, "task@example.com", "Таск");

    Json create = Json::object();
    create.set("title", Json("Купить кофе"));
    create.set("notes", Json("эспрессо"));
    create.set("priority", Json(2));
    const Json created = fixture.call(session, "tasks.create", create);
    CHECK_EQ(created.getString("type"), std::string("ok"));
    const long long taskId = created.get("payload").getInt("id");
    CHECK(taskId > 0);
    CHECK_EQ(created.get("payload").getString("status"), std::string("pending"));
    CHECK_EQ(created.get("payload").getInt("priority"), 2);

    // Пустой заголовок отклоняется.
    Json empty = Json::object();
    empty.set("title", Json(""));
    CHECK_EQ(fixture.call(session, "tasks.create", empty).getString("code"), std::string("bad_request"));

    // Список содержит задачу.
    CHECK(fixture.call(session, "tasks.list", Json::object()).get("payload").get("tasks").size() >= 1);

    // Отметка «выполнено».
    Json complete = Json::object();
    complete.set("id", Json(taskId));
    CHECK_EQ(fixture.call(session, "tasks.complete", complete).get("payload").getString("status"),
             std::string("done"));

    // Фильтр по статусу pending теперь пуст.
    Json pending = Json::object();
    pending.set("status", Json("pending"));
    CHECK_EQ(fixture.call(session, "tasks.list", pending).get("payload").get("tasks").size(),
             static_cast<std::size_t>(0));

    // Чужая задача недоступна (проверка владения).
    auto other = fixture.makeSession("other");
    fixture.registerUser(other, "other@example.com", "Другой");
    CHECK_EQ(fixture.call(other, "tasks.complete", complete).getString("code"), std::string("not_found"));

    // Удаление: первый раз ок, второй — not_found.
    CHECK_EQ(fixture.call(session, "tasks.delete", complete).getString("type"), std::string("ok"));
    CHECK_EQ(fixture.call(session, "tasks.delete", complete).getString("code"), std::string("not_found"));
}

TEST(ai_task_scheduler_tick) {
    Fixture fixture;
    auto session = fixture.makeSession();
    fixture.registerUser(session, "sched@example.com", "Шед");
    auto& db = fixture.server->database().db();

    // Задача с напоминанием, срок которого уже наступил (remind_at в прошлом).
    selfvoy::TaskRecord task;
    task.userId = session->userId();
    task.title = "Позвонить клиенту";
    task.remindAt = "2020-01-01T00:00:00.000Z";  // заведомо <= now
    long long id = 0;
    CHECK(db.createTask(task, id).ok);
    CHECK(db.findTask(id)->remindedAt.empty());
    CHECK_EQ(db.listDueTasks(10).size(), static_cast<std::size_t>(1));

    // Проход планировщика помечает напомненным и возвращает задачу.
    CHECK_EQ(fixture.server->runSchedulerTick(10), static_cast<std::size_t>(1));
    CHECK(!db.findTask(id)->remindedAt.empty());
    // Повторно не отправляется (одноразово).
    CHECK_EQ(fixture.server->runSchedulerTick(10), static_cast<std::size_t>(0));

    // Задача без remind_at не становится due.
    selfvoy::TaskRecord noRemind;
    noRemind.userId = session->userId();
    noRemind.title = "Без напоминания";
    long long id2 = 0;
    CHECK(db.createTask(noRemind, id2).ok);
    CHECK_EQ(fixture.server->runSchedulerTick(10), static_cast<std::size_t>(0));
}

TEST(ai_create_reminder_makes_task) {
    Fixture fixture;
    auto session = fixture.makeSession();
    fixture.registerUser(session, "rem@example.com", "Рем");

    Json run = Json::object();
    run.set("tool", Json("create_reminder"));
    Json args = Json::object();
    args.set("text", Json("выпить воды"));
    args.set("at", Json("2030-01-01T10:00:00.000Z"));  // будущее → не due
    run.set("args", args);
    const Json result = fixture.call(session, "tool.run", run);
    CHECK_EQ(result.getString("type"), std::string("ok"));
    CHECK(result.get("payload").getInt("task_id") > 0);

    // Напоминание видно как задача; срок в будущем → планировщик не трогает.
    CHECK(fixture.call(session, "tasks.list", Json::object()).get("payload").get("tasks").size() >= 1);
    CHECK_EQ(fixture.server->runSchedulerTick(10), static_cast<std::size_t>(0));

    // В контекст AI напоминание уходит занятым слотом с start/end (иначе AI отвечает 422
    // и у пользователя ломаются переговоры); запись расписания без времени — только в memory.
    fixture.server->memory().remember(session->userId(), [] {
        Json updates = Json::array();
        Json entry = Json::object();
        entry.set("kind", Json("schedule"));
        entry.set("text", Json("По средам бассейн"));
        updates.push(entry);
        return updates;
    }());
    Json offsetRun = run;
    Json offsetArgs = args;
    offsetArgs.set("text", Json("позвонить юристу"));
    offsetArgs.set("at", Json("2030-01-02T10:00:00+03:00"));
    offsetRun.set("args", offsetArgs);
    CHECK_EQ(fixture.call(session, "tool.run", offsetRun).getString("type"), std::string("ok"));
    const Json calendar = fixture.server->memory().context(session->userId(), "").get("calendar");
    CHECK_EQ(calendar.size(), static_cast<std::size_t>(2));
    bool water = false, lawyer = false;
    for (const auto& event : calendar.items()) {
        CHECK(!event.getString("start").empty());
        CHECK(!event.getString("end").empty());
        if (event.getString("title") == "Напоминание: выпить воды") {
            water = true;
            CHECK_EQ(event.getString("start"), std::string("2030-01-01T10:00:00Z"));
            CHECK_EQ(event.getString("end"), std::string("2030-01-01T10:30:00Z"));
        }
        if (event.getString("title") == "Напоминание: позвонить юристу") {
            lawyer = true;
            CHECK_EQ(event.getString("start"), std::string("2030-01-02T07:00:00Z"));
        }
    }
    CHECK(water);
    CHECK(lawyer);
}

// --------------------------------------------------- интеграции (этап 9)

TEST(integrations_pkce_s256_known_vector) {
    // RFC 7636, Приложение B: эталонная пара verifier → challenge.
    const std::string verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
    const std::string challenge = selfvoy::crypto::base64UrlEncode(selfvoy::crypto::sha256(verifier));
    CHECK_EQ(challenge, std::string("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
}

TEST(integrations_begin_and_state_rules) {
    ::setenv("SELFVOY_GOOGLE_CLIENT_ID", "test-client-id", 1);
    ::setenv("SELFVOY_GOOGLE_CLIENT_SECRET", "test-client-secret", 1);
    ::setenv("SELFVOY_GOOGLE_TOKEN_URL", "http://127.0.0.1:1/token", 1);  // недоступен
    Fixture fixture;
    auto anna = fixture.makeSession("anna");
    auto oleg = fixture.makeSession("oleg");
    fixture.registerUser(anna, "anna@example.com", "Анна");
    fixture.registerUser(oleg, "oleg@example.com", "Олег");

    Json beginPayload = Json::object();
    beginPayload.set("provider", Json("google_calendar"));
    beginPayload.set("redirect_uri", Json("http://127.0.0.1:9999/callback"));
    const Json begin = fixture.call(anna, "integrations.begin", beginPayload);
    CHECK_EQ(begin.getString("type"), std::string("ok"));
    const Json beginData = begin.get("payload");
    const std::string state = beginData.getString("state");
    CHECK(!state.empty());
    const std::string url = beginData.getString("authorize_url");
    CHECK(url.find("code_challenge_method=S256") != std::string::npos);
    CHECK(url.find("code_challenge=") != std::string::npos);
    CHECK(url.find("state=" + state) != std::string::npos);
    CHECK(url.find("calendar.events.readonly") != std::string::npos);

    // Неизвестный провайдер отклоняется.
    Json badProvider = beginPayload;
    badProvider.set("provider", Json("yandex_disk"));
    CHECK_EQ(fixture.call(anna, "integrations.begin", badProvider).getString("code"),
             std::string("bad_request"));

    Json callback = Json::object();
    callback.set("provider", Json("google_calendar"));
    callback.set("code", Json("any-code"));
    callback.set("state", Json(state));

    // Чужой state недействителен.
    CHECK_EQ(fixture.call(oleg, "integrations.callback", callback).getString("code"),
             std::string("bad_request"));

    // Несовпадение провайдера недействительно.
    Json wrongProvider = callback;
    wrongProvider.set("provider", Json("google_gmail"));
    CHECK_EQ(fixture.call(anna, "integrations.callback", wrongProvider).getString("code"),
             std::string("bad_request"));

    // Эндаунт токенов недоступен → upstream_error, state сгорает (одноразовый).
    CHECK_EQ(fixture.call(anna, "integrations.callback", callback).getString("code"),
             std::string("upstream_error"));
    CHECK_EQ(fixture.call(anna, "integrations.callback", callback).getString("code"),
             std::string("bad_request"));

    ::unsetenv("SELFVOY_GOOGLE_CLIENT_ID");
    ::unsetenv("SELFVOY_GOOGLE_CLIENT_SECRET");
    ::unsetenv("SELFVOY_GOOGLE_TOKEN_URL");
}

TEST(integrations_state_expires) {
    ::setenv("SELFVOY_GOOGLE_CLIENT_ID", "test-client-id", 1);
    ::setenv("SELFVOY_GOOGLE_CLIENT_SECRET", "test-client-secret", 1);
    ::setenv("SELFVOY_OAUTH_STATE_TTL", "-5", 1);  // мгновенно истёкший state
    Fixture fixture;
    auto anna = fixture.makeSession("anna");
    fixture.registerUser(anna, "anna@example.com", "Анна");

    Json beginPayload = Json::object();
    beginPayload.set("provider", Json("google_gmail"));
    beginPayload.set("redirect_uri", Json("http://127.0.0.1:9999/callback"));
    const Json begin = fixture.call(anna, "integrations.begin", beginPayload);
    CHECK_EQ(begin.getString("type"), std::string("ok"));

    Json callback = Json::object();
    callback.set("provider", Json("google_gmail"));
    callback.set("code", Json("any-code"));
    callback.set("state", Json(begin.get("payload").getString("state")));
    const Json expired = fixture.call(anna, "integrations.callback", callback);
    CHECK_EQ(expired.getString("code"), std::string("bad_request"));

    ::unsetenv("SELFVOY_GOOGLE_CLIENT_ID");
    ::unsetenv("SELFVOY_GOOGLE_CLIENT_SECRET");
    ::unsetenv("SELFVOY_OAUTH_STATE_TTL");
}

TEST(integrations_database_roundtrip) {
    Fixture fixture;
    auto& db = fixture.server->database().db();

    selfvoy::IntegrationConnectionRecord record;
    record.userId = 7;
    record.provider = "google_calendar";
    record.account = "anna@gmail.com";
    record.scope = "https://www.googleapis.com/auth/calendar.events.readonly";
    record.tokenEncrypted = "cipher-blob";
    record.status = "active";
    long long id = 0;
    CHECK(db.upsertIntegrationConnection(record, id).ok);
    CHECK(id > 0);

    const auto found = db.findIntegrationConnection(7, "google_calendar");
    CHECK(found.has_value());
    CHECK_EQ(found->account, std::string("anna@gmail.com"));
    CHECK_EQ(found->tokenEncrypted, std::string("cipher-blob"));

    // Upsert по (user, provider) обновляет существующую запись.
    record.tokenEncrypted = "cipher-blob-2";
    record.account = "anna2@gmail.com";
    long long sameId = 0;
    CHECK(db.upsertIntegrationConnection(record, sameId).ok);
    CHECK_EQ(sameId, id);
    const auto updated = db.findIntegrationConnection(7, "google_calendar");
    CHECK_EQ(updated->tokenEncrypted, std::string("cipher-blob-2"));
    CHECK_EQ(db.listIntegrationConnections(7).size(), static_cast<std::size_t>(1));

    // Наружу токен не отдаётся.
    const Json json = updated->toJson();
    CHECK(!json.contains("token_encrypted"));
    CHECK_EQ(json.getString("provider"), std::string("google_calendar"));
    CHECK_EQ(updated->toJson(true).getString("token_encrypted"), std::string("cipher-blob-2"));

    CHECK(db.updateIntegrationToken(id, "cipher-3", "expired", "токен истёк").ok);
    CHECK_EQ(db.findIntegrationConnection(7, "google_calendar")->status, std::string("expired"));
    CHECK(db.touchIntegrationUsed(id).ok);
    CHECK(!db.findIntegrationConnection(7, "google_calendar")->lastUsedAt.empty());
    CHECK(db.deleteIntegrationConnection(id).ok);
    CHECK(!db.findIntegrationConnection(7, "google_calendar").has_value());
    CHECK(!db.deleteIntegrationConnection(id).ok);  // уже удалено

    // Одноразовые OAuth-состояния.
    selfvoy::OauthStateRecord state;
    state.state = "st-1";
    state.userId = 7;
    state.provider = "google_gmail";
    state.verifier = "verifier-1";
    state.redirectUri = "http://127.0.0.1:9999/cb";
    state.expiresAt = "2030-01-01T00:00:00Z";
    CHECK(db.createOauthState(state).ok);
    const auto loaded = db.findOauthState("st-1");
    CHECK(loaded.has_value());
    CHECK_EQ(loaded->verifier, std::string("verifier-1"));
    CHECK(loaded->usedAt.empty());
    CHECK(db.useOauthState("st-1").ok);
    CHECK(!db.findOauthState("st-1")->usedAt.empty());
    CHECK(!db.useOauthState("missing").ok);
    CHECK(!db.findOauthState("missing").has_value());
}

TEST(integrations_list_hides_secret) {
    Fixture fixture;
    auto anna = fixture.makeSession("anna");
    fixture.registerUser(anna, "anna@example.com", "Анна");

    selfvoy::IntegrationConnectionRecord record;
    record.userId = anna->userId();
    record.provider = "google_gmail";
    record.account = "anna@gmail.com";
    record.tokenEncrypted = "super-secret-cipher";
    long long id = 0;
    CHECK(fixture.server->database().db().upsertIntegrationConnection(record, id).ok);

    const Json listed = fixture.call(anna, "integrations.list");
    CHECK_EQ(listed.getString("type"), std::string("ok"));
    const std::string dump = listed.dump();
    CHECK(dump.find("super-secret-cipher") == std::string::npos);
    CHECK(dump.find("google_gmail") != std::string::npos);
    const Json connections = listed.get("payload").get("connections");
    CHECK_EQ(connections.size(), static_cast<std::size_t>(1));
    CHECK_EQ(connections.items()[0].getString("account"), std::string("anna@gmail.com"));
}

}  // namespace

// --- Этап 13: уведомления и push-устройства -------------------------------

TEST(server_notifications_flow) {
    Fixture fixture;
    auto session = fixture.makeSession("notify");
    fixture.registerUser(session, "nina@example.com", "Нина");

    // Вход внутри registerUser уже создал уведомление login.new.
    const Json listed = integration::payloadOf(fixture.call(session, "notifications.list"));
    CHECK(listed.getInt("unread") >= 1);
    const Json notifications = listed.get("notifications");
    bool sawLogin = false;
    long long loginId = 0;
    for (const auto& item : notifications.items()) {
        if (item.getString("kind") == std::string("login.new")) {
            sawLogin = true;
            loginId = item.getInt("id");
            CHECK_EQ(item.getString("title"), std::string("Новый вход в Selfvoy"));
            CHECK_EQ(item.getBool("read"), false);
        }
    }
    CHECK(sawLogin);

    // Пометить прочитанным: непрочитанных становится меньше, повтор — не ошибка.
    Json readOne = Json::object();
    readOne.set("id", Json(loginId));
    const Json afterRead = integration::payloadOf(fixture.call(session, "notifications.read", readOne));
    CHECK_EQ(afterRead.getInt("unread"), listed.getInt("unread") - 1);
    CHECK_EQ(fixture.call(session, "notifications.read", readOne).getString("type"), std::string("ok"));

    // Включение 2FA порождает уведомление безопасности.
    const Json setup = fixture.call(session, "auth.setup2fa");
    CHECK_EQ(setup.getString("type"), std::string("ok"));
    std::string rawSecret;
    CHECK(selfvoy::totp::decodeSecretBase32(setup.get("payload").getString("secret"), rawSecret));
    Json confirm = Json::object();
    confirm.set("code", Json(selfvoy::totp::codeNow(rawSecret)));
    CHECK_EQ(fixture.call(session, "auth.confirm2fa", confirm).getString("type"), std::string("ok"));

    // Фильтр непрочитанных видит twofactor.enabled.
    Json unreadReq = Json::object();
    unreadReq.set("unread", Json(true));
    const Json unreadList = integration::payloadOf(fixture.call(session, "notifications.list", unreadReq));
    // Именованная переменная: get() возвращает по значению, items() — ссылку
    // внутрь него (временный Json в range-for оставил бы ссылку висячей).
    const Json unreadItems = unreadList.get("notifications");
    bool saw2fa = false;
    for (const auto& item : unreadItems.items()) {
        if (item.getString("kind") == std::string("twofactor.enabled")) saw2fa = true;
    }
    CHECK(saw2fa);

    // «Прочитать все» (без id) очищает непрочитанные.
    CHECK_EQ(fixture.call(session, "notifications.read").getString("type"), std::string("ok"));
    const Json afterAll = integration::payloadOf(fixture.call(session, "notifications.list", unreadReq));
    CHECK_EQ(afterAll.getInt("unread"), 0);
    CHECK_EQ(afterAll.get("notifications").items().size(), std::size_t(0));

    // Изоляция: у другого пользователя только его собственные уведомления.
    auto other = fixture.makeSession("notify-other");
    fixture.registerUser(other, "oleg@example.com", "Олег");
    const Json otherList = integration::payloadOf(fixture.call(other, "notifications.list"));
    CHECK_EQ(otherList.get("notifications").items().size(), std::size_t(1));
    CHECK_EQ(otherList.get("notifications").at(0).getString("kind"), std::string("login.new"));
}

TEST(server_push_devices) {
    Fixture fixture;
    auto session = fixture.makeSession("push");
    fixture.registerUser(session, "push@example.com", "Пуш");

    // Валидная регистрация APNs-токена (64 hex-символа).
    Json reg = Json::object();
    reg.set("platform", Json("apns"));
    reg.set("token", Json(std::string(64, 'a')));
    const Json registered = integration::payloadOf(fixture.call(session, "devices.push.register", reg));
    CHECK(registered.getInt("id") > 0);
    const long long deviceId = registered.getInt("id");

    // Повторная регистрация того же токена — upsert, тот же id.
    const Json again = integration::payloadOf(fixture.call(session, "devices.push.register", reg));
    CHECK_EQ(again.getInt("id"), deviceId);

    // Список: устройство есть, токен наружу не отдаётся.
    const Json devices = integration::payloadOf(fixture.call(session, "devices.push.list"));
    CHECK_EQ(devices.get("devices").items().size(), std::size_t(1));
    CHECK(devices.get("devices").at(0).contains("token") == false);
    CHECK_EQ(devices.get("devices").at(0).getString("platform"), std::string("apns"));

    // Невалидные входные данные — bad_request.
    Json badPlatform = Json::object();
    badPlatform.set("platform", Json("sms"));
    badPlatform.set("token", Json(std::string(64, 'b')));
    CHECK_EQ(fixture.call(session, "devices.push.register", badPlatform).getString("code"),
             std::string("bad_request"));
    Json badToken = Json::object();
    badToken.set("platform", Json("apns"));
    badToken.set("token", Json(""));
    CHECK_EQ(fixture.call(session, "devices.push.register", badToken).getString("code"),
             std::string("bad_request"));
    // Токен попадает в путь запроса к шлюзу APNs — только hex.
    badToken.set("token", Json(std::string(64, 'a') + "/../x?"));
    CHECK_EQ(fixture.call(session, "devices.push.register", badToken).getString("code"),
             std::string("bad_request"));
    badToken.set("platform", Json("webhook"));
    badToken.set("token", Json("a b"));
    CHECK_EQ(fixture.call(session, "devices.push.register", badToken).getString("code"),
             std::string("bad_request"));
    // Не больше 20 устройств; уже известный токен регистрируется повторно.
    int accepted = 1;  // apns-устройство выше
    for (int i = 0; i < 25; ++i) {
        Json device = Json::object();
        device.set("platform", Json("webhook"));
        device.set("token", Json("cap-device-" + std::to_string(i)));
        if (fixture.call(session, "devices.push.register", device).getString("type") == "ok") ++accepted;
    }
    CHECK_EQ(accepted, 20);
    Json repeat = Json::object();
    repeat.set("platform", Json("webhook"));
    repeat.set("token", Json("cap-device-0"));
    CHECK_EQ(fixture.call(session, "devices.push.register", repeat).getString("type"), std::string("ok"));
    // Убираем добавленные устройства — дальше тест работает с одним apns.
    const Json all = integration::payloadOf(fixture.call(session, "devices.push.list")).get("devices");
    for (const auto& device : all.items()) {
        if (device.getInt("id") == deviceId) continue;
        Json drop = Json::object();
        drop.set("id", Json(device.getInt("id")));
        fixture.call(session, "devices.push.revoke", drop);
    }

    // Отзыв: чужое устройство — not_found, своё — revoked, повтор — not_found.
    auto other = fixture.makeSession("push-other");
    fixture.registerUser(other, "intruder@example.com", "Чужой");
    Json rev = Json::object();
    rev.set("id", Json(deviceId));
    CHECK_EQ(fixture.call(other, "devices.push.revoke", rev).getString("type"), std::string("error"));
    CHECK_EQ(fixture.call(other, "devices.push.revoke", rev).getString("code"), std::string("not_found"));
    const Json revoked = integration::payloadOf(fixture.call(session, "devices.push.revoke", rev));
    CHECK(revoked.getBool("revoked"));
    CHECK_EQ(fixture.call(session, "devices.push.revoke", rev).getString("code"), std::string("not_found"));
    CHECK_EQ(integration::payloadOf(fixture.call(session, "devices.push.list"))
                 .get("devices")
                 .items()
                 .size(),
             std::size_t(0));
}

TEST(server_onboarding_prefs) {
    // Онбординг-опрос после регистрации: новые поля настроек и флаг onboarded.
    Fixture fixture;
    auto session = fixture.makeSession("onboarding");
    fixture.registerUser(session, "survey@example.com", "Анкета");

    // У нового пользователя опрос не пройден.
    const Json defaults = integration::payloadOf(fixture.call(session, "prefs.get"));
    CHECK(defaults.getBool("onboarded") == false);
    CHECK_EQ(defaults.getString("birthday"), std::string(""));

    // Заполнение анкеты: день рождения, аллергии, диета, город.
    Json survey = Json::object();
    survey.set("birthday", Json("1995-04-18"));
    Json allergies = Json::array();
    allergies.push(Json("орехи"));
    allergies.push(Json("лактоза"));
    survey.set("allergies", allergies);
    Json diet = Json::array();
    diet.push(Json("vegetarian"));
    survey.set("diet", diet);
    survey.set("city", Json("Керкраде"));
    const Json saved = integration::payloadOf(fixture.call(session, "prefs.set", survey));
    CHECK_EQ(saved.getString("birthday"), std::string("1995-04-18"));
    CHECK_EQ(saved.get("allergies").size(), static_cast<std::size_t>(2));
    CHECK(saved.getBool("onboarded"));

    // Флаг устойчив: последующие правки настроек его не сбрасывают.
    Json theme = Json::object();
    theme.set("theme", Json("graphite-light"));
    const Json after = integration::payloadOf(fixture.call(session, "prefs.set", theme));
    CHECK(after.getBool("onboarded"));
    CHECK_EQ(after.getString("birthday"), std::string("1995-04-18"));

    // Анкета видна в контексте AI-сервиса (как остальные настройки).
    const Json context = fixture.server->memory().context(session->userId(), "спланируй ужин");
    CHECK_EQ(context.get("preferences").getString("birthday"), std::string("1995-04-18"));
}

TEST(server_push_devices_fcm) {
    // Этап 11 (Android): платформа 'fcm' принимается наравне с 'apns'.
    Fixture fixture;
    auto session = fixture.makeSession("push-fcm");
    fixture.registerUser(session, "android@example.com", "Андроид");

    Json reg = Json::object();
    reg.set("platform", Json("fcm"));
    reg.set("token", Json("fcm-device-token-01"));
    const Json registered = integration::payloadOf(fixture.call(session, "devices.push.register", reg));
    CHECK(registered.getInt("id") > 0);
    CHECK_EQ(registered.getString("platform"), std::string("fcm"));

    // Upsert по (user, token): повтор не плодит устройства.
    const Json again = integration::payloadOf(fixture.call(session, "devices.push.register", reg));
    CHECK_EQ(again.getInt("id"), registered.getInt("id"));

    const Json devices = integration::payloadOf(fixture.call(session, "devices.push.list"));
    CHECK_EQ(devices.get("devices").items().size(), std::size_t(1));
    CHECK_EQ(devices.get("devices").at(0).getString("platform"), std::string("fcm"));
    CHECK(devices.get("devices").at(0).contains("token") == false);
}

// --- Этап 14: сценарии безопасности -----------------------------------------

TEST(security_brute_force_throttle) {
    // Порог снижаем до 3, окно — 60 с (Fixture читает Config::fromEnv()).
    ::setenv("SELFVOY_MAX_LOGIN_ATTEMPTS", "3", 1);
    ::setenv("SELFVOY_MAX_IP_ATTEMPTS", "3", 1);
    ::setenv("SELFVOY_LOGIN_WINDOW", "60", 1);
    {
        Fixture fixture;
        auto session = fixture.makeSession("brute");
        fixture.registerUser(session, "brute@example.com", "Брут");

        Json login = Json::object();
        login.set("email", Json("brute@example.com"));
        login.set("password", Json("неверный-пароль"));
        for (int attempt = 0; attempt < 3; ++attempt) {
            const Json failed = fixture.call(session, "auth.login", login);
            CHECK_EQ(failed.getString("code"), std::string("unauthorized"));
        }

        // Превышен порог: даже верный пароль заблокирован до конца окна.
        login.set("password", Json("selfvoy1234"));
        const Json blocked = fixture.call(session, "auth.login", login);
        CHECK_EQ(blocked.getString("code"), std::string("forbidden"));

        // Регистрация под перебором тоже блокируется (тот же счётчик адреса).
        Json reg = Json::object();
        reg.set("email", Json("fresh@example.com"));
        reg.set("password", Json("selfvoy1234"));
        reg.set("display_name", Json("Фреш"));
        CHECK_EQ(fixture.call(session, "auth.register", reg).getString("code"),
                 std::string("forbidden"));
    }
    ::unsetenv("SELFVOY_MAX_LOGIN_ATTEMPTS");
    ::unsetenv("SELFVOY_MAX_IP_ATTEMPTS");
    ::unsetenv("SELFVOY_LOGIN_WINDOW");
}

TEST(security_injection_payloads_are_literal) {
    Fixture fixture;
    auto session = fixture.makeSession("inject");
    fixture.registerUser(session, "inject@example.com", "Инъекций");

    // SQL-инъекция в память: текст хранится и возвращается буквально.
    // kind — из CHECK схемы (fact|preference|schedule|contact).
    const std::string sqli = "'); DROP TABLE memories; -- <script>alert(1)</script>";
    Json add = Json::object();
    add.set("text", Json(sqli));
    add.set("kind", Json("fact"));
    CHECK_EQ(fixture.call(session, "memory.add", add).get("payload").getInt("saved"), 1);
    const Json entries =
        integration::payloadOf(fixture.call(session, "memory.list")).get("entries");
    bool foundLiteral = false;
    for (const auto& entry : entries.items()) {
        if (entry.getString("text") == sqli) foundLiteral = true;
    }
    CHECK(foundLiteral);

    // Задача с кавычками и точкой с запятой — round-trip без искажений.
    Json task = Json::object();
    task.set("title", Json("купить 'молоко'; удалить \"всё\""));
    const Json created = fixture.call(session, "tasks.create", task);
    CHECK_EQ(created.get("payload").getString("title"),
             std::string("купить 'молоко'; удалить \"всё\""));

    // Поиск пользователей с инъекцией: пустой результат, без падения.
    Json search = Json::object();
    search.set("query", Json("' OR '1'='1"));
    const Json found = fixture.call(session, "users.search", search);
    CHECK_EQ(found.getString("type"), std::string("ok"));
    CHECK_EQ(found.get("payload").get("users").items().size(), std::size_t(0));

    // Чужой email с инъекцией не входит (и не ломает хранилище).
    Json login = Json::object();
    login.set("email", Json("inject@example.com' OR '1'='1"));
    login.set("password", Json("selfvoy1234"));
    CHECK_EQ(fixture.call(session, "auth.login", login).getString("type"), std::string("error"));
}

TEST(security_xss_message_roundtrip_is_literal) {
    Fixture fixture;
    auto anna = fixture.makeSession("xss-anna");
    auto bob = fixture.makeSession("xss-bob");
    fixture.registerUser(anna, "xanna@example.com", "Анна");
    fixture.registerUser(bob, "xbob@example.com", "Боб");

    Json open = Json::object();
    open.set("contact", Json("xbob@example.com"));
    const Json opened = fixture.call(anna, "chat.open", open);
    const long long chat = integration::payloadOf(opened).get("chat").getInt("id");
    CHECK(chat > 0);

    // XSS-payload доставляется как текст: сервер ничего не исполняет и не
    // экранирует — клиенты (QML Text, SwiftUI Text) HTML не рендерят.
    const std::string xss = "<script>alert('xss')</script><img src=x onerror=alert(2)>";
    Json send = Json::object();
    send.set("chat_id", Json(chat));
    send.set("body", Json(xss));
    CHECK_EQ(fixture.call(anna, "chat.send", send).getString("type"), std::string("ok"));

    Json history = Json::object();
    history.set("chat_id", Json(chat));
    const Json messages =
        integration::payloadOf(fixture.call(bob, "chat.history", history)).get("messages");
    bool sawLiteral = false;
    for (const auto& message : messages.items()) {
        if (message.getString("body") == xss) sawLiteral = true;
    }
    CHECK(sawLiteral);
}

TEST(security_cross_user_isolation) {
    Fixture fixture;
    auto anna = fixture.makeSession("iso-anna");
    auto bob = fixture.makeSession("iso-bob");
    fixture.registerUser(anna, "isoanna@example.com", "Анна");
    fixture.registerUser(bob, "isobob@example.com", "Боб");

    // Память Анны не видна Бобу.
    Json add = Json::object();
    add.set("text", Json("Секретный рецепт Анны"));
    add.set("kind", Json("preference"));
    CHECK_EQ(fixture.call(anna, "memory.add", add).get("payload").getInt("saved"), 1);
    const Json bobMemory =
        integration::payloadOf(fixture.call(bob, "memory.list")).get("entries");
    CHECK_EQ(bobMemory.items().size(), std::size_t(0));

    // Настройки Анны не видны Бобу (у него свои значения по умолчанию).
    Json prefs = Json::object();
    prefs.set("city", Json("Керкраде"));
    fixture.call(anna, "prefs.set", prefs);
    CHECK_EQ(integration::payloadOf(fixture.call(bob, "prefs.get")).getString("city"),
             std::string(""));

    // Задачи и уведомления тоже изолированы.
    Json task = Json::object();
    task.set("title", Json("Задача Анны"));
    fixture.call(anna, "tasks.create", task);
    CHECK_EQ(integration::payloadOf(fixture.call(bob, "tasks.list"))
                 .get("tasks")
                 .items()
                 .size(),
             std::size_t(0));
    CHECK_EQ(integration::payloadOf(fixture.call(bob, "notifications.list"))
                 .get("notifications")
                 .items()
                 .size(),
             std::size_t(1));  // только его собственный login.new

    // Чужой чат недоступен: у Анны есть чат с Бобом, список у обоих по одному.
    Json open = Json::object();
    open.set("contact", Json("isobob@example.com"));
    const Json annaChat = fixture.call(anna, "chat.open", open);
    CHECK_EQ(annaChat.getString("type"), std::string("ok"));
    const Json list = integration::payloadOf(fixture.call(anna, "chat.list"));
    CHECK_EQ(list.get("chats").items().size(), std::size_t(1));
    const Json bobList = integration::payloadOf(fixture.call(bob, "chat.list"));
    CHECK_EQ(bobList.get("chats").items().size(), std::size_t(1));
}

TEST(security_sessions_list_hides_secrets) {
    Fixture fixture;
    auto session = fixture.makeSession("leak");
    fixture.registerUser(session, "leak@example.com", "Лик");

    const Json sessions =
        integration::payloadOf(fixture.call(session, "sessions.list")).get("sessions");
    CHECK(sessions.items().size() >= 1);
    for (const auto& item : sessions.items()) {
        // Ни токена, ни его хэша, ни jwt_id наружу.
        CHECK(item.contains("token") == false);
        CHECK(item.contains("token_hash") == false);
        CHECK(item.contains("jwt_id") == false);
        CHECK(item.contains("refresh_token") == false);
        CHECK(!item.getString("id").empty());
    }
}

// ===================================================== аудит безопасности (C1–L24)
namespace audit {

// Кадр клиента с маской (RFC 6455 §5.3) — как шлёт браузер.
std::string maskedFrame(const std::string& payload, unsigned char opcode = 0x1) {
    std::string frame;
    frame.push_back(static_cast<char>(0x80 | opcode));
    const std::size_t size = payload.size();
    if (size < 126) {
        frame.push_back(static_cast<char>(0x80 | size));
    } else if (size < 65536) {
        frame.push_back(static_cast<char>(0x80 | 126));
        frame.push_back(static_cast<char>((size >> 8) & 0xFF));
        frame.push_back(static_cast<char>(size & 0xFF));
    } else {
        frame.push_back(static_cast<char>(0x80 | 127));
        for (int i = 7; i >= 0; --i) frame.push_back(static_cast<char>((static_cast<unsigned long long>(size) >> (i * 8)) & 0xFF));
    }
    const char mask[4] = {0x11, 0x22, 0x33, 0x44};
    frame.append(mask, 4);
    for (std::size_t i = 0; i < size; ++i) frame.push_back(static_cast<char>(payload[i] ^ mask[i % 4]));
    return frame;
}

Json loginPayload(const std::string& email, const std::string& password) {
    Json login = Json::object();
    login.set("email", Json(email));
    login.set("password", Json(password));
    return login;
}

}  // namespace audit

TEST(audit_c1_c2_config_validation) {
    std::vector<std::string> warnings;
    std::string error;

    // C1: драйвер почты по умолчанию — log; dev без SELFVOY_DEV_MODE не стартует.
    selfvoy::Config defaults;
    CHECK_EQ(defaults.mailDriver, std::string("log"));
    CHECK(defaults.jwtSecret.empty());
    CHECK(!defaults.devMode);

    selfvoy::Config prod;
    prod.jwtSecret = std::string(48, 'k') + "random";
    prod.twoFactorKey = "0123456789abcdef0123456789abcdef";
    CHECK(prod.validate(error, warnings));
    prod.mailDriver = "dev";
    error.clear();
    CHECK(!prod.validate(error, warnings));
    CHECK(error.find("SELFVOY_DEV_MODE") != std::string::npos);

    // Сервер с такой конфигурацией не стартует.
    selfvoy::Server refused(prod);
    error.clear();
    CHECK(!refused.start(error));
    CHECK(error.find("SELFVOY_DEV_MODE") != std::string::npos);

    // C2: слабые/пустые/шаблонные секреты и отсутствие ключа 2FA вне dev — отказ.
    for (const std::string secret : {std::string(""), std::string("selfvoy-dev-secret"), std::string("short-secret"),
                                     std::string("CHANGE-IN-PRODUCTION-0123456789abcdefghijklmnop")}) {
        selfvoy::Config weak;
        weak.jwtSecret = secret;
        weak.twoFactorKey = "0123456789abcdef0123456789abcdef";
        error.clear();
        CHECK(!weak.validate(error, warnings));
        CHECK(error.find("SELFVOY_JWT_SECRET") != std::string::npos);
    }
    selfvoy::Config noTwoFactor;
    noTwoFactor.jwtSecret = std::string(64, 'x');
    error.clear();
    CHECK(!noTwoFactor.validate(error, warnings));
    CHECK(error.find("SELFVOY_2FA_KEY") != std::string::npos);

    // В dev-режиме без секрета — случайный секрет на процесс (с предупреждением).
    selfvoy::Config dev;
    dev.devMode = true;
    dev.mailDriver = "dev";
    warnings.clear();
    error.clear();
    CHECK(dev.validate(error, warnings));
    CHECK_EQ(dev.jwtSecret.size(), static_cast<std::size_t>(64));
    CHECK(!warnings.empty());
    selfvoy::Config dev2;
    dev2.devMode = true;
    CHECK(dev2.validate(error, warnings));
    CHECK(dev.jwtSecret != dev2.jwtSecret);

    // Код сброса живёт не дольше часа.
    selfvoy::Config longReset = prod;
    longReset.mailDriver = "log";
    longReset.resetTtlSeconds = 86400;
    CHECK(longReset.validate(error, warnings));
    CHECK_EQ(longReset.resetTtlSeconds, 3600LL);
}

// Переименование Aura -> Selfvoy: старые AURA_* читаются один релиз, если SELFVOY_* не задана.
TEST(config_legacy_aura_env_fallback) {
    ::unsetenv("SELFVOY_PORT");
    ::unsetenv("SELFVOY_LOG_LEVEL");
    ::setenv("AURA_PORT", "9123", 1);
    ::setenv("AURA_LOG_LEVEL", "debug", 1);
    selfvoy::Config legacy = selfvoy::Config::fromEnv();
    CHECK_EQ(legacy.port, 9123);
    CHECK_EQ(legacy.logLevel, std::string("debug"));
    CHECK_EQ(legacy.legacyEnvNames.size(), static_cast<std::size_t>(2));
    CHECK(std::find(legacy.legacyEnvNames.begin(), legacy.legacyEnvNames.end(), "AURA_PORT") !=
          legacy.legacyEnvNames.end());

    // Одно предупреждение со списком старых имён.
    legacy.jwtSecret = std::string(48, 'k') + "random";
    legacy.twoFactorKey = "0123456789abcdef0123456789abcdef";
    std::vector<std::string> warnings;
    std::string error;
    CHECK(legacy.validate(error, warnings));
    std::size_t deprecations = 0;
    for (const auto& warning : warnings) {
        if (warning.find("AURA_PORT") != std::string::npos) {
            ++deprecations;
            CHECK(warning.find("AURA_LOG_LEVEL") != std::string::npos);
        }
    }
    CHECK_EQ(deprecations, static_cast<std::size_t>(1));

    // Если заданы обе — побеждает SELFVOY_*, старое имя не считается использованным.
    ::setenv("SELFVOY_PORT", "9456", 1);
    selfvoy::Config both = selfvoy::Config::fromEnv();
    CHECK_EQ(both.port, 9456);
    CHECK(std::find(both.legacyEnvNames.begin(), both.legacyEnvNames.end(), "AURA_PORT") ==
          both.legacyEnvNames.end());
    CHECK_EQ(selfvoy::envOr("SELFVOY_LOG_LEVEL", "info"), std::string("debug"));

    ::unsetenv("SELFVOY_PORT");
    ::unsetenv("AURA_PORT");
    ::unsetenv("AURA_LOG_LEVEL");
    selfvoy::Config clean = selfvoy::Config::fromEnv();
    CHECK_EQ(clean.port, 9000);
    CHECK(clean.legacyEnvNames.empty());
}

TEST(audit_c1_log_driver_hides_codes) {
    selfvoy::Config config;
    config.mailDriver = "log";
    CHECK(!selfvoy::makeMailProvider(config)->exposesCodes());
    // dev без devMode тоже не раскрывает коды (вторая линия защиты).
    config.mailDriver = "dev";
    CHECK(!selfvoy::makeMailProvider(config)->exposesCodes());
    config.devMode = true;
    CHECK(selfvoy::makeMailProvider(config)->exposesCodes());
}

TEST(audit_c2_forged_sub_rejected) {
    Fixture fixture;
    auto victim = fixture.makeSession("victim");
    fixture.registerUser(victim, "victim@example.com", "Жертва");
    auto mallory = fixture.makeSession("mallory");
    const Json logged = fixture.registerUser(mallory, "mallory@example.com", "Мэллори");
    const std::string token = integration::payloadOf(logged).getString("token");

    // Токен с собственным живым jti, но sub жертвы (подписан тем же секретом).
    const std::string payloadPart = token.substr(token.find('.') + 1, token.rfind('.') - token.find('.') - 1);
    std::string claimsRaw;
    std::string padded = payloadPart;
    for (auto& ch : padded) {
        if (ch == '-') ch = '+';
        if (ch == '_') ch = '/';
    }
    while (padded.size() % 4) padded.push_back('=');
    CHECK(selfvoy::crypto::base64Decode(padded, claimsRaw));
    Json claims = Json::parse(claimsRaw);
    claims.set("sub", Json(victim->userId()));
    claims.set("email", Json("victim@example.com"));
    claims.erase("exp");
    claims.erase("iat");
    const std::string forged = selfvoy::jwt::sign(claims, fixture.config.jwtSecret, 3600);

    auto attacker = fixture.makeSession("attacker");
    Json auth = Json::object();
    auth.set("token", Json(forged));
    CHECK_EQ(fixture.call(attacker, "auth.token", auth).getString("code"), std::string("unauthorized"));
    CHECK(!attacker->authenticated());
    // Неподделанный токен по-прежнему работает.
    auth.set("token", Json(token));
    CHECK_EQ(fixture.call(attacker, "auth.token", auth).getString("type"), std::string("ok"));
    CHECK_EQ(attacker->userId(), mallory->userId());
}

TEST(audit_h3_json_many_keys_and_preauth_frame) {
    // 90 000 ключей в одном объекте: разбор отклоняется быстро (лимит ключей).
    std::string text = "{";
    for (int i = 0; i < 90000; ++i) {
        if (i) text += ",";
        text += "\"" + std::to_string(100000 + i) + "\":0";
    }
    text += "}";
    const auto started = std::chrono::steady_clock::now();
    std::string error;
    CHECK(Json::parse(text, &error).isNull());
    CHECK(error.find("ключей") != std::string::npos);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    CHECK(elapsed < 1000);

    // Дубли ключей: последнее значение побеждает, порядок — по первому вхождению.
    const Json dup = Json::parse(R"({"a":1,"b":2,"a":3})");
    CHECK_EQ(dup.getInt("a"), 3);
    CHECK_EQ(dup.keys().size(), static_cast<std::size_t>(2));
    CHECK_EQ(dup.keys().front(), std::string("a"));

    // Ровно на лимите — разбирается.
    std::string ok = "{";
    for (std::size_t i = 0; i < Json::kMaxObjectKeys; ++i) {
        if (i) ok += ",";
        ok += "\"k" + std::to_string(i) + "\":" + std::to_string(i);
    }
    ok += "}";
    CHECK_EQ(Json::parse(ok).keys().size(), Json::kMaxObjectKeys);

    // До входа кадр больше SELFVOY_MAX_PREAUTH_FRAME закрывает соединение.
    selfvoy::Config config;
    config.maxPreAuthFrameBytes = 1024;
    config.authTimeoutSec = 0;
    int fds[2] = {-1, -1};
    CHECK_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    auto session = std::make_shared<selfvoy::Session>(fds[1], "big", "127.0.0.1:1", config);
    bool handled = false;
    std::thread reader([&] { session->readLoop([&](std::shared_ptr<selfvoy::Session>, const Json&) { handled = true; }); });
    const std::string frame = audit::maskedFrame(R"({"type":"ping","payload":{"x":")" + std::string(4000, 'a') + "\"}}");
    CHECK(::send(fds[0], frame.data(), frame.size(), 0) > 0);
    reader.join();
    CHECK(!handled);
    CHECK(!session->alive());
    ::close(fds[0]);
}

TEST(audit_h4_argon2_gate_and_register_limits) {
    // Параллельные Argon2id ограничены числом ядер.
    selfvoy::crypto::resetArgon2Peak();
    const int limit = selfvoy::crypto::argon2Stats().limit;
    CHECK(limit >= 1);
    std::vector<std::thread> workers;
    for (int i = 0; i < limit + 3; ++i) {
        workers.emplace_back([] { selfvoy::crypto::hashPassword("pass1234"); });
    }
    for (auto& worker : workers) worker.join();
    CHECK(selfvoy::crypto::argon2Stats().peak <= limit);
    CHECK_EQ(selfvoy::crypto::argon2Stats().inFlight, 0);

    ::setenv("SELFVOY_REGISTER_PER_IP_HOUR", "2", 1);
    {
        Fixture fixture;
        auto session = fixture.makeSession("reg");
        Json reg = Json::object();
        reg.set("password", Json("selfvoy1234"));
        reg.set("email", Json("r1@example.com"));
        CHECK_EQ(fixture.call(session, "auth.register", reg).getString("type"), std::string("ok"));
        // Дубликат отсекается до хэширования и не расходует лимит регистраций.
        CHECK_EQ(fixture.call(session, "auth.register", reg).getString("code"), std::string("conflict"));
        reg.set("email", Json("r2@example.com"));
        CHECK_EQ(fixture.call(session, "auth.register", reg).getString("type"), std::string("ok"));
        // Третья регистрация с того же IP (другой порт) за час — отказ.
        auto other = std::make_shared<selfvoy::Session>(-1, "reg2", "127.0.0.1:5555", fixture.config);
        reg.set("email", Json("r3@example.com"));
        const Json third = fixture.call(other, "auth.register", reg);
        CHECK_EQ(third.getString("code"), std::string("forbidden"));
    }
    ::unsetenv("SELFVOY_REGISTER_PER_IP_HOUR");
}

TEST(audit_h5_verify_code_attempts) {
    Fixture fixture;
    auto session = fixture.makeSession("verify");
    Json reg = Json::object();
    reg.set("email", Json("squat@example.com"));
    reg.set("password", Json("selfvoy1234"));
    const std::string code = integration::payloadOf(fixture.call(session, "auth.register", reg)).getString("email_code");
    CHECK_EQ(code.size(), static_cast<std::size_t>(6));

    Json verify = Json::object();
    verify.set("email", Json("squat@example.com"));
    for (int i = 0; i < 5; ++i) {
        verify.set("code", Json(code == "111111" ? std::string("222222") : std::string("111111")));
        CHECK_EQ(fixture.call(session, "auth.verifyEmail", verify).getString("code"), std::string("bad_request"));
    }
    // После 5 неверных попыток настоящий код сгорел.
    verify.set("code", Json(code));
    CHECK_EQ(fixture.call(session, "auth.verifyEmail", verify).getString("code"), std::string("bad_request"));
    // Новый код (resendCode) работает.
    Json resend = Json::object();
    resend.set("email", Json("squat@example.com"));
    const std::string fresh = integration::payloadOf(fixture.call(session, "auth.resendCode", resend)).getString("email_code");
    CHECK_EQ(fresh.size(), static_cast<std::size_t>(6));
    verify.set("code", Json(fresh));
    CHECK(fixture.call(session, "auth.verifyEmail", verify).get("payload").getBool("verified"));
}

TEST(audit_h6_totp_bruteforce_and_ip_key) {
    CHECK_EQ(selfvoy::Session::ipOf("127.0.0.1:54321"), std::string("127.0.0.1"));
    CHECK_EQ(selfvoy::Session::ipOf("2001:db8::1:443"), std::string("2001:db8::1"));
    CHECK_EQ(selfvoy::Session::ipOf("10.0.0.1"), std::string("10.0.0.1"));

    Fixture fixture;
    auto owner = fixture.makeSession("erin");
    fixture.registerUser(owner, "erin@example.com", "Эрин");
    const std::string secret = integration::payloadOf(fixture.call(owner, "auth.setup2fa")).getString("secret");
    std::string raw;
    CHECK(selfvoy::totp::decodeSecretBase32(secret, raw));
    Json confirm = Json::object();
    confirm.set("code", Json(selfvoy::totp::codeNow(raw)));
    CHECK_EQ(fixture.call(owner, "auth.confirm2fa", confirm).getString("type"), std::string("ok"));

    // H7: повторный setup2fa при включённой 2FA — отказ, 2FA остаётся включённой.
    CHECK_EQ(fixture.call(owner, "auth.setup2fa").getString("code"), std::string("bad_request"));
    CHECK(fixture.call(owner, "auth.status2fa").get("payload").getBool("enabled"));

    // Перебор TOTP с переподключениями и «верным паролем» между попытками.
    const std::string valid = selfvoy::totp::codeNow(raw);
    Json attempt = audit::loginPayload("erin@example.com", "selfvoy1234");
    int wrong = 0;
    for (int i = 0; i < 5; ++i) {
        auto attacker = std::make_shared<selfvoy::Session>(-1, "atk", "127.0.0.1:" + std::to_string(40000 + i), fixture.config);
        CHECK_EQ(fixture.call(attacker, "auth.login", attempt).getString("code"), std::string("requires_2fa"));
        Json guess = attempt;
        guess.set("code", Json(valid == "000000" ? std::string("000001") : std::string("000000")));
        if (fixture.call(attacker, "auth.login2fa", guess).getString("code") == "unauthorized") ++wrong;
    }
    CHECK_EQ(wrong, 5);
    // Лимит исчерпан: даже верный код отклоняется до конца окна.
    auto late = std::make_shared<selfvoy::Session>(-1, "late", "127.0.0.1:49999", fixture.config);
    const std::int64_t next = (std::time(nullptr) / selfvoy::totp::kPeriod + 1) * selfvoy::totp::kPeriod;
    Json good = attempt;
    good.set("code", Json(selfvoy::totp::codeAt(raw, next)));
    CHECK_EQ(fixture.call(late, "auth.login2fa", good).getString("code"), std::string("forbidden"));
}

TEST(audit_h8_open_connections_follow_revocation) {
    Fixture fixture;
    auto first = fixture.makeSession("first");
    fixture.registerUser(first, "bob@example.com", "Боб");
    // Второе соединение не через makeSession (иначе закроется сокет первого).
    auto second = std::make_shared<selfvoy::Session>(-1, "second", "127.0.0.1:2", fixture.config);
    CHECK_EQ(fixture.call(second, "auth.login", audit::loginPayload("bob@example.com", "selfvoy1234")).getString("type"),
             std::string("ok"));
    CHECK_EQ(fixture.call(first, "auth.me").getString("type"), std::string("ok"));

    // sessions.revokeAll со второго: первое соединение выбито сразу.
    CHECK_EQ(fixture.call(second, "sessions.revokeAll").getString("type"), std::string("ok"));
    CHECK(!first->authenticated());
    CHECK_EQ(fixture.call(first, "auth.me").getString("code"), std::string("unauthorized"));
    Json task = Json::object();
    task.set("title", Json("still here"));
    CHECK_EQ(fixture.call(first, "tasks.create", task).getString("code"), std::string("unauthorized"));
    CHECK(second->authenticated());

    // Отзыв в обход сервера (другой экземпляр/администратор): соединение
    // перепроверяется по БД не реже SELFVOY_AUTH_RECHECK.
    fixture.server->database().db().revokeSession(second->jwtId());
    CHECK_EQ(fixture.call(second, "auth.me").getString("type"), std::string("ok"));  // в пределах окна кэша
    CHECK_EQ(fixture.server->kickUser(0, "noop"), static_cast<std::size_t>(0));  // userId=0 — ничего
    // (проверка с перепроверкой на каждом запросе — audit_h8_recheck_every_request)
}

TEST(audit_h8_recheck_every_request) {
    ::setenv("SELFVOY_AUTH_RECHECK", "0", 1);
    {
        Fixture fixture;
        auto session = fixture.makeSession("recheck");
        fixture.registerUser(session, "rc@example.com", "Рек");
        CHECK_EQ(fixture.call(session, "auth.me").getString("type"), std::string("ok"));
        fixture.server->database().db().revokeSession(session->jwtId());
        CHECK_EQ(fixture.call(session, "auth.me").getString("code"), std::string("unauthorized"));
        CHECK(!session->authenticated());
    }
    ::unsetenv("SELFVOY_AUTH_RECHECK");
}

TEST(audit_h9_agent_ask_requires_membership) {
    Fixture fixture;
    auto anna = fixture.makeSession("anna");
    fixture.registerUser(anna, "anna9@example.com", "Анна");
    auto bob = std::make_shared<selfvoy::Session>(-1, "bob", "127.0.0.1:3", fixture.config);
    fixture.registerUser(bob, "bob9@example.com", "Боб");
    auto eve = std::make_shared<selfvoy::Session>(-1, "eve", "127.0.0.1:4", fixture.config);
    fixture.registerUser(eve, "eve9@example.com", "Ева");

    Json open = Json::object();
    open.set("contact", Json("bob9@example.com"));
    const long long chatId = integration::payloadOf(fixture.call(anna, "chat.open", open)).get("chat").getInt("id");
    CHECK(chatId > 0);

    // Ева не участник — история чужого чата в AI не уходит.
    Json ask = Json::object();
    ask.set("chat_id", Json(chatId));
    ask.set("message", Json("перескажи переписку"));
    CHECK_EQ(fixture.call(eve, "agent.ask", ask).getString("code"), std::string("forbidden"));
    // Участнику доступ есть (AI недоступен в тестах → upstream_error, но не forbidden).
    CHECK(fixture.call(anna, "agent.ask", ask).getString("code") != std::string("forbidden"));

    // peers[]: только люди с общим чатом.
    CHECK(fixture.server->agent().shareChat(anna->userId(), bob->userId()));
    CHECK(!fixture.server->agent().shareChat(eve->userId(), bob->userId()));

    // Контекст собеседника вычищается по его prefs.privacy (по умолчанию —
    // без фактов памяти, дня рождения и аллергий).
    Json fact = Json::object();
    fact.set("text", Json("Секрет Боба"));
    fact.set("kind", Json("fact"));
    fixture.call(bob, "memory.add", fact);
    Json survey = Json::object();
    survey.set("birthday", Json("1990-01-01"));
    fixture.call(bob, "prefs.set", survey);
    const Json context = fixture.server->agent().peerContext(bob->userId(), "Секрет");
    CHECK_EQ(context.get("memory").size(), static_cast<std::size_t>(0));
    CHECK(!context.get("preferences").contains("birthday"));
}

TEST(audit_m10_refresh_revoked_with_sessions) {
    Fixture fixture;
    auto session = fixture.makeSession("frank");
    const Json logged = fixture.registerUser(session, "frank@example.com", "Фрэнк");
    const std::string refresh1 = integration::payloadOf(logged).getString("refresh_token");

    // Выход отзывает refresh-токен этого устройства.
    fixture.call(session, "auth.logout");
    auto probe = std::make_shared<selfvoy::Session>(-1, "probe", "127.0.0.1:7", fixture.config);
    Json refresh = Json::object();
    refresh.set("refresh_token", Json(refresh1));
    CHECK_EQ(fixture.call(probe, "auth.refresh", refresh).getString("code"), std::string("unauthorized"));

    // revokeAll с устройства B: refresh устройства A мёртв, своего — жив.
    auto deviceA = std::make_shared<selfvoy::Session>(-1, "A", "127.0.0.1:8", fixture.config);
    auto deviceB = std::make_shared<selfvoy::Session>(-1, "B", "127.0.0.1:9", fixture.config);
    const std::string refreshA = integration::payloadOf(
        fixture.call(deviceA, "auth.login", audit::loginPayload("frank@example.com", "selfvoy1234"))).getString("refresh_token");
    const std::string refreshB = integration::payloadOf(
        fixture.call(deviceB, "auth.login", audit::loginPayload("frank@example.com", "selfvoy1234"))).getString("refresh_token");
    CHECK_EQ(fixture.call(deviceB, "sessions.revokeAll").getString("type"), std::string("ok"));
    auto probe2 = std::make_shared<selfvoy::Session>(-1, "probe2", "127.0.0.1:10", fixture.config);
    refresh.set("refresh_token", Json(refreshA));
    CHECK_EQ(fixture.call(probe2, "auth.refresh", refresh).getString("code"), std::string("unauthorized"));
    refresh.set("refresh_token", Json(refreshB));
    const Json rotated = fixture.call(deviceB, "auth.refresh", refresh);
    CHECK_EQ(rotated.getString("type"), std::string("ok"));

    // sessions.revoke отзывает и refresh-семейство этой сессии.
    auto deviceC = std::make_shared<selfvoy::Session>(-1, "C", "127.0.0.1:11", fixture.config);
    const Json loginC = fixture.call(deviceC, "auth.login", audit::loginPayload("frank@example.com", "selfvoy1234"));
    const std::string refreshC = integration::payloadOf(loginC).getString("refresh_token");
    Json revoke = Json::object();
    revoke.set("session_id", Json(integration::payloadOf(loginC).getString("session_id")));
    CHECK(fixture.call(deviceB, "sessions.revoke", revoke).get("payload").getBool("revoked"));
    CHECK(!deviceC->authenticated());  // открытое соединение отозванной сессии закрыто
    refresh.set("refresh_token", Json(refreshC));
    CHECK_EQ(fixture.call(probe2, "auth.refresh", refresh).getString("code"), std::string("unauthorized"));
}

TEST(audit_m12_send_email_header_injection) {
    Fixture fixture;
    auto& tools = fixture.server->tools();
    Json args = Json::object();
    args.set("to", Json("friend@example.com\r\nBcc: evil@example.com"));
    args.set("body", Json("hi"));
    CHECK(!tools.sendEmail(1, args).ok);
    args.set("to", Json("friend@example.com, evil@example.com"));
    CHECK(!tools.sendEmail(1, args).ok);
    args.set("to", Json("friend@example.com"));
    args.set("subject", Json("тема\r\nBcc: evil@example.com"));
    CHECK(!tools.sendEmail(1, args).ok);
    args.set("subject", Json("Обычная тема"));
    CHECK(tools.sendEmail(1, args).ok);
}

TEST(audit_m13_mail_per_target_limit) {
    Fixture fixture;
    auto session = fixture.makeSession("mail");
    fixture.registerUser(session, "target@example.com", "Цель");
    Json forgot = Json::object();
    forgot.set("email", Json("target@example.com"));
    int issued = 0;
    for (int i = 0; i < 5; ++i) {
        // Каждый раз новое соединение (другой порт): лимит на адрес получателя.
        auto client = std::make_shared<selfvoy::Session>(-1, "m", "127.0.0.1:" + std::to_string(30000 + i), fixture.config);
        const Json reply = fixture.call(client, "auth.forgotPassword", forgot);
        CHECK_EQ(reply.getString("type"), std::string("ok"));  // ответ одинаковый
        if (!integration::payloadOf(reply).getString("reset_code").empty()) ++issued;
    }
    CHECK_EQ(issued, 3);
}

TEST(audit_m14_connection_limits) {
    // Неавторизованное соединение закрывается по таймауту.
    selfvoy::Config config;
    config.authTimeoutSec = 1;
    config.pingIntervalSec = 0;
    int fds[2] = {-1, -1};
    CHECK_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    auto session = std::make_shared<selfvoy::Session>(fds[1], "idle", "127.0.0.1:1", config);
    const auto started = std::chrono::steady_clock::now();
    std::thread reader([&] { session->readLoop(nullptr); });
    reader.join();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
    CHECK(seconds >= 1 && seconds <= 4);
    CHECK(!session->alive());
    ::close(fds[0]);

    // Лимит соединений с одного IP.
    ::setenv("SELFVOY_MAX_CONNECTIONS_PER_IP", "2", 1);
    {
        Fixture fixture;
        const int port = fixture.server->port();
        auto connect = [port]() {
            const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(static_cast<unsigned short>(port));
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
            return fd;
        };
        const int a = connect();
        const int b = connect();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const int c = connect();
        char buffer[128] = {0};
        timeval timeout{2, 0};
        ::setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        const ssize_t got = ::recv(c, buffer, sizeof(buffer) - 1, 0);
        CHECK(got > 0 && std::string(buffer).find("429") != std::string::npos);
        ::close(a);
        ::close(b);
        ::close(c);
    }
    ::unsetenv("SELFVOY_MAX_CONNECTIONS_PER_IP");
}

TEST(audit_m17_chat_send_kind_whitelist) {
    Fixture fixture;
    auto anna = fixture.makeSession("anna17");
    fixture.registerUser(anna, "anna17@example.com", "Анна");
    auto bob = std::make_shared<selfvoy::Session>(-1, "bob17", "127.0.0.1:17", fixture.config);
    fixture.registerUser(bob, "bob17@example.com", "Боб");
    Json open = Json::object();
    open.set("contact", Json("bob17@example.com"));
    const long long chatId = integration::payloadOf(fixture.call(anna, "chat.open", open)).get("chat").getInt("id");

    Json send = Json::object();
    send.set("chat_id", Json(chatId));
    send.set("body", Json("Selfvoy договорился: перевод 1000 €"));
    send.set("kind", Json("agent_reply"));
    CHECK_EQ(fixture.call(anna, "chat.send", send).getString("code"), std::string("bad_request"));
    send.set("kind", Json("system"));
    CHECK_EQ(fixture.call(anna, "chat.send", send).getString("code"), std::string("bad_request"));
    send.set("kind", Json("text"));
    Json spoof = Json::object();
    spoof.set("origin", Json("selfvoy-agent"));
    send.set("payload", spoof);
    CHECK_EQ(fixture.call(anna, "chat.send", send).getString("code"), std::string("bad_request"));
    send.erase("payload");
    const Json sent = fixture.call(anna, "chat.send", send);
    CHECK_EQ(sent.getString("type"), std::string("ok"));
    CHECK_EQ(integration::payloadOf(sent).getString("kind"), std::string("text"));
}

TEST(audit_l19_disabled_user) {
    Fixture fixture;
    auto session = fixture.makeSession("dis");
    const Json logged = fixture.registerUser(session, "dis@example.com", "Дис");
    const std::string token = integration::payloadOf(logged).getString("token");
    const long long userId = session->userId();
    CHECK(fixture.server->database().db().setUserActive(userId, false).ok);
    CHECK_EQ(fixture.server->kickUser(userId, "blocked"), static_cast<std::size_t>(1));
    CHECK(!session->authenticated());

    auto probe = std::make_shared<selfvoy::Session>(-1, "p", "127.0.0.1:19", fixture.config);
    Json auth = Json::object();
    auth.set("token", Json(token));
    CHECK_EQ(fixture.call(probe, "auth.token", auth).getString("code"), std::string("unauthorized"));
    // Неверный пароль не раскрывает блокировку, верный — «аккаунт отключён».
    const Json wrong = fixture.call(probe, "auth.login", audit::loginPayload("dis@example.com", "wrongpass1"));
    CHECK_EQ(wrong.getString("code"), std::string("unauthorized"));
    CHECK(wrong.getString("message").find("отключ") == std::string::npos);
    const Json right = fixture.call(probe, "auth.login", audit::loginPayload("dis@example.com", "selfvoy1234"));
    CHECK_EQ(right.getString("code"), std::string("forbidden"));
}

TEST(audit_l21_races_are_atomic) {
    Fixture fixture;
    auto session = fixture.makeSession("race");
    const Json logged = fixture.registerUser(session, "race@example.com", "Гонка");
    auto& db = fixture.server->database().db();
    const long long userId = session->userId();

    // Двойное подтверждение: исполняет только первый claim.
    selfvoy::PendingActionRecord pending;
    pending.userId = userId;
    pending.tool = "create_note";
    pending.args.set("text", Json("заметка"));
    const long long id = db.createPendingAction(pending);
    CHECK(db.claimPendingAction(id, userId, "approved"));
    CHECK(!db.claimPendingAction(id, userId, "approved"));
    const long long other = db.createPendingAction(pending);
    CHECK(!db.claimPendingAction(other, userId + 1000, "approved"));  // чужое

    // Одновременный refresh: второй отзыв того же токена не проходит.
    const std::string refresh = integration::payloadOf(logged).getString("refresh_token");
    const auto record = db.findRefreshToken(selfvoy::crypto::toHex(selfvoy::crypto::sha256(refresh)));
    CHECK(record.has_value());
    CHECK(db.revokeRefreshToken(record->id, "").ok);
    CHECK(!db.revokeRefreshToken(record->id, "").ok);

    // tasks.due обрабатывает только свои напоминания.
    auto other_user = std::make_shared<selfvoy::Session>(-1, "o", "127.0.0.1:21", fixture.config);
    fixture.registerUser(other_user, "race2@example.com", "Гонка2");
    Json task = Json::object();
    task.set("title", Json("чужое"));
    task.set("remind_at", Json("2020-01-01T00:00:00.000Z"));
    fixture.call(other_user, "tasks.create", task);
    CHECK_EQ(fixture.call(session, "tasks.due").get("payload").getInt("sent"), 0);
    CHECK_EQ(fixture.call(other_user, "tasks.due").get("payload").getInt("sent"), 1);
    CHECK_EQ(fixture.call(other_user, "tasks.due").get("payload").getInt("sent"), 0);
}

TEST(audit_l22_internal_errors_are_generic) {
    Fixture fixture;
    fixture.server->registerHandler("test.throw", [](std::shared_ptr<selfvoy::Session>, const selfvoy::protocol::Request&) -> Json {
        throw std::runtime_error("ERROR: relation \"users\" violates constraint users_email_key");
    });
    auto session = fixture.makeSession("throw");
    fixture.registerUser(session, "throw@example.com", "Бросок");
    const Json reply = fixture.call(session, "test.throw");
    CHECK_EQ(reply.getString("code"), std::string("internal_error"));
    CHECK(reply.getString("message").find("relation") == std::string::npos);
}

TEST(audit_l23_embedded_file_safety) {
    const std::string path = "/tmp/selfvoy-audit-db-" + std::to_string(::getpid()) + ".json";
    {
        FILE* file = std::fopen(path.c_str(), "w");
        std::fputs("{\"users\": [ {\"id\": 1, ", file);  // обрезанный JSON
        std::fclose(file);
    }
    auto broken = selfvoy::makeEmbeddedDatabase(path);
    std::string error;
    CHECK(!broken->connect(error));
    CHECK(error.find("повреждён") != std::string::npos);
    // Файл не перезаписан пустой базой.
    {
        FILE* file = std::fopen(path.c_str(), "r");
        char buffer[64] = {0};
        std::fgets(buffer, sizeof buffer, file);
        std::fclose(file);
        CHECK(std::string(buffer).find("users") != std::string::npos);
    }
    std::remove(path.c_str());

    auto fresh = selfvoy::makeEmbeddedDatabase(path);
    error.clear();
    CHECK(fresh->connect(error));
    long long id = 0;
    CHECK(fresh->createUser("perm@example.com", "x", "Perm", id).ok);
    struct stat info {};
    CHECK_EQ(::stat(path.c_str(), &info), 0);
    CHECK_EQ(info.st_mode & 0777, static_cast<unsigned>(0600));
    std::remove(path.c_str());
}

TEST(audit_l24_attempts_table_bounded) {
    ::setenv("SELFVOY_LOGIN_WINDOW", "1", 1);
    ::setenv("SELFVOY_MAX_IP_ATTEMPTS", "0", 1);
    {
        Fixture fixture;
        auto& auth = fixture.server->auth();
        for (int i = 0; i < 5000; ++i) {
            auth.login("ghost" + std::to_string(i) + "@example.com", "wrongpass1", "t", "", "127.0.0.1:1");
        }
        CHECK(auth.attemptsSize() >= 1000);
        std::this_thread::sleep_for(std::chrono::milliseconds(2100));
        for (int i = 0; i < 10; ++i) auth.login("late" + std::to_string(i) + "@example.com", "wrongpass1", "t", "", "127.0.0.1:1");
        CHECK(auth.attemptsSize() < 100);
    }
    ::unsetenv("SELFVOY_LOGIN_WINDOW");
    ::unsetenv("SELFVOY_MAX_IP_ATTEMPTS");
}

TEST(audit_password_unicode_rule) {
    CHECK(selfvoy::AuthManager::passwordError("пароль12").empty());
    CHECK(selfvoy::AuthManager::passwordError("Ελληνικά1").empty());
    CHECK(selfvoy::AuthManager::passwordError("Größe123").empty());
    CHECK(!selfvoy::AuthManager::passwordError("парол1").empty());     // 6 символов, хотя 11 байт
    CHECK(!selfvoy::AuthManager::passwordError("пароль-пароль").empty());  // нет цифр
    CHECK(!selfvoy::AuthManager::passwordError("12345678±").empty());  // нет букв
    CHECK(!selfvoy::AuthManager::passwordError(std::string("abc\xff" "12345")).empty());  // не UTF-8
    CHECK(!selfvoy::AuthManager::passwordError("a1" + std::string(255, 'b')).empty());   // > 256 байт
    CHECK(selfvoy::AuthManager::passwordError("a1" + std::string(254, 'b')).empty());    // ровно 256
}

TEST(audit_change_password_returns_token_pair) {
    Fixture fixture;
    auto session = fixture.makeSession("kai2");
    const Json logged = fixture.registerUser(session, "kai2@example.com", "Кай");
    const std::string oldRefresh = integration::payloadOf(logged).getString("refresh_token");
    auto other = std::make_shared<selfvoy::Session>(-1, "other", "127.0.0.1:31", fixture.config);
    fixture.call(other, "auth.login", audit::loginPayload("kai2@example.com", "selfvoy1234"));

    Json change = Json::object();
    change.set("old_password", Json("selfvoy1234"));
    change.set("new_password", Json("новыйпароль1"));
    const Json changed = fixture.call(session, "auth.changePassword", change);
    CHECK_EQ(changed.getString("type"), std::string("ok"));
    const Json payload = integration::payloadOf(changed);
    CHECK(payload.getBool("changed"));
    CHECK_EQ(payload.getString("token").size() > 20, true);
    CHECK(!payload.getString("refresh_token").empty());
    // Текущее соединение живо (под новой сессией), другое — выбито.
    CHECK(session->authenticated());
    CHECK_EQ(fixture.call(session, "auth.me").getString("type"), std::string("ok"));
    CHECK(!other->authenticated());
    // Старый refresh мёртв, новый — работает.
    auto probe = std::make_shared<selfvoy::Session>(-1, "probe", "127.0.0.1:32", fixture.config);
    Json refresh = Json::object();
    refresh.set("refresh_token", Json(oldRefresh));
    CHECK_EQ(fixture.call(probe, "auth.refresh", refresh).getString("code"), std::string("unauthorized"));
    refresh.set("refresh_token", Json(payload.getString("refresh_token")));
    CHECK_EQ(fixture.call(probe, "auth.refresh", refresh).getString("type"), std::string("ok"));
}

TEST(audit_reset_code_six_digits_with_limit) {
    Fixture fixture;
    auto session = fixture.makeSession("reset6");
    fixture.registerUser(session, "reset6@example.com", "Ресет");
    Json forgot = Json::object();
    forgot.set("email", Json("reset6@example.com"));
    const std::string code = integration::payloadOf(fixture.call(session, "auth.forgotPassword", forgot)).getString("reset_code");
    CHECK_EQ(code.size(), static_cast<std::size_t>(6));
    CHECK(std::all_of(code.begin(), code.end(), [](char ch) { return ch >= '0' && ch <= '9'; }));

    Json reset = Json::object();
    reset.set("email", Json("reset6@example.com"));
    reset.set("new_password", Json("newpass123"));
    // Слабый пароль отклоняется, не расходуя попытку и не подсказывая про код.
    reset.set("code", Json(code));
    reset.set("new_password", Json("weak"));
    CHECK_EQ(fixture.call(session, "auth.resetPassword", reset).getString("code"), std::string("bad_request"));
    reset.set("new_password", Json("newpass123"));
    for (int i = 0; i < 5; ++i) {
        reset.set("code", Json(code == "123456" ? std::string("654321") : std::string("123456")));
        CHECK_EQ(fixture.call(session, "auth.resetPassword", reset).getString("code"), std::string("bad_request"));
    }
    reset.set("code", Json(code));
    CHECK_EQ(fixture.call(session, "auth.resetPassword", reset).getString("code"), std::string("bad_request"));
    // Новый код работает.
    const std::string fresh = integration::payloadOf(fixture.call(session, "auth.forgotPassword", forgot)).getString("reset_code");
    reset.set("code", Json(fresh));
    CHECK(fixture.call(session, "auth.resetPassword", reset).get("payload").getBool("reset"));
}

// M15: после рестарта/обрыва PostgreSQL сервер переподключается сам.
// Запускается, только если сервер собран с libpq и задан SELFVOY_TEST_PG_URL.
TEST(audit_m15_postgres_reconnect) {
#ifdef SELFVOY_WITH_LIBPQ
    const char* url = std::getenv("SELFVOY_TEST_PG_URL");
    if (url == nullptr || *url == '\0') {
        std::cout << "  (пропущен: SELFVOY_TEST_PG_URL не задан)\n";
        return;
    }
    const std::string base = url;
    const std::string appUrl = base + (base.find('?') == std::string::npos ? "?" : "&") +
                               "application_name=selfvoy-reconnect-test";
    auto database = selfvoy::makePostgresDatabase(appUrl);
    std::string error;
    CHECK(database && database->connect(error));
    if (!database) return;
    CHECK(database->findUserById(1).has_value() || true);
    // Обрываем соединение сервера со стороны PostgreSQL.
    PGconn* admin = PQconnectdb(base.c_str());
    CHECK(PQstatus(admin) == CONNECTION_OK);
    PGresult* killed = PQexec(admin,
        "SELECT pg_terminate_backend(pid) FROM pg_stat_activity "
        "WHERE application_name = 'selfvoy-reconnect-test' AND pid <> pg_backend_pid()");
    CHECK(PQresultStatus(killed) == PGRES_TUPLES_OK && PQntuples(killed) >= 1);
    PQclear(killed);
    PQfinish(admin);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    // Первый же запрос после обрыва проходит (переподключение + повтор).
    const auto users = database->searchUsers("zz-no-such-user-zz", 1);
    CHECK(users.empty());
    CHECK(database->healthy());
    long long id = 0;
    const std::string email = "reconnect-" + std::to_string(::getpid()) + "@example.com";
    CHECK(database->createUser(email, "x", "Reconnect", id).ok);
    CHECK(database->findUserByEmail(email).has_value());
    // Ошибка БД наружу — обобщённая, без текста PostgreSQL.
    long long dup = 0;
    const auto duplicate = database->createUser(email, "x", "Reconnect", dup);
    CHECK(!duplicate.ok);
    CHECK(duplicate.message.find("duplicate") == std::string::npos);
    CHECK(duplicate.message.find("users") == std::string::npos);
#else
    std::cout << "  (пропущен: сборка без libpq)\n";
#endif
}

TEST(audit_a11_external_tools_get_no_ai_token) {
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK_EQ(::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof addr), 0);
    CHECK_EQ(::listen(listener, 1), 0);
    socklen_t len = sizeof addr;
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len);
    const int port = ntohs(addr.sin_port);

    std::string request;
    std::thread fake([&] {
        const int fd = ::accept(listener, nullptr, nullptr);
        char chunk[4096];
        while (request.find("\r\n\r\n") == std::string::npos) {
            const ssize_t n = ::recv(fd, chunk, sizeof chunk, 0);
            if (n <= 0) break;
            request.append(chunk, static_cast<std::size_t>(n));
        }
        const std::string body = "{\"results\":[]}";
        const std::string reply = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                  std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        ::send(fd, reply.data(), reply.size(), 0);
        ::close(fd);
    });

    Fixture fixture;
    selfvoy::Config config = fixture.config;
    config.toolsMode = "http";
    config.mapsApiUrl = "http://127.0.0.1:" + std::to_string(port) + "/places";
    config.aiServiceToken = "super-secret-ai-token";
    selfvoy::ToolManager tools(config, fixture.server->database(), fixture.server->chats());
    Json args = Json::object();
    args.set("city", Json("Херлен"));
    CHECK(tools.findCafe(1, args).ok);
    fake.join();
    ::close(listener);
    CHECK(request.find("POST /places") != std::string::npos);
    CHECK(request.find("super-secret-ai-token") == std::string::npos);
    CHECK(request.find("X-Selfvoy-Token") == std::string::npos);
}

// ------------------------------------------------------- админ-панель (admin.*)
namespace admin_test {

// Сессия на собственном socketpair: Fixture::makeSession закрывает конец
// предыдущей сессии, а здесь нужно, чтобы несколько соединений были «живыми»
// (иначе нечего проверять при выбивании соединений).
struct Sockets {
    std::vector<int> fds;
    ~Sockets() {
        for (const int fd : fds) ::close(fd);
    }
    std::shared_ptr<selfvoy::Session> open(Fixture& fixture, const std::string& id) {
        int pair[2] = {-1, -1};
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) return nullptr;
        fds.push_back(pair[0]);
        fds.push_back(pair[1]);
        return std::make_shared<selfvoy::Session>(pair[1], id, "127.0.0.1:0", fixture.config);
    }
};

long long userIdOf(const Json& loginResponse) {
    return loginResponse.get("payload").get("user").getInt("id");
}

Json byId(long long userId) {
    Json payload = Json::object();
    payload.set("user_id", Json(userId));
    return payload;
}

Json setActivePayload(long long userId, bool active, const std::string& reason) {
    Json payload = byId(userId);
    payload.set("active", Json(active));
    payload.set("reason", Json(reason));
    return payload;
}

Json setRolePayload(long long userId, const std::string& role) {
    Json payload = byId(userId);
    payload.set("role", Json(role));
    return payload;
}

Json loginPayload(const std::string& email) {
    Json payload = Json::object();
    payload.set("email", Json(email));
    payload.set("password", Json("selfvoy1234"));
    return payload;
}

}  // namespace admin_test

TEST(admin_role_in_auth_payloads) {
    Fixture fixture;
    auto session = fixture.makeSession("role-user");
    const Json login = fixture.registerUser(session, "plain@example.com", "Плейн");
    CHECK_EQ(login.get("payload").get("user").getString("role"), std::string("user"));
    CHECK_EQ(integration::payloadOf(fixture.call(session, "auth.me")).getString("role"), std::string("user"));
    // Обычный пользователь в админ-панель не попадает.
    CHECK_EQ(fixture.call(session, "admin.stats").getString("code"), std::string("forbidden"));
    CHECK_EQ(fixture.call(session, "admin.users").getString("code"), std::string("forbidden"));
    CHECK_EQ(fixture.call(session, "admin.audit").getString("code"), std::string("forbidden"));
    // Без входа — unauthorized: методы admin.* не публичные.
    auto anonymous = fixture.makeSession("role-anon");
    CHECK_EQ(fixture.call(anonymous, "admin.stats").getString("code"), std::string("unauthorized"));
}

TEST(admin_bootstrap_from_env_verified_only) {
    ::setenv("SELFVOY_ADMIN_EMAILS", "Boss@Example.com, ghost@example.com;late@example.com", 1);
    Fixture fixture;
    ::unsetenv("SELFVOY_ADMIN_EMAILS");
    CHECK_EQ(fixture.config.adminEmails.empty(), false);

    // Подтверждённый адрес из списка — admin сразу после verifyEmail; ответ входа уже с ролью.
    auto boss = fixture.makeSession("boss");
    const Json login = fixture.registerUser(boss, "boss@example.com", "Босс");
    CHECK_EQ(login.get("payload").get("user").getString("role"), std::string("admin"));
    CHECK_EQ(integration::payloadOf(fixture.call(boss, "auth.me")).getString("role"), std::string("admin"));
    CHECK_EQ(fixture.call(boss, "admin.stats").getString("type"), std::string("ok"));

    // Неподтверждённый адрес из списка роль не получает (иначе её взял бы тот,
    // кто первым зарегистрировал чужой email).
    auto ghost = fixture.makeSession("ghost");
    Json reg = Json::object();
    reg.set("email", Json("ghost@example.com"));
    reg.set("password", Json("selfvoy1234"));
    reg.set("display_name", Json("Призрак"));
    CHECK_EQ(fixture.call(ghost, "auth.register", reg).getString("type"), std::string("ok"));
    auto& db = fixture.server->database().db();
    CHECK_EQ(db.findUserByEmail("ghost@example.com")->role, std::string("user"));
    CHECK_EQ(fixture.call(ghost, "auth.login", admin_test::loginPayload("ghost@example.com")).getString("code"),
             std::string("email_not_verified"));
    CHECK_EQ(db.findUserByEmail("ghost@example.com")->role, std::string("user"));

    // Адрес не из списка — обычный пользователь.
    auto other = fixture.makeSession("other");
    fixture.registerUser(other, "other@example.com", "Другой");
    CHECK_EQ(db.findUserByEmail("other@example.com")->role, std::string("user"));

    // bootstrapAll при старте: пользователь подтвердился раньше, чем адрес попал в список.
    fixture.config.adminEmails = "other@example.com";
    fixture.server->stop();
    fixture.server = std::make_unique<selfvoy::Server>(fixture.config);
    std::string error;
    CHECK(fixture.server->start(error));
    auto& restarted = fixture.server->database().db();
    CHECK_EQ(restarted.findUserByEmail("other@example.com")->role, std::string("admin"));
    bool bootstrapAudited = false;
    const long long otherId = restarted.findUserByEmail("other@example.com")->id;
    for (const auto& entry : restarted.listAuditPage(otherId, 0, 10)) {
        if (entry.kind == "role_bootstrap") bootstrapAudited = true;
    }
    CHECK(bootstrapAudited);
}

TEST(admin_moderator_vs_admin_permissions) {
    Fixture fixture;
    admin_test::Sockets sockets;
    auto adminSession = sockets.open(fixture, "perm-admin");
    auto modSession = sockets.open(fixture, "perm-mod");
    auto userSession = sockets.open(fixture, "perm-user");
    auto peerSession = sockets.open(fixture, "perm-peer");
    const long long adminId = admin_test::userIdOf(fixture.registerUser(adminSession, "padmin@example.com", "Админ"));
    const long long modId = admin_test::userIdOf(fixture.registerUser(modSession, "pmod@example.com", "Модератор"));
    const long long userId = admin_test::userIdOf(fixture.registerUser(userSession, "puser@example.com", "Юзер"));
    const long long peerId = admin_test::userIdOf(fixture.registerUser(peerSession, "ppeer@example.com", "Пир"));
    auto& db = fixture.server->database().db();
    CHECK(db.setUserRole(adminId, "admin").ok);

    // Администратор назначает модератора; роль видна в ответе.
    const Json promoted = fixture.call(adminSession, "admin.setRole", admin_test::setRolePayload(modId, "moderator"));
    CHECK_EQ(promoted.getString("type"), std::string("ok"));
    CHECK_EQ(integration::payloadOf(promoted).get("user").getString("role"), std::string("moderator"));
    CHECK_EQ(fixture.call(adminSession, "admin.setRole", admin_test::setRolePayload(modId, "root")).getString("code"),
             std::string("bad_request"));

    // Модератор: обзор и список — да; роли и журнал — нет.
    CHECK_EQ(fixture.call(modSession, "admin.stats").getString("type"), std::string("ok"));
    CHECK_EQ(fixture.call(modSession, "admin.users").getString("type"), std::string("ok"));
    CHECK_EQ(fixture.call(modSession, "admin.audit").getString("code"), std::string("forbidden"));
    CHECK_EQ(fixture.call(modSession, "admin.setRole", admin_test::setRolePayload(userId, "moderator")).getString("code"),
             std::string("forbidden"));
    // Модератор не трогает администраторов, модераторов и себя.
    CHECK_EQ(fixture.call(modSession, "admin.setActive", admin_test::setActivePayload(adminId, false, "тест"))
                 .getString("code"),
             std::string("forbidden"));
    CHECK_EQ(fixture.call(modSession, "admin.revokeSessions", admin_test::byId(adminId)).getString("code"),
             std::string("forbidden"));
    CHECK_EQ(fixture.call(modSession, "admin.setActive", admin_test::setActivePayload(modId, false, "тест"))
                 .getString("code"),
             std::string("forbidden"));
    // Причина блокировки обязательна.
    CHECK_EQ(fixture.call(modSession, "admin.setActive", admin_test::setActivePayload(userId, false, "  "))
                 .getString("code"),
             std::string("bad_request"));
    // Обычного пользователя модератор блокирует и разблокирует.
    const Json blocked = fixture.call(modSession, "admin.setActive", admin_test::setActivePayload(userId, false, "спам"));
    CHECK_EQ(blocked.getString("type"), std::string("ok"));
    CHECK_EQ(integration::payloadOf(blocked).get("user").getBool("is_active", true), false);
    CHECK_EQ(fixture.call(modSession, "admin.setActive", admin_test::setActivePayload(userId, true, ""))
                 .getString("type"),
             std::string("ok"));

    // Карточка пользователя: модератор видит события, но без IP-адресов.
    const Json modView = integration::payloadOf(fixture.call(modSession, "admin.user", admin_test::byId(userId)));
    CHECK_EQ(modView.get("user").getString("email"), std::string("puser@example.com"));
    CHECK(modView.get("audit").items().size() > 0);
    for (const auto& entry : modView.get("audit").items()) CHECK(entry.getString("remote_addr").empty());
    const Json adminView = integration::payloadOf(fixture.call(adminSession, "admin.user", admin_test::byId(userId)));
    bool anyAddress = false;
    for (const auto& entry : adminView.get("audit").items()) anyAddress |= !entry.getString("remote_addr").empty();
    CHECK(anyAddress);
    CHECK_EQ(fixture.call(modSession, "admin.user", admin_test::byId(999999)).getString("code"),
             std::string("not_found"));

    // Свою роль и статус не меняет никто, в том числе единственный администратор
    // (так последний администратор не может понизить сам себя).
    CHECK_EQ(fixture.call(adminSession, "admin.setRole", admin_test::setRolePayload(adminId, "user")).getString("code"),
             std::string("forbidden"));
    CHECK_EQ(fixture.call(adminSession, "admin.setActive", admin_test::setActivePayload(adminId, false, "я"))
                 .getString("code"),
             std::string("forbidden"));
    CHECK_EQ(db.findUserById(adminId)->role, std::string("admin"));

    // Второй администратор может понизить первого; тогда последний — он сам, и себя не понизит.
    CHECK_EQ(fixture.call(adminSession, "admin.setRole", admin_test::setRolePayload(peerId, "admin")).getString("type"),
             std::string("ok"));
    CHECK_EQ(fixture.call(peerSession, "admin.setRole", admin_test::setRolePayload(adminId, "user")).getString("type"),
             std::string("ok"));
    CHECK_EQ(fixture.call(peerSession, "admin.setRole", admin_test::setRolePayload(peerId, "moderator")).getString("code"),
             std::string("forbidden"));
    // Понижение действует сразу: бывший администратор без перевхода теряет доступ.
    CHECK_EQ(fixture.call(adminSession, "admin.stats").getString("code"), std::string("forbidden"));

    // Роль модератора/админа — только активному пользователю с подтверждённым email.
    Json reg = Json::object();
    reg.set("email", Json("pnew@example.com"));
    reg.set("password", Json("selfvoy1234"));
    reg.set("display_name", Json("Новый"));
    fixture.call(fixture.makeSession("perm-new"), "auth.register", reg);
    const long long freshId = db.findUserByEmail("pnew@example.com")->id;
    CHECK_EQ(fixture.call(peerSession, "admin.setRole", admin_test::setRolePayload(freshId, "moderator")).getString("code"),
             std::string("bad_request"));

    // Журнал: смена роли с old_role и актором.
    bool roleAudited = false;
    for (const auto& entry : db.listAuditPage(adminId, 0, 20)) {
        if (entry.kind == "admin_role_changed" && entry.detail.getString("old_role") == "admin" &&
            entry.detail.getString("role") == "user" && entry.detail.getString("actor_email") == "ppeer@example.com") {
            roleAudited = true;
        }
    }
    CHECK(roleAudited);
}

TEST(admin_disable_kicks_and_blocks_login) {
    Fixture fixture;
    admin_test::Sockets sockets;
    auto adminSession = sockets.open(fixture, "dis-admin");
    auto victim = sockets.open(fixture, "dis-victim");
    auto victimSecond = sockets.open(fixture, "dis-victim-2");
    const long long adminId = admin_test::userIdOf(fixture.registerUser(adminSession, "dadmin@example.com", "Админ"));
    const Json victimLogin = fixture.registerUser(victim, "dvictim@example.com", "Нарушитель");
    const long long victimId = admin_test::userIdOf(victimLogin);
    const std::string victimToken = victimLogin.get("payload").getString("token");
    const std::string victimRefresh = victimLogin.get("payload").getString("refresh_token");
    CHECK_EQ(fixture.call(victimSecond, "auth.login", admin_test::loginPayload("dvictim@example.com")).getString("type"),
             std::string("ok"));
    auto& db = fixture.server->database().db();
    CHECK(db.setUserRole(adminId, "admin").ok);
    CHECK_EQ(fixture.server->connections().forUser(victimId).size(), std::size_t(2));

    const Json stats = integration::payloadOf(fixture.call(adminSession, "admin.stats"));
    CHECK_EQ(stats.getInt("online_now"), 2);  // два разных пользователя, у нарушителя два соединения

    const Json disabled =
        fixture.call(adminSession, "admin.setActive", admin_test::setActivePayload(victimId, false, "мошенничество"));
    CHECK_EQ(disabled.getString("type"), std::string("ok"));
    // Все открытые соединения выбиты, сессии и refresh-токены отозваны.
    CHECK_EQ(victim->alive(), false);
    CHECK_EQ(victimSecond->alive(), false);
    CHECK_EQ(fixture.server->connections().forUser(victimId).size(), std::size_t(0));
    CHECK_EQ(integration::payloadOf(fixture.call(adminSession, "admin.user", admin_test::byId(victimId)))
                 .getInt("sessions_active", -1),
             0);

    // Вход, старый токен и refresh больше не работают.
    auto retry = fixture.makeSession("dis-retry");
    CHECK_EQ(fixture.call(retry, "auth.login", admin_test::loginPayload("dvictim@example.com")).getString("code"),
             std::string("forbidden"));
    Json tokenPayload = Json::object();
    tokenPayload.set("token", Json(victimToken));
    CHECK_EQ(fixture.call(retry, "auth.token", tokenPayload).getString("code"), std::string("unauthorized"));
    CHECK(!retry->authenticated());
    Json refreshPayload = Json::object();
    refreshPayload.set("refresh_token", Json(victimRefresh));
    CHECK_EQ(fixture.call(retry, "auth.refresh", refreshPayload).getString("code"), std::string("unauthorized"));

    // Журнал: причина, актор и число завершённых сессий.
    bool audited = false;
    for (const auto& entry : db.listAuditPage(victimId, 0, 20)) {
        if (entry.kind == "admin_user_disabled" && entry.detail.getString("reason") == "мошенничество" &&
            entry.detail.getString("actor_email") == "dadmin@example.com" && entry.detail.getInt("revoked") == 2) {
            audited = true;
        }
    }
    CHECK(audited);

    // Даже живая сессия в БД (созданная в обход) не пускает заблокированного по токену.
    std::string jwtId;
    const std::string forged = fixture.server->auth().issueToken(victimId, "dvictim@example.com", jwtId);
    selfvoy::SessionRecord record;
    record.id = "00000000000000000000000000000001";
    record.userId = victimId;
    record.jwtId = jwtId;
    record.expiresAt = "2999-01-01T00:00:00Z";
    CHECK(db.createSession(record).ok);
    auto forgedSession = fixture.makeSession("dis-forged");
    Json forgedPayload = Json::object();
    forgedPayload.set("token", Json(forged));
    CHECK_EQ(fixture.call(forgedSession, "auth.token", forgedPayload).getString("code"), std::string("unauthorized"));
    db.revokeAllSessions(victimId, "");

    // Разблокировка: вход снова работает.
    CHECK_EQ(fixture.call(adminSession, "admin.setActive", admin_test::setActivePayload(victimId, true, ""))
                 .getString("type"),
             std::string("ok"));
    auto back = fixture.makeSession("dis-back");
    CHECK_EQ(fixture.call(back, "auth.login", admin_test::loginPayload("dvictim@example.com")).getString("type"),
             std::string("ok"));
}

TEST(admin_revoke_sessions_kicks) {
    Fixture fixture;
    admin_test::Sockets sockets;
    auto adminSession = sockets.open(fixture, "rev-admin");
    auto target = sockets.open(fixture, "rev-target");
    const long long adminId = admin_test::userIdOf(fixture.registerUser(adminSession, "radmin@example.com", "Админ"));
    const Json targetLogin = fixture.registerUser(target, "rtarget@example.com", "Цель");
    const long long targetId = admin_test::userIdOf(targetLogin);
    CHECK(fixture.server->database().db().setUserRole(adminId, "admin").ok);

    const Json revoked = fixture.call(adminSession, "admin.revokeSessions", admin_test::byId(targetId));
    CHECK_EQ(revoked.getString("type"), std::string("ok"));
    CHECK_EQ(integration::payloadOf(revoked).getInt("revoked"), 1);
    CHECK_EQ(target->alive(), false);
    auto retry = fixture.makeSession("rev-retry");
    Json tokenPayload = Json::object();
    tokenPayload.set("token", Json(targetLogin.get("payload").getString("token")));
    CHECK_EQ(fixture.call(retry, "auth.token", tokenPayload).getString("code"), std::string("unauthorized"));
    // Аккаунт не заблокирован: войти заново можно.
    CHECK_EQ(fixture.call(retry, "auth.login", admin_test::loginPayload("rtarget@example.com")).getString("type"),
             std::string("ok"));
    CHECK_EQ(fixture.call(adminSession, "admin.revokeSessions", admin_test::byId(adminId)).getString("code"),
             std::string("forbidden"));
    bool audited = false;
    for (const auto& entry : fixture.server->database().db().listAuditPage(targetId, 0, 20)) {
        if (entry.kind == "admin_sessions_revoked" && entry.detail.getInt("revoked") == 1) audited = true;
    }
    CHECK(audited);
}

TEST(admin_users_filters_and_stats) {
    Fixture fixture;
    admin_test::Sockets sockets;
    auto adminSession = sockets.open(fixture, "flt-admin");
    const long long adminId = admin_test::userIdOf(fixture.registerUser(adminSession, "fadmin@example.com", "Админ"));
    auto& db = fixture.server->database().db();
    CHECK(db.setUserRole(adminId, "admin").ok);
    for (const char* name : {"ivan", "maria", "petr", "olga"}) {
        auto session = fixture.makeSession(std::string("flt-") + name);
        fixture.registerUser(session, std::string(name) + "@example.com", std::string("Имя ") + name);
    }
    // Неподтверждённый пользователь.
    Json reg = Json::object();
    reg.set("email", Json("unverified@example.com"));
    reg.set("password", Json("selfvoy1234"));
    reg.set("display_name", Json("Ivan Unverified"));
    fixture.call(fixture.makeSession("flt-unv"), "auth.register", reg);
    // Модератор, заблокированный и пользователь с 2FA.
    const long long ivanId = db.findUserByEmail("ivan@example.com")->id;
    const long long mariaId = db.findUserByEmail("maria@example.com")->id;
    const long long petrId = db.findUserByEmail("petr@example.com")->id;
    CHECK(db.setUserRole(mariaId, "moderator").ok);
    CHECK_EQ(fixture.call(adminSession, "admin.setActive", admin_test::setActivePayload(petrId, false, "спам"))
                 .getString("type"),
             std::string("ok"));
    selfvoy::TwoFactorRecord twoFactor;
    twoFactor.userId = ivanId;
    twoFactor.secretEncrypted = "x";
    twoFactor.enabled = true;
    CHECK(db.upsertTwoFactor(twoFactor).ok);

    const auto list = [&](const std::string& query, const std::string& role, const std::string& status, int limit,
                          int offset) {
        Json payload = Json::object();
        payload.set("query", Json(query));
        payload.set("role", Json(role));
        payload.set("status", Json(status));
        payload.set("limit", Json(limit));
        payload.set("offset", Json(offset));
        return integration::payloadOf(fixture.call(adminSession, "admin.users", payload));
    };
    const Json all = list("", "", "", 50, 0);
    CHECK_EQ(all.getInt("total"), 6);
    CHECK_EQ(all.get("users").items().size(), std::size_t(6));
    CHECK(all.get("users").at(0).getInt("id") > all.get("users").at(1).getInt("id"));  // новые первыми
    const Json first = all.get("users").at(0);
    for (const char* key : {"id", "email", "display_name", "avatar_url", "role", "is_active", "email_verified",
                            "two_factor", "created_at", "last_seen_at"}) {
        CHECK(first.contains(key));
    }
    CHECK(!first.contains("password_hash"));

    // Поиск по email и имени без учёта регистра.
    CHECK_EQ(list("IVAN", "", "", 50, 0).getInt("total"), 2);  // ivan@ и «Ivan Unverified»
    CHECK_EQ(list("ivan", "", "unverified", 50, 0).getInt("total"), 1);
    CHECK_EQ(list("", "", "unverified", 50, 0).get("users").at(0).getString("email"),
             std::string("unverified@example.com"));
    CHECK_EQ(list("", "", "disabled", 50, 0).get("users").at(0).getInt("id"), petrId);
    CHECK_EQ(list("", "", "active", 50, 0).getInt("total"), 5);
    CHECK_EQ(list("", "moderator", "", 50, 0).get("users").at(0).getInt("id"), mariaId);
    CHECK_EQ(list("", "admin", "", 50, 0).getInt("total"), 1);
    CHECK_EQ(list("ivan@", "", "", 50, 0).get("users").at(0).getBool("two_factor"), true);
    CHECK_EQ(list("100%", "", "", 50, 0).getInt("total"), 0);  // спецсимволы LIKE — буквально
    // Страницы: total не зависит от limit/offset.
    const Json page1 = list("", "", "", 4, 0);
    const Json page2 = list("", "", "", 4, 4);
    CHECK_EQ(page1.get("users").items().size(), std::size_t(4));
    CHECK_EQ(page2.get("users").items().size(), std::size_t(2));
    CHECK_EQ(page2.getInt("total"), 6);
    CHECK(page1.get("users").at(3).getInt("id") > page2.get("users").at(0).getInt("id"));
    // Неверные фильтры.
    Json badStatus = Json::object();
    badStatus.set("status", Json("banned"));
    CHECK_EQ(fixture.call(adminSession, "admin.users", badStatus).getString("code"), std::string("bad_request"));
    Json badRole = Json::object();
    badRole.set("role", Json("owner"));
    CHECK_EQ(fixture.call(adminSession, "admin.users", badRole).getString("code"), std::string("bad_request"));

    const Json stats = integration::payloadOf(fixture.call(adminSession, "admin.stats"));
    CHECK_EQ(stats.getInt("users_total"), 6);
    CHECK_EQ(stats.getInt("users_verified"), 5);
    CHECK_EQ(stats.getInt("users_disabled"), 1);
    CHECK_EQ(stats.getInt("users_2fa"), 1);
    CHECK_EQ(stats.getInt("users_new_7d"), 6);
    CHECK_EQ(stats.getInt("moderators"), 1);
    CHECK_EQ(stats.getInt("admins"), 1);
    CHECK_EQ(stats.getInt("sessions_active"), 4);  // 5 входов, сессия petr отозвана блокировкой
    CHECK(stats.contains("online_now"));
}

TEST(admin_audit_paging) {
    Fixture fixture;
    admin_test::Sockets sockets;
    auto adminSession = sockets.open(fixture, "aud-admin");
    auto userSession = sockets.open(fixture, "aud-user");
    const long long adminId = admin_test::userIdOf(fixture.registerUser(adminSession, "aadmin@example.com", "Админ"));
    const long long userId = admin_test::userIdOf(fixture.registerUser(userSession, "auser@example.com", "Юзер"));
    CHECK(fixture.server->database().db().setUserRole(adminId, "admin").ok);
    for (int i = 0; i < 3; ++i) {
        fixture.call(adminSession, "admin.setActive", admin_test::setActivePayload(userId, false, "раз"));
        fixture.call(adminSession, "admin.setActive", admin_test::setActivePayload(userId, true, ""));
    }

    const auto page = [&](long long user, long long before, int limit) {
        Json payload = Json::object();
        payload.set("user_id", Json(user));
        payload.set("before_id", Json(before));
        payload.set("limit", Json(limit));
        return integration::payloadOf(fixture.call(adminSession, "admin.audit", payload)).get("entries");
    };
    const Json first = page(0, 0, 5);
    CHECK_EQ(first.items().size(), std::size_t(5));
    for (std::size_t i = 1; i < first.items().size(); ++i) {
        CHECK(first.at(i - 1).getInt("id") > first.at(i).getInt("id"));
    }
    const long long lastId = first.at(4).getInt("id");
    const Json second = page(0, lastId, 5);
    CHECK(second.items().size() > 0);
    for (const auto& entry : second.items()) CHECK(entry.getInt("id") < lastId);
    // Фильтр по пользователю + email в каждой записи.
    const Json onlyUser = page(userId, 0, 200);
    CHECK(onlyUser.items().size() >= 6);
    for (const auto& entry : onlyUser.items()) {
        CHECK_EQ(entry.getInt("user_id"), userId);
        CHECK_EQ(entry.getString("user_email"), std::string("auser@example.com"));
    }
    // Дошли до конца — пустая страница.
    const long long oldest = page(0, 0, 200).items().back().getInt("id");
    CHECK_EQ(page(0, oldest, 10).items().size(), std::size_t(0));
}

// ------------------------------------------------- приглашения по ссылке (invite.*)
namespace invite_test {

// Поддельный AI-сервис: отвечает на каждый POST телом из reply(); запросы
// складываются в requests. Живёт до деструктора (закрывает слушатель).
struct FakeAi {
    int listener = -1;
    int port = 0;
    std::function<std::string()> reply;
    std::vector<std::string> requests;
    std::mutex mutex;
    std::thread thread;
    bool stopping = false;

    explicit FakeAi(std::function<std::string()> body) : reply(std::move(body)) {
        listener = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
        ::listen(listener, 8);
        socklen_t len = sizeof addr;
        ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len);
        port = ntohs(addr.sin_port);
        thread = std::thread([this] {
            for (;;) {
                const int fd = ::accept(listener, nullptr, nullptr);
                if (fd < 0) return;
                std::string request;
                char chunk[4096];
                std::size_t need = std::string::npos;
                while (need == std::string::npos || request.size() < need) {
                    const ssize_t n = ::recv(fd, chunk, sizeof chunk, 0);
                    if (n <= 0) break;
                    request.append(chunk, static_cast<std::size_t>(n));
                    const std::size_t head = request.find("\r\n\r\n");
                    if (head != std::string::npos && need == std::string::npos) {
                        std::size_t length = 0;
                        const std::size_t cl = request.find("Content-Length: ");
                        if (cl != std::string::npos && cl < head) length = std::stoul(request.substr(cl + 16));
                        need = head + 4 + length;
                    }
                }
                std::string body;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (stopping) {
                        ::close(fd);
                        return;
                    }
                    requests.push_back(request);
                    body = reply();
                }
                const std::string answer = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                           std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                ::send(fd, answer.data(), answer.size(), 0);
                ::close(fd);
            }
        });
    }

    ~FakeAi() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        ::shutdown(listener, SHUT_RDWR);
        ::close(listener);
        thread.join();
    }

    std::string url() const { return "http://127.0.0.1:" + std::to_string(port); }
};

// Час, кратный получасу, через hours часов от текущего момента (UTC).
std::time_t future(int hours) {
    const std::time_t now = std::time(nullptr);
    return (now / 1800 + 1) * 1800 + static_cast<std::time_t>(hours) * 3600;
}

Json slot(std::time_t start, int minutes) {
    Json json = Json::object();
    json.set("start", Json(selfvoy::isoUtc(start)));
    json.set("end", Json(selfvoy::isoUtc(start + minutes * 60)));
    json.set("score", Json(0.9));
    json.set("reason", Json("удобное время"));
    return json;
}

// Перезапуск сервера фикстуры с адресом поддельного AI (БД-файл сохраняется).
void useAi(Fixture& fixture, const std::string& url, const std::string& publicUrl = "") {
    fixture.config.aiServiceUrl = url;
    fixture.config.publicUrl = publicUrl;
    fixture.server->stop();
    fixture.server = std::make_unique<selfvoy::Server>(fixture.config);
    std::string error;
    if (!fixture.server->start(error)) std::cout << "  не удалось перезапустить сервер: " << error << "\n";
}

Json createPayload(const std::string& topic, long long duration = 60) {
    Json payload = Json::object();
    payload.set("topic", Json(topic));
    payload.set("duration_minutes", Json(duration));
    payload.set("window_hours", Json(168LL));
    return payload;
}

Json respondPayload(const std::string& token, const std::string& action, const std::string& start,
                    const std::string& name) {
    Json payload = Json::object();
    payload.set("token", Json(token));
    payload.set("action", Json(action));
    if (!start.empty()) payload.set("start", Json(start));
    payload.set("guest_name", Json(name));
    return payload;
}

Json tokenPayload(const std::string& token) {
    Json payload = Json::object();
    payload.set("token", Json(token));
    return payload;
}

}  // namespace invite_test

TEST(invite_accept_flow) {
    using namespace invite_test;
    const std::time_t first = future(26);
    const std::time_t second = future(50);
    const std::time_t third = future(74);
    // AI предлагает и лишнее: прошлое время, чужую длительность и пересечение —
    // сервер это отбрасывает (LLM/AI не источник истины).
    FakeAi ai([&] {
        Json slots = Json::array();
        slots.push(slot(third, 60));
        slots.push(slot(first, 60));
        slots.push(slot(std::time(nullptr) - 7200, 60));
        slots.push(slot(second, 45));
        slots.push(slot(second, 60));
        slots.push(slot(first + 1800, 60));
        Json body = Json::object();
        body.set("slots", slots);
        return body.dump();
    });
    Fixture fixture;
    useAi(fixture, ai.url(), "https://selfvoy.example.com");
    admin_test::Sockets sockets;
    auto owner = sockets.open(fixture, "invite-owner");
    fixture.registerUser(owner, "host@example.com", "Иван");

    const Json created = fixture.call(owner, "invite.create", createPayload("Кофе и план"));
    CHECK_EQ(created.getString("type"), std::string("ok"));
    if (created.getString("type") != "ok") {
        std::cout << "  invite.create: " << created.dump() << "\n";
        return;
    }
    const Json createdPayload = integration::payloadOf(created);
    const std::string token = createdPayload.getString("token");
    CHECK_EQ(token.size(), std::size_t(43));
    CHECK_EQ(createdPayload.getString("url"), "https://selfvoy.example.com/#/i/" + token);
    const Json invite = createdPayload.get("invite");
    CHECK_EQ(invite.getString("status"), std::string("open"));
    CHECK_EQ(invite.get("slots").items().size(), std::size_t(3));
    CHECK_EQ(invite.get("slots").items().front().getString("start"), selfvoy::isoUtc(first));
    CHECK_EQ(invite.getString("expires_at"), selfvoy::isoUtc(third));
    {
        std::lock_guard<std::mutex> lock(ai.mutex);
        // Первый запрос — проверка /healthz при старте сервера.
        const auto slotsRequest = std::find_if(ai.requests.begin(), ai.requests.end(), [](const std::string& r) {
            return r.find("POST /v1/agent/slots") != std::string::npos;
        });
        CHECK(slotsRequest != ai.requests.end());
        if (slotsRequest != ai.requests.end()) {
            CHECK(slotsRequest->find("\"duration_minutes\":60") != std::string::npos);
            CHECK(slotsRequest->find("\"context\"") != std::string::npos);
        }
    }
    // В БД — только хэш токена; в списке владельца токена нет.
    auto& db = fixture.server->database().db();
    const auto stored = db.findInvite(invite.getInt("id"));
    CHECK(stored.has_value());
    CHECK(stored->tokenHash != token);
    CHECK_EQ(stored->tokenHash, selfvoy::InviteManager::tokenHash(token));
    const Json listed = integration::payloadOf(fixture.call(owner, "invite.list"));
    CHECK_EQ(listed.get("invites").items().size(), std::size_t(1));
    CHECK(listed.dump().find(token) == std::string::npos);

    // Гость без входа видит приглашение, но не email владельца.
    auto guest = sockets.open(fixture, "invite-guest");
    const Json seen = fixture.call(guest, "invite.get", tokenPayload(token));
    CHECK_EQ(seen.getString("type"), std::string("ok"));
    const Json guestView = integration::payloadOf(seen).get("invite");
    CHECK_EQ(guestView.getString("owner_name"), std::string("Иван"));
    CHECK_EQ(guestView.getString("topic"), std::string("Кофе и план"));
    CHECK_EQ(guestView.get("slots").items().size(), std::size_t(3));
    CHECK(seen.dump().find("host@example.com") == std::string::npos);
    CHECK_EQ(fixture.call(guest, "invite.get", tokenPayload(std::string(43, 'A'))).getString("code"),
             std::string("not_found"));
    CHECK_EQ(fixture.call(guest, "invite.get", tokenPayload("short")).getString("code"), std::string("not_found"));

    // Ошибки ввода гостя.
    CHECK_EQ(fixture.call(guest, "invite.respond", respondPayload(token, "accept", selfvoy::isoUtc(first + 600), "Мария"))
                 .getString("code"),
             std::string("bad_request"));
    CHECK_EQ(fixture.call(guest, "invite.respond", respondPayload(token, "accept", selfvoy::isoUtc(first), "  "))
                 .getString("code"),
             std::string("bad_request"));
    Json badEmail = respondPayload(token, "accept", selfvoy::isoUtc(first), "Мария");
    badEmail.set("guest_email", Json("not an email"));
    CHECK_EQ(fixture.call(guest, "invite.respond", badEmail).getString("code"), std::string("bad_request"));

    // Выбор времени: ответ гостя закрывает приглашение.
    Json accept = respondPayload(token, "accept", selfvoy::isoUtc(second), "Мария");
    accept.set("guest_email", Json("maria@example.com"));
    accept.set("comment", Json("Возьму ноутбук"));
    const Json accepted = fixture.call(guest, "invite.respond", accept);
    CHECK_EQ(accepted.getString("type"), std::string("ok"));
    const Json acceptedView = integration::payloadOf(accepted).get("invite");
    CHECK_EQ(acceptedView.getString("status"), std::string("accepted"));
    CHECK_EQ(acceptedView.get("chosen").getString("start"), selfvoy::isoUtc(second));
    CHECK_EQ(acceptedView.get("chosen").getString("end"), selfvoy::isoUtc(second + 3600));
    CHECK_EQ(acceptedView.get("slots").items().size(), std::size_t(0));
    // Повторный ответ (в т.ч. отказ) — conflict.
    CHECK_EQ(fixture.call(guest, "invite.respond", respondPayload(token, "decline", "", "Мария")).getString("code"),
             std::string("conflict"));

    // У владельца: статус, задача с напоминанием, встреча в расписании, уведомление.
    const Json invitesAfter = integration::payloadOf(fixture.call(owner, "invite.list")).get("invites");
    CHECK_EQ(invitesAfter.items().size(), std::size_t(1));
    if (invitesAfter.items().empty()) return;
    const Json after = invitesAfter.items().front();
    CHECK_EQ(after.getString("status"), std::string("accepted"));
    CHECK_EQ(after.getString("guest_name"), std::string("Мария"));
    CHECK_EQ(after.getString("guest_email"), std::string("maria@example.com"));
    CHECK_EQ(after.getString("guest_comment"), std::string("Возьму ноутбук"));
    bool taskFound = false;
    const Json tasks = integration::payloadOf(fixture.call(owner, "tasks.list")).get("tasks");
    for (const auto& task : tasks.items()) {
        if (task.getString("title") != "Встреча: Кофе и план — Мария") continue;
        taskFound = true;
        // Сравниваем моменты, а не строки: PostgreSQL и встроенная БД пишут «.000Z».
        std::time_t due = 0, remind = 0;
        CHECK(selfvoy::parseIsoUtc(task.getString("due_at"), due));
        CHECK(selfvoy::parseIsoUtc(task.getString("remind_at"), remind));
        CHECK_EQ(due, second);
        CHECK_EQ(remind, second - 900);
        CHECK(task.getString("notes").find("maria@example.com") != std::string::npos);
    }
    CHECK(taskFound);
    const long long ownerId = db.findUserByEmail("host@example.com")->id;
    bool busy = false;
    for (const auto& [start, end] : fixture.server->memory().busy(ownerId)) {
        if (start == second && end == second + 3600) busy = true;
    }
    CHECK(busy);
    bool notified = false;
    const Json notifications = integration::payloadOf(fixture.call(owner, "notifications.list")).get("notifications");
    for (const auto& item : notifications.items()) {
        if (item.getString("kind") == "invite.accepted") {
            notified = true;
            CHECK_EQ(item.get("payload").getString("start"), selfvoy::isoUtc(second));
        }
    }
    CHECK(notified);
    // Страница гостя после выбора: время видно, слотов нет.
    const Json reopened = integration::payloadOf(fixture.call(guest, "invite.get", tokenPayload(token))).get("invite");
    CHECK_EQ(reopened.getString("status"), std::string("accepted"));
    CHECK_EQ(reopened.get("chosen").getString("start"), selfvoy::isoUtc(second));
}

TEST(invite_busy_cancel_and_decline) {
    using namespace invite_test;
    const std::time_t first = future(30);
    const std::time_t second = future(54);
    FakeAi ai([&] {
        Json slots = Json::array();
        slots.push(slot(first, 60));
        slots.push(slot(second, 60));
        Json body = Json::object();
        body.set("slots", slots);
        return body.dump();
    });
    Fixture fixture;
    useAi(fixture, ai.url());
    admin_test::Sockets sockets;
    auto owner = sockets.open(fixture, "busy-owner");
    fixture.registerUser(owner, "busy@example.com", "Ольга");
    const Json a = integration::payloadOf(fixture.call(owner, "invite.create", createPayload("Созвон")));
    const Json b = integration::payloadOf(fixture.call(owner, "invite.create", createPayload("Обед")));
    const Json c = integration::payloadOf(fixture.call(owner, "invite.create", createPayload("Прогулка")));
    CHECK_EQ(a.getString("url"), std::string());  // SELFVOY_PUBLIC_URL не задан
    auto guest = sockets.open(fixture, "busy-guest");

    // Время, занятое гостем приглашения A, исчезает из B и не принимается в B.
    CHECK_EQ(fixture.call(guest, "invite.respond",
                          respondPayload(a.getString("token"), "accept", selfvoy::isoUtc(first), "Пётр"))
                 .getString("type"),
             std::string("ok"));
    const Json bView = integration::payloadOf(fixture.call(guest, "invite.get", tokenPayload(b.getString("token"))));
    CHECK_EQ(bView.get("invite").get("slots").items().size(), std::size_t(1));
    CHECK_EQ(bView.get("invite").get("slots").items().front().getString("start"), selfvoy::isoUtc(second));
    const Json taken = fixture.call(guest, "invite.respond",
                                    respondPayload(b.getString("token"), "accept", selfvoy::isoUtc(first), "Анна"));
    CHECK_EQ(taken.getString("code"), std::string("conflict"));
    CHECK(taken.getString("message").find("занято") != std::string::npos);

    // Отказ: приглашение закрыто, владельцу — уведомление invite.declined.
    Json decline = respondPayload(b.getString("token"), "decline", "", "Анна");
    decline.set("comment", Json("Уезжаю"));
    const Json declined = fixture.call(guest, "invite.respond", decline);
    CHECK_EQ(integration::payloadOf(declined).get("invite").getString("status"), std::string("declined"));
    bool declineNotified = false;
    const Json notifications = integration::payloadOf(fixture.call(owner, "notifications.list")).get("notifications");
    for (const auto& item : notifications.items()) {
        if (item.getString("kind") == "invite.declined") {
            declineNotified = true;
            CHECK_EQ(item.get("payload").getString("comment"), std::string("Уезжаю"));
        }
    }
    CHECK(declineNotified);

    // Отмена: только владелец и только открытое.
    const long long cId = c.get("invite").getInt("id");
    Json byId = Json::object();
    byId.set("invite_id", Json(cId));
    auto stranger = sockets.open(fixture, "busy-stranger");
    fixture.registerUser(stranger, "stranger@example.com", "Чужой");
    CHECK_EQ(fixture.call(stranger, "invite.cancel", byId).getString("code"), std::string("not_found"));
    const Json cancelled = fixture.call(owner, "invite.cancel", byId);
    CHECK_EQ(integration::payloadOf(cancelled).get("invite").getString("status"), std::string("cancelled"));
    CHECK_EQ(fixture.call(owner, "invite.cancel", byId).getString("code"), std::string("conflict"));
    CHECK_EQ(fixture.call(guest, "invite.respond",
                          respondPayload(c.getString("token"), "accept", selfvoy::isoUtc(second), "Пётр"))
                 .getString("code"),
             std::string("conflict"));
    CHECK_EQ(integration::payloadOf(fixture.call(guest, "invite.get", tokenPayload(c.getString("token"))))
                 .get("invite")
                 .getString("status"),
             std::string("cancelled"));
    // Чужие приглашения в список не попадают.
    CHECK_EQ(integration::payloadOf(fixture.call(stranger, "invite.list")).get("invites").items().size(),
             std::size_t(0));
}

TEST(invite_validation_limits_and_upstream) {
    using namespace invite_test;
    Fixture fixture;  // AI недоступен
    admin_test::Sockets sockets;
    auto anonymous = sockets.open(fixture, "inv-anon");
    CHECK_EQ(fixture.call(anonymous, "invite.create", createPayload("Кофе")).getString("code"),
             std::string("unauthorized"));
    CHECK_EQ(fixture.call(anonymous, "invite.list").getString("code"), std::string("unauthorized"));
    auto owner = sockets.open(fixture, "inv-owner");
    fixture.registerUser(owner, "limits@example.com", "Лимит");
    CHECK_EQ(fixture.call(owner, "invite.create", createPayload("  ")).getString("code"), std::string("bad_request"));
    CHECK_EQ(fixture.call(owner, "invite.create", createPayload("Строка\nс переводом")).getString("code"),
             std::string("bad_request"));
    CHECK_EQ(fixture.call(owner, "invite.create", createPayload(std::string(121, 'x'))).getString("code"),
             std::string("bad_request"));
    CHECK_EQ(fixture.call(owner, "invite.create", createPayload("Кофе", 10)).getString("code"),
             std::string("bad_request"));
    Json wide = createPayload("Кофе");
    wide.set("window_hours", Json(400LL));
    CHECK_EQ(fixture.call(owner, "invite.create", wide).getString("code"), std::string("bad_request"));
    CHECK_EQ(fixture.call(owner, "invite.create", createPayload("Кофе")).getString("code"),
             std::string("upstream_error"));

    // Пустой ответ AI — conflict «нет свободных окон».
    FakeAi empty([] { return std::string("{\"slots\":[]}"); });
    useAi(fixture, empty.url());
    auto again = sockets.open(fixture, "inv-owner-2");
    Json login = Json::object();
    login.set("email", Json("limits@example.com"));
    login.set("password", Json("selfvoy1234"));
    CHECK_EQ(fixture.call(again, "auth.login", login).getString("type"), std::string("ok"));
    CHECK_EQ(fixture.call(again, "invite.create", createPayload("Кофе")).getString("code"), std::string("conflict"));

    // Лимит ответов гостя с одного IP.
    std::string lastCode;
    for (int i = 0; i <= selfvoy::InviteManager::kRespondPerMinute; ++i) {
        lastCode = fixture.call(anonymous, "invite.respond",
                                respondPayload(std::string(43, 'B'), "accept", selfvoy::isoUtc(future(30)), "Гость"))
                       .getString("code");
    }
    CHECK_EQ(lastCode, std::string("forbidden"));
}

TEST(invite_effective_status_and_open_limit) {
    using namespace invite_test;
    selfvoy::InviteRecord record;
    record.status = "open";
    record.expiresAt = selfvoy::isoUtc(std::time(nullptr) - 60);
    CHECK_EQ(selfvoy::InviteManager::effectiveStatus(record, std::time(nullptr)), std::string("expired"));
    record.expiresAt = selfvoy::isoUtc(std::time(nullptr) + 3600);
    CHECK_EQ(selfvoy::InviteManager::effectiveStatus(record, std::time(nullptr)), std::string("open"));
    record.status = "declined";
    CHECK_EQ(selfvoy::InviteManager::effectiveStatus(record, std::time(nullptr)), std::string("declined"));
    CHECK_EQ(selfvoy::InviteManager::tokenHash("bad token with spaces and 43 symbols long.."), std::string());

    // Не больше kMaxOpenInvites открытых приглашений у владельца.
    const std::time_t start = future(30);
    FakeAi ai([&] {
        Json slots = Json::array();
        slots.push(slot(start, 30));
        Json body = Json::object();
        body.set("slots", slots);
        return body.dump();
    });
    Fixture fixture;
    useAi(fixture, ai.url());
    admin_test::Sockets sockets;
    auto owner = sockets.open(fixture, "open-limit");
    fixture.registerUser(owner, "many@example.com", "Много");
    for (int i = 0; i < selfvoy::InviteManager::kMaxOpenInvites; ++i) {
        CHECK_EQ(fixture.call(owner, "invite.create", createPayload("Встреча " + std::to_string(i), 30))
                     .getString("type"),
                 std::string("ok"));
    }
    CHECK_EQ(fixture.call(owner, "invite.create", createPayload("Лишняя", 30)).getString("code"),
             std::string("forbidden"));
}

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);  // сессии в тестах пишут в socketpair
    std::cout.setf(std::ios::unitbuf);
    const std::string filter = argc > 1 ? argv[1] : "";
    int executed = 0;
    for (const auto& test : registry()) {
        if (!filter.empty() && test.name.find(filter) == std::string::npos) continue;
        ++executed;
        std::cout << "[ RUN  ] " << test.name << "\n";
        const int before = g_failures;
        test.body();
        std::cout << (g_failures == before ? "[  OK  ] " : "[ FAIL ] ") << test.name << "\n";
    }
    std::cout << "\nтестов: " << executed << ", проверок: " << g_checks << ", ошибок: " << g_failures
              << "\n";
    return g_failures == 0 ? 0 : 1;
}

// ------------------------------------------------------ страницы записи (booking.*)
namespace booking_test {

using invite_test::FakeAi;
using invite_test::future;

// Слот AI с местной датой владельца day.
Json daySlot(std::time_t start, int minutes, const std::string& day) {
    Json json = invite_test::slot(start, minutes);
    json.set("day", Json(day));
    return json;
}

Json pagePayload(const std::string& title) {
    Json payload = Json::object();
    payload.set("title", Json(title));
    payload.set("description", Json("Разбор вашего проекта"));
    payload.set("duration_minutes", Json(60LL));
    payload.set("days_ahead", Json(14LL));
    payload.set("max_per_day", Json(2LL));
    payload.set("notice_hours", Json(1LL));
    payload.set("max_series", Json(3LL));
    return payload;
}

Json slugPayload(const std::string& slug) {
    Json payload = Json::object();
    payload.set("slug", Json(slug));
    return payload;
}

Json bookPayload(const std::string& slug, std::time_t start, const std::string& name, long long count = 1) {
    Json payload = slugPayload(slug);
    payload.set("start", Json(selfvoy::isoUtc(start)));
    payload.set("guest_name", Json(name));
    if (count != 1) payload.set("count", Json(count));
    return payload;
}

Json tokenPayload(const std::string& token) { return invite_test::tokenPayload(token); }

// Письма гостям уходят из фоновой очереди — ждём, пока ящик dev перестанет меняться.
std::vector<selfvoy::MailNotice> settledOutbox() {
    std::size_t last = selfvoy::devOutbox().size();
    for (int i = 0; i < 60; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const std::size_t now = selfvoy::devOutbox().size();
        if (now == last && i >= 2) break;
        last = now;
    }
    return selfvoy::devOutbox();
}

// Ответ поддельного AI на POST /v1/agent/series: повторы ровно через неделю
// (пояс UTC), местное время — как в UTC.
std::string seriesBody(const std::string& request) {
    const Json body = Json::parse(request.substr(request.find("\r\n\r\n") + 4));
    std::time_t start = 0;
    selfvoy::parseIsoUtc(body.getString("start"), start);
    const long long minutes = body.getInt("duration_minutes", 60);
    Json occurrences = Json::array();
    for (long long k = 0; k < body.getInt("count", 1); ++k) {
        const std::time_t at = start + k * 7 * 86400;
        Json item = Json::object();
        item.set("start", Json(selfvoy::isoUtc(at)));
        item.set("end", Json(selfvoy::isoUtc(at + minutes * 60)));
        item.set("day", Json(selfvoy::isoUtc(at).substr(0, 10)));
        item.set("local", Json(selfvoy::isoUtc(at).substr(0, 16)));
        occurrences.push(item);
    }
    Json answer = Json::object();
    answer.set("occurrences", occurrences);
    return answer.dump();
}

bool hasSlot(const Json& page, std::time_t start) {
    const Json slots = page.get("slots");  // get() возвращает копию: держим её до конца цикла
    for (const auto& slot : slots.items()) {
        if (slot.getString("start") == selfvoy::isoUtc(start)) return true;
    }
    return false;
}

bool isBusy(Fixture& fixture, long long ownerId, std::time_t start) {
    for (const auto& [from, to] : fixture.server->memory().busy(ownerId)) {
        if (from == start && to == start + 3600) return true;
    }
    return false;
}

int countTasks(Fixture& fixture, const std::shared_ptr<selfvoy::Session>& owner, const std::string& title) {
    int count = 0;
    const Json tasks = integration::payloadOf(fixture.call(owner, "tasks.list")).get("tasks");
    for (const auto& task : tasks.items()) {
        if (task.getString("title") == title) ++count;
    }
    return count;
}

int countNotifications(Fixture& fixture, const std::shared_ptr<selfvoy::Session>& owner, const std::string& kind) {
    int count = 0;
    const Json list = integration::payloadOf(fixture.call(owner, "notifications.list")).get("notifications");
    for (const auto& item : list.items()) {
        if (item.getString("kind") == kind) ++count;
    }
    return count;
}

}  // namespace booking_test

TEST(booking_page_book_reschedule_cancel) {
    using namespace booking_test;
    const std::time_t first = future(26);
    const std::time_t second = first + 2 * 3600;
    const std::time_t third = first + 4 * 3600;
    const std::string day = selfvoy::isoUtc(first).substr(0, 10);
    // AI предлагает и лишнее (прошлое, чужая длительность, пересечение) — сервер отбрасывает.
    FakeAi* self = nullptr;
    FakeAi ai([&] {
        // Серия — отдельный эндпоинт; запрос уже записан в requests (под тем же мьютексом).
        if (self && self->requests.back().find("POST /v1/agent/series") != std::string::npos) {
            return seriesBody(self->requests.back());
        }
        Json slots = Json::array();
        slots.push(daySlot(third, 60, day));
        slots.push(daySlot(first, 60, day));
        slots.push(daySlot(std::time(nullptr) - 7200, 60, day));
        slots.push(daySlot(second, 45, day));
        slots.push(daySlot(second, 60, day));
        slots.push(daySlot(first + 1800, 60, day));
        Json body = Json::object();
        body.set("slots", slots);
        return body.dump();
    });
    self = &ai;
    Fixture fixture;
    invite_test::useAi(fixture, ai.url(), "https://selfvoy.example.com");
    admin_test::Sockets sockets;
    auto owner = sockets.open(fixture, "booking-owner");
    fixture.registerUser(owner, "coach@example.com", "Ольга");
    const long long ownerId = fixture.server->database().db().findUserByEmail("coach@example.com")->id;

    // Проверка полей страницы.
    Json bad = pagePayload("");
    CHECK_EQ(fixture.call(owner, "booking.pageCreate", bad).getString("code"), std::string("bad_request"));
    bad = pagePayload("Консультация");
    bad.set("duration_minutes", Json(10LL));
    CHECK_EQ(fixture.call(owner, "booking.pageCreate", bad).getString("code"), std::string("bad_request"));
    bad = pagePayload("Консультация");
    bad.set("max_series", Json(13LL));
    CHECK_EQ(fixture.call(owner, "booking.pageCreate", bad).getString("code"), std::string("bad_request"));
    bad = pagePayload("Консультация");
    bad.set("weekends", Json("да"));
    CHECK_EQ(fixture.call(owner, "booking.pageCreate", bad).getString("code"), std::string("bad_request"));

    const Json created = fixture.call(owner, "booking.pageCreate", pagePayload("Консультация"));
    CHECK_EQ(created.getString("type"), std::string("ok"));
    const Json page = integration::payloadOf(created).get("page");
    const std::string slug = page.getString("slug");
    CHECK(selfvoy::BookingManager::validSlug(slug));
    CHECK_EQ(page.getString("url"), "https://selfvoy.example.com/#/b/" + slug);
    CHECK_EQ(page.getString("status"), std::string("active"));
    CHECK_EQ(page.getInt("max_series"), 3LL);

    // Гость без входа видит свободное время, но не email владельца.
    auto guest = sockets.open(fixture, "booking-guest");
    CHECK_EQ(fixture.call(guest, "booking.get", slugPayload("unknown000")).getString("code"),
             std::string("not_found"));
    CHECK_EQ(fixture.call(guest, "booking.get", slugPayload("../etc")).getString("code"), std::string("not_found"));
    const Json seen = integration::payloadOf(fixture.call(guest, "booking.get", slugPayload(slug))).get("page");
    CHECK_EQ(seen.getString("owner_name"), std::string("Ольга"));
    CHECK_EQ(seen.getString("title"), std::string("Консультация"));
    CHECK(seen.dump().find("coach@example.com") == std::string::npos);
    CHECK_EQ(seen.get("slots").items().size(), std::size_t(3));
    CHECK(hasSlot(seen, first) && hasSlot(seen, second) && hasSlot(seen, third));
    CHECK_EQ(seen.getString("ref").size(), std::size_t(8));
    {
        std::lock_guard<std::mutex> lock(ai.mutex);
        const auto request = std::find_if(ai.requests.begin(), ai.requests.end(), [](const std::string& r) {
            return r.find("POST /v1/agent/slots") != std::string::npos;
        });
        CHECK(request != ai.requests.end());
        if (request != ai.requests.end()) {
            CHECK(request->find("\"window_hours\":336") != std::string::npos);
            CHECK(request->find("\"lead_minutes\":60") != std::string::npos);
            CHECK(request->find("\"weekends\":false") != std::string::npos);
        }
    }

    // Запись: неверные данные, затем серия из двух встреч.
    CHECK_EQ(fixture.call(guest, "booking.book", bookPayload(slug, first + 600, "Пётр")).getString("code"),
             std::string("bad_request"));
    CHECK_EQ(fixture.call(guest, "booking.book", bookPayload(slug, first, " ")).getString("code"),
             std::string("bad_request"));
    CHECK_EQ(fixture.call(guest, "booking.book", bookPayload(slug, first, "Пётр", 4)).getString("code"),
             std::string("bad_request"));
    Json withEmail = bookPayload(slug, first, "Пётр", 2);
    withEmail.set("guest_email", Json("petr@example.com"));
    withEmail.set("comment", Json("Про стартап"));
    const Json booked = fixture.call(guest, "booking.book", withEmail);
    CHECK_EQ(booked.getString("type"), std::string("ok"));
    if (booked.getString("type") != "ok") {
        std::cout << "  booking.book: " << booked.dump() << "\n";
        return;
    }
    const std::string token = integration::payloadOf(booked).getString("token");
    CHECK_EQ(token.size(), std::size_t(43));
    CHECK_EQ(integration::payloadOf(booked).getString("url"), "https://selfvoy.example.com/#/m/" + token);
    const Json bookedView = integration::payloadOf(booked).get("booking");
    CHECK_EQ(bookedView.get("occurrences").items().size(), std::size_t(2));
    CHECK_EQ(bookedView.get("occurrences").at(1).getString("start"), selfvoy::isoUtc(first + 7 * 86400));
    CHECK_EQ(bookedView.get("occurrences").at(1).getString("local"), selfvoy::isoUtc(first + 7 * 86400).substr(0, 16));
    CHECK(isBusy(fixture, ownerId, first));
    CHECK(isBusy(fixture, ownerId, first + 7 * 86400));
    CHECK_EQ(countTasks(fixture, owner, "Запись: Консультация — Пётр"), 2);
    CHECK_EQ(countNotifications(fixture, owner, "booking.created"), 1);
    // В БД только хэш токена; повторная запись на занятое время — conflict.
    const auto stored = fixture.server->database().db().findBookingByToken(selfvoy::BookingManager::tokenHash(token));
    CHECK(stored.has_value());
    CHECK(stored && stored->manageHash != token);
    CHECK_EQ(fixture.call(guest, "booking.book", bookPayload(slug, first, "Анна")).getString("code"),
             std::string("conflict"));

    // Второй гость занимает second — лимит 2 записи в день скрывает third.
    const Json other = fixture.call(guest, "booking.book", bookPayload(slug, second, "Анна"));
    CHECK_EQ(other.getString("type"), std::string("ok"));
    const Json afterLimit = integration::payloadOf(fixture.call(guest, "booking.get", slugPayload(slug))).get("page");
    CHECK_EQ(afterLimit.get("slots").items().size(), std::size_t(0));
    CHECK_EQ(fixture.call(guest, "booking.book", bookPayload(slug, third, "Иван")).getString("code"),
             std::string("conflict"));

    // Ссылка управления: перенос ближайшей встречи на third (свой день не мешает лимиту).
    CHECK_EQ(fixture.call(guest, "booking.manage", tokenPayload(std::string(43, 'A'))).getString("code"),
             std::string("not_found"));
    const Json managed = integration::payloadOf(fixture.call(guest, "booking.manage", tokenPayload(token))).get("booking");
    CHECK_EQ(managed.getString("guest_name"), std::string("Пётр"));
    CHECK(managed.getBool("can_change"));
    CHECK(managed.getBool("can_cancel"));
    CHECK(managed.getBool("can_reschedule"));
    CHECK_EQ(managed.getInt("reschedules_left"), 3LL);
    CHECK(hasSlot(managed, third));
    Json move = tokenPayload(token);
    move.set("start", Json(selfvoy::isoUtc(third)));
    const Json moved = fixture.call(guest, "booking.reschedule", move);
    CHECK_EQ(moved.getString("type"), std::string("ok"));
    const Json movedView = integration::payloadOf(moved).get("booking");
    CHECK_EQ(movedView.get("occurrences").at(0).getString("start"), selfvoy::isoUtc(third));
    CHECK_EQ(movedView.getInt("reschedules_left"), 2LL);
    CHECK(!isBusy(fixture, ownerId, first));
    CHECK(isBusy(fixture, ownerId, third));
    CHECK_EQ(countTasks(fixture, owner, "Запись: Консультация — Пётр"), 2);
    CHECK_EQ(countNotifications(fixture, owner, "booking.rescheduled"), 1);
    // На занятое время перенести нельзя.
    move.set("start", Json(selfvoy::isoUtc(second)));
    CHECK_EQ(fixture.call(guest, "booking.reschedule", move).getString("code"), std::string("conflict"));

    // Отмена второй встречи серии, затем всех оставшихся.
    Json cancelOne = tokenPayload(token);
    cancelOne.set("occurrence", Json(selfvoy::isoUtc(first + 7 * 86400)));
    const Json oneCancelled = fixture.call(guest, "booking.cancel", cancelOne);
    CHECK_EQ(oneCancelled.getString("type"), std::string("ok"));
    CHECK_EQ(integration::payloadOf(oneCancelled).get("booking").getString("status"), std::string("active"));
    CHECK(!isBusy(fixture, ownerId, first + 7 * 86400));
    CHECK_EQ(countTasks(fixture, owner, "Запись: Консультация — Пётр"), 1);
    Json cancelAll = tokenPayload(token);
    cancelAll.set("reason", Json("заболел"));
    const Json allCancelled = fixture.call(guest, "booking.cancel", cancelAll);
    const Json cancelledView = integration::payloadOf(allCancelled).get("booking");
    CHECK_EQ(cancelledView.getString("status"), std::string("cancelled"));
    CHECK_EQ(cancelledView.getString("cancelled_by"), std::string("guest"));
    CHECK(!cancelledView.getBool("can_change"));
    CHECK(!isBusy(fixture, ownerId, third));
    CHECK_EQ(countTasks(fixture, owner, "Запись: Консультация — Пётр"), 0);
    CHECK_EQ(countNotifications(fixture, owner, "booking.cancelled"), 2);
    CHECK_EQ(fixture.call(guest, "booking.cancel", tokenPayload(token)).getString("code"), std::string("conflict"));

    // Письма гостю: подтверждение со ссылкой управления (имя гостя в письмо не попадает).
    {
        int created = 0, cancelledMail = 0, rescheduledMail = 0;
        for (const auto& notice : settledOutbox()) {
            if (notice.to != "petr@example.com") continue;
            if (notice.kind == "booking.created") {
                ++created;
                CHECK_EQ(notice.linkUrl, "https://selfvoy.example.com/#/m/" + token);
                CHECK_EQ(notice.lines.size(), std::size_t(2));
                // Тема — только текст сервера; название страницы — в теле письма.
                CHECK(notice.subject.find("Консультация") == std::string::npos);
                CHECK(notice.note.find("Консультация") != std::string::npos);
                CHECK(notice.title.find("Пётр") == std::string::npos && notice.lead.find("Пётр") == std::string::npos);
            }
            if (notice.kind == "booking.rescheduled") ++rescheduledMail;
            if (notice.kind == "booking.cancelled") ++cancelledMail;
        }
        CHECK_EQ(created, 1);
        CHECK_EQ(rescheduledMail, 1);
        CHECK_EQ(cancelledMail, 2);
    }

    // Владелец: список записей и отмена записи Анны.
    const Json bookings = integration::payloadOf(fixture.call(owner, "booking.list")).get("bookings");
    CHECK_EQ(bookings.items().size(), std::size_t(2));
    long long annaId = 0;
    for (const auto& item : bookings.items()) {
        CHECK_EQ(item.getString("page_title"), std::string("Консультация"));
        if (item.getString("guest_name") == "Анна") annaId = item.getInt("id");
        if (item.getString("guest_name") == "Пётр") {
            CHECK_EQ(item.getString("guest_email"), std::string("petr@example.com"));
        }
    }
    auto stranger = sockets.open(fixture, "booking-stranger");
    fixture.registerUser(stranger, "stranger@example.com", "Чужой");
    Json byBooking = Json::object();
    byBooking.set("booking_id", Json(annaId));
    CHECK_EQ(fixture.call(stranger, "booking.ownerCancel", byBooking).getString("code"), std::string("not_found"));
    const Json ownerCancelled = fixture.call(owner, "booking.ownerCancel", byBooking);
    CHECK_EQ(integration::payloadOf(ownerCancelled).get("booking").getString("cancelled_by"), std::string("owner"));
    CHECK(!isBusy(fixture, ownerId, second));

    // Потерянная ссылка: новая уходит письмом, старая перестаёт работать; чужой email —
    // тот же ответ, но без письма.
    Json withMail = bookPayload(slug, third, "Ира");
    withMail.set("guest_email", Json("ira@example.com"));
    const std::string iraToken = integration::payloadOf(fixture.call(guest, "booking.book", withMail)).getString("token");
    CHECK_EQ(iraToken.size(), std::size_t(43));
    Json resend = slugPayload(slug);
    resend.set("email", Json("IRA@example.com"));
    CHECK(integration::payloadOf(fixture.call(guest, "booking.resendLink", resend)).getBool("sent"));
    std::string newLink;
    for (const auto& notice : settledOutbox()) {
        if (notice.to == "ira@example.com" && notice.kind == "booking.link") newLink = notice.linkUrl;
    }
    CHECK(!newLink.empty());
    const std::string newToken = newLink.substr(newLink.rfind('/') + 1);
    CHECK(newToken != iraToken);
    CHECK_EQ(fixture.call(guest, "booking.manage", tokenPayload(iraToken)).getString("code"), std::string("not_found"));
    CHECK_EQ(fixture.call(guest, "booking.manage", tokenPayload(newToken)).getString("type"), std::string("ok"));
    resend.set("email", Json("nobody@example.com"));
    const std::size_t before = settledOutbox().size();
    CHECK(integration::payloadOf(fixture.call(guest, "booking.resendLink", resend)).getBool("sent"));
    CHECK_EQ(settledOutbox().size(), before);
    resend.set("email", Json("не-почта"));
    CHECK_EQ(fixture.call(guest, "booking.resendLink", resend).getString("code"), std::string("bad_request"));

    // Пауза: страница видна, но записаться нельзя; удаление — ссылка перестаёт работать.
    Json pause = Json::object();
    pause.set("page_id", Json(page.getInt("id")));
    pause.set("status", Json("paused"));
    CHECK_EQ(fixture.call(stranger, "booking.pageUpdate", pause).getString("code"), std::string("not_found"));
    CHECK_EQ(fixture.call(owner, "booking.pageUpdate", pause).getString("type"), std::string("ok"));
    const Json paused = integration::payloadOf(fixture.call(guest, "booking.get", slugPayload(slug))).get("page");
    CHECK_EQ(paused.getString("status"), std::string("paused"));
    CHECK_EQ(paused.get("slots").items().size(), std::size_t(0));
    CHECK_EQ(fixture.call(guest, "booking.book", bookPayload(slug, third, "Иван")).getString("code"),
             std::string("conflict"));
    // На паузе гость не переносит, но отменить может.
    {
        const Json view = integration::payloadOf(fixture.call(guest, "booking.manage", tokenPayload(newToken))).get("booking");
        CHECK(view.getBool("can_cancel"));
        CHECK(!view.getBool("can_reschedule"));
        CHECK(!view.getBool("can_change"));
    }
    Json remove = Json::object();
    remove.set("page_id", Json(page.getInt("id")));
    CHECK_EQ(fixture.call(owner, "booking.pageDelete", remove).getString("type"), std::string("ok"));
    CHECK_EQ(fixture.call(guest, "booking.get", slugPayload(slug)).getString("code"), std::string("not_found"));
    // Страница удалена — сделанная запись остаётся, отменить её гость по-прежнему может.
    {
        const Json view = integration::payloadOf(fixture.call(guest, "booking.manage", tokenPayload(newToken))).get("booking");
        CHECK(view.getBool("can_cancel"));
        CHECK(!view.getBool("can_reschedule"));
        CHECK_EQ(fixture.call(guest, "booking.cancel", tokenPayload(newToken)).getString("type"), std::string("ok"));
    }
    CHECK_EQ(integration::payloadOf(fixture.call(owner, "booking.pageList")).get("pages").items().size(),
             std::size_t(0));
    // Методы владельца без входа недоступны.
    CHECK_EQ(fixture.call(guest, "booking.pageList").getString("code"), std::string("unauthorized"));
}

TEST(booking_ai_unavailable) {
    using namespace booking_test;
    Fixture fixture;  // AI-сервис недоступен (порт 1)
    admin_test::Sockets sockets;
    auto owner = sockets.open(fixture, "booking-owner");
    fixture.registerUser(owner, "coach2@example.com", "Ольга");
    const Json created = fixture.call(owner, "booking.pageCreate", pagePayload("Созвон"));
    const std::string slug = integration::payloadOf(created).get("page").getString("slug");
    CHECK_EQ(integration::payloadOf(created).get("page").getString("url"), std::string());
    auto guest = sockets.open(fixture, "booking-guest");
    const Json seen = integration::payloadOf(fixture.call(guest, "booking.get", slugPayload(slug))).get("page");
    CHECK(seen.getBool("slots_unavailable"));
    CHECK_EQ(seen.get("slots").items().size(), std::size_t(0));
    CHECK_EQ(fixture.call(guest, "booking.book", bookPayload(slug, future(30), "Пётр")).getString("code"),
             std::string("upstream_error"));
    // Заблокированный владелец гостей не принимает (страница не служит каналом рассылки).
    const long long ownerId = fixture.server->database().db().findUserByEmail("coach2@example.com")->id;
    CHECK(fixture.server->database().db().setUserActive(ownerId, false).ok);
    CHECK_EQ(fixture.call(guest, "booking.get", slugPayload(slug)).getString("code"), std::string("not_found"));
    CHECK_EQ(fixture.call(guest, "booking.book", bookPayload(slug, future(30), "Пётр")).getString("code"),
             std::string("not_found"));
    CHECK(fixture.server->database().db().setUserActive(ownerId, true).ok);
    CHECK_EQ(fixture.call(guest, "booking.get", slugPayload(slug)).getString("type"), std::string("ok"));
}

// За доверенным прокси адрес клиента берётся из X-Forwarded-For (правый не-прокси),
// от остальных заголовок игнорируется — иначе лимиты гостей обходились бы подделкой.
TEST(session_forwarded_for_trusted_proxy) {
    const auto shake = [](const std::string& peer, const std::string& trusted, const std::string& xff) {
        selfvoy::Config config;
        config.trustedProxies = trusted;
        int fds[2] = {-1, -1};
        CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
        selfvoy::Session session(fds[1], "xff", peer, config);
        const std::string request = "GET / HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n"
                                    "X-Forwarded-For: " + xff + "\r\n\r\n";
        CHECK(::send(fds[0], request.data(), request.size(), 0) == static_cast<ssize_t>(request.size()));
        std::string error;
        CHECK(session.performHandshake(error));
        ::close(fds[0]);
        return session.remoteIp();
    };
    CHECK_EQ(shake("10.0.0.5:4000", "10.0.0.5", "1.2.3.4, 203.0.113.7"), std::string("203.0.113.7"));
    CHECK_EQ(shake("10.0.0.5:4000", "10.0.0.5, 10.0.0.6", "203.0.113.7, 10.0.0.6"), std::string("203.0.113.7"));
    CHECK_EQ(shake("198.51.100.1:4000", "10.0.0.5", "203.0.113.7"), std::string("198.51.100.1"));  // не прокси
    CHECK_EQ(shake("10.0.0.5:4000", "", "203.0.113.7"), std::string("10.0.0.5"));  // прокси не задан
    CHECK_EQ(shake("10.0.0.5:4000", "10.0.0.5", "<script>"), std::string("10.0.0.5"));  // мусор
}

// Одноразовые секреты: второе использование резервного кода 2FA и OAuth state не проходит
// (иначе два параллельных запроса с одним кодом оба считались бы верными).
TEST(one_time_secrets_single_use) {
    auto db = selfvoy::makeEmbeddedDatabase("");
    long long userId = 0;
    CHECK(db->createUser("once@example.com", "argon2id$x", "Раз", userId).ok);
    selfvoy::RecoveryCodeRecord code;
    code.id = "11111111-1111-1111-1111-111111111111";
    code.userId = userId;
    code.codeHash = std::string(64, 'a');
    CHECK(db->replaceRecoveryCodes(userId, {code}).ok);
    CHECK(db->useRecoveryCode(code.id).ok);
    CHECK(!db->useRecoveryCode(code.id).ok);
    selfvoy::OauthStateRecord state;
    state.state = "state-once";
    state.userId = userId;
    state.provider = "google";
    state.expiresAt = "2999-01-01T00:00:00Z";
    CHECK(db->createOauthState(state).ok);
    CHECK(db->useOauthState("state-once").ok);
    CHECK(!db->useOauthState("state-once").ok);
}

// Встроенная БД: id не переиспользуются — иначе task_id старой записи указал бы
// на задачу другого пользователя (и отмена записи удалила бы чужую задачу).
TEST(embedded_ids_not_reused) {
    auto db = selfvoy::makeEmbeddedDatabase("");
    long long anna = 0, boris = 0;
    CHECK(db->createUser("ids-anna@example.com", "argon2id$x", "Анна", anna).ok);
    CHECK(db->createUser("ids-boris@example.com", "argon2id$x", "Борис", boris).ok);
    selfvoy::TaskRecord task;
    task.userId = anna;
    task.title = "встреча";
    long long first = 0;
    CHECK(db->createTask(task, first).ok);
    CHECK(db->deleteTask(first).ok);
    task.userId = boris;
    long long second = 0;
    CHECK(db->createTask(task, second).ok);
    CHECK(second > first);
}

// Блокировка расписания — на владельца: чужой владелец не ждёт, тот же — ждёт release().
TEST(schedule_lock_per_owner) {
    selfvoy::DatabaseManager database(selfvoy::makeEmbeddedDatabase(""));
    selfvoy::ScheduleLock locks(database);
    auto held = locks.acquire(1);
    CHECK(held.ok());
    const auto started = std::chrono::steady_clock::now();
    std::thread other([&] { auto guard = locks.acquire(2); CHECK(guard.ok()); });
    other.join();
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(500));
    std::atomic<bool> got{false};
    std::thread same([&] { auto guard = locks.acquire(1); got = guard.ok(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(!got.load());
    held.release();
    same.join();
    CHECK(got.load());
}

// Ссылку, которая приходит только письмом, нельзя менять, если письма не уходят.
TEST(mail_provider_delivers) {
    selfvoy::Config config;
    config.mailDriver = "log";
    CHECK(!selfvoy::makeMailProvider(config)->delivers());
    config.mailDriver = "dev";
    config.devMode = true;
    CHECK(selfvoy::makeMailProvider(config)->delivers());
    config.mailDriver = "smtp";
    CHECK(selfvoy::makeMailProvider(config)->delivers());
}

TEST(referral_register_and_verify) {
    Fixture fixture;
    admin_test::Sockets sockets;
    auto host = sockets.open(fixture, "ref-host");
    fixture.registerUser(host, "host@example.com", "Ирина");
    const Json info = integration::payloadOf(fixture.call(host, "referral.get"));
    const std::string code = info.getString("code");
    CHECK_EQ(code.size(), std::size_t(8));
    CHECK_EQ(info.getInt("joined"), 0LL);
    // Код стабилен между запросами.
    CHECK_EQ(integration::payloadOf(fixture.call(host, "referral.get")).getString("code"), code);

    auto newcomer = sockets.open(fixture, "ref-new");
    Json payload = Json::object();
    payload.set("email", Json("friend@example.com"));
    payload.set("password", Json("selfvoy1234"));
    payload.set("display_name", Json("Саша"));
    std::string lower = code;
    for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    payload.set("ref", Json(lower));
    payload.set("ref_source", Json("booking"));
    const Json registered = fixture.call(newcomer, "auth.register", payload);
    CHECK_EQ(registered.getString("type"), std::string("ok"));
    Json before = integration::payloadOf(fixture.call(host, "referral.get"));
    CHECK_EQ(before.getInt("joined"), 1LL);
    CHECK_EQ(before.getInt("verified"), 0LL);
    CHECK_EQ(before.get("referrals").at(0).getString("display_name"), std::string("Саша"));
    CHECK_EQ(before.get("referrals").at(0).getString("source"), std::string("booking"));
    CHECK(before.dump().find("friend@example.com") == std::string::npos);

    Json verify = Json::object();
    verify.set("email", Json("friend@example.com"));
    verify.set("code", Json(integration::payloadOf(registered).getString("email_code")));
    CHECK_EQ(fixture.call(newcomer, "auth.verifyEmail", verify).getString("type"), std::string("ok"));
    const Json after = integration::payloadOf(fixture.call(host, "referral.get"));
    CHECK_EQ(after.getInt("verified"), 1LL);
    CHECK(after.get("referrals").at(0).getBool("verified"));
    CHECK_EQ(booking_test::countNotifications(fixture, host, "referral.joined"), 1);

    // Неверный код регистрацию не ломает и никому не засчитывается.
    auto other = sockets.open(fixture, "ref-other");
    payload.set("email", Json("other@example.com"));
    payload.set("ref", Json("ZZZZZZZZ"));
    CHECK_EQ(fixture.call(other, "auth.register", payload).getString("type"), std::string("ok"));
    payload.set("email", Json("third@example.com"));
    payload.set("ref", Json("<script>"));
    CHECK_EQ(fixture.call(other, "auth.register", payload).getString("type"), std::string("ok"));
    CHECK_EQ(integration::payloadOf(fixture.call(host, "referral.get")).getInt("joined"), 1LL);
    CHECK_EQ(fixture.call(other, "referral.get").getString("code"), std::string("unauthorized"));
}

// ============================================================ рост (S1a)
// https, тариф Pro, служебное состояние, шифр токена ссылки управления, Telegram-бот.
namespace growth_test {

#ifdef SELFVOY_HAVE_OPENSSL
// Локальный TLS-сервер с самоподписанным сертификатом на 127.0.0.1 (SAN IP):
// принимает accepts соединений, на запрос отвечает «200 ok» и рвёт соединение
// без close_notify (клиент должен остановиться по Content-Length).
struct TlsServer {
    EVP_PKEY* key = nullptr;
    X509* cert = nullptr;
    SSL_CTX* ctx = nullptr;
    int listener = -1;
    int port = 0;
    std::string certPath;
    std::thread thread;
    std::mutex mutex;
    std::vector<std::string> requests;

    explicit TlsServer(int accepts) {
        key = EVP_EC_gen("P-256");
        cert = X509_new();
        X509_set_version(cert, 2);
        ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
        X509_gmtime_adj(X509_getm_notBefore(cert), -60);
        X509_gmtime_adj(X509_getm_notAfter(cert), 3600);
        X509_set_pubkey(cert, key);
        X509_NAME* name = X509_get_subject_name(cert);
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("selfvoy-test"),
                                   -1, -1, 0);
        X509_set_issuer_name(cert, name);
        X509V3_CTX v3{};
        X509V3_set_ctx_nodb(&v3);
        X509V3_set_ctx(&v3, cert, cert, nullptr, nullptr, 0);
        for (const auto& [nid, value] : {std::make_pair(NID_subject_alt_name, "IP:127.0.0.1"),
                                         std::make_pair(NID_basic_constraints, "critical,CA:TRUE")}) {
            X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &v3, nid, value);
            X509_add_ext(cert, ext, -1);
            X509_EXTENSION_free(ext);
        }
        X509_sign(cert, key, EVP_sha256());
        certPath = "/tmp/selfvoy-test-ca-" + std::to_string(::getpid()) + ".pem";
        if (FILE* file = std::fopen(certPath.c_str(), "w")) {
            PEM_write_X509(file, cert);
            std::fclose(file);
        }
        ctx = SSL_CTX_new(TLS_server_method());
        SSL_CTX_use_certificate(ctx, cert);
        SSL_CTX_use_PrivateKey(ctx, key);

        listener = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
        ::listen(listener, 4);
        socklen_t len = sizeof addr;
        ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len);
        port = ntohs(addr.sin_port);
        thread = std::thread([this, accepts] {
            for (int i = 0; i < accepts; ++i) {
                const int fd = ::accept(listener, nullptr, nullptr);
                if (fd < 0) return;
                SSL* ssl = SSL_new(ctx);
                SSL_set_fd(ssl, fd);
                if (SSL_accept(ssl) == 1) {
                    std::string request;
                    char chunk[2048];
                    while (request.find("\r\n\r\n") == std::string::npos) {
                        const int n = SSL_read(ssl, chunk, sizeof chunk);
                        if (n <= 0) break;
                        request.append(chunk, static_cast<std::size_t>(n));
                    }
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        requests.push_back(request);
                    }
                    const std::string answer = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\n\r\nok";
                    SSL_write(ssl, answer.data(), static_cast<int>(answer.size()));
                }
                SSL_free(ssl);  // без SSL_shutdown: соединение рвётся без close_notify
                ::close(fd);
            }
        });
    }

    ~TlsServer() {
        ::shutdown(listener, SHUT_RDWR);
        ::close(listener);
        if (thread.joinable()) thread.join();
        SSL_CTX_free(ctx);
        X509_free(cert);
        EVP_PKEY_free(key);
        std::remove(certPath.c_str());
    }
};
#endif

// Поддельный Bot API: записывает вызовы, отдаёт апдейты, 403 для «заблокировавших» чатов.
struct FakeTelegram : selfvoy::TelegramBot::Transport {
    struct Call {
        std::string method;
        Json body;
    };
    std::mutex mutex;
    std::vector<Call> calls;
    std::vector<long long> blocked;
    Json updates = Json::array();

    selfvoy::net::HttpResponse call(const std::string& method, const Json& body, int, const std::atomic<bool>*,
                                    std::string&) override {
        std::lock_guard<std::mutex> lock(mutex);
        calls.push_back({method, body});
        selfvoy::net::HttpResponse response;
        response.status = 200;
        if (method == "sendMessage" &&
            std::find(blocked.begin(), blocked.end(), body.getInt("chat_id")) != blocked.end()) {
            response.status = 403;
            response.body = R"({"ok":false,"error_code":403,"description":"Forbidden: bot was blocked by the user"})";
            return response;
        }
        if (method == "getUpdates") {
            Json answer = Json::object();
            answer.set("ok", Json(true));
            answer.set("result", updates);
            updates = Json::array();
            response.body = answer.dump();
            return response;
        }
        response.body = R"({"ok":true,"result":true})";
        return response;
    }

    // Тексты sendMessage в чат (по порядку).
    std::vector<std::string> texts(long long chatId) {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<std::string> out;
        for (const auto& item : calls) {
            if (item.method == "sendMessage" && item.body.getInt("chat_id") == chatId) {
                out.push_back(item.body.getString("text"));
            }
        }
        return out;
    }
    std::string last(long long chatId) {
        const auto all = texts(chatId);
        return all.empty() ? std::string() : all.back();
    }
    std::vector<Call> of(const std::string& method) {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<Call> out;
        for (const auto& item : calls) {
            if (item.method == method) out.push_back(item);
        }
        return out;
    }
};

Json textUpdate(long long chatId, const std::string& text, const std::string& type = "private",
                const std::string& username = "tg_user") {
    Json chat = Json::object();
    chat.set("id", Json(chatId));
    chat.set("type", Json(type));
    Json from = Json::object();
    from.set("id", Json(chatId));
    from.set("username", Json(username));
    Json message = Json::object();
    message.set("message_id", Json(1LL));
    message.set("chat", chat);
    message.set("from", from);
    message.set("text", Json(text));
    Json update = Json::object();
    update.set("update_id", Json(1LL));
    update.set("message", message);
    return update;
}

Json callbackUpdate(long long chatId, const std::string& data, long long messageId = 77) {
    Json chat = Json::object();
    chat.set("id", Json(chatId));
    chat.set("type", Json("private"));
    Json message = Json::object();
    message.set("message_id", Json(messageId));
    message.set("chat", chat);
    Json callback = Json::object();
    callback.set("id", Json("cb-" + std::to_string(chatId)));
    callback.set("data", Json(data));
    callback.set("message", message);
    Json update = Json::object();
    update.set("update_id", Json(2LL));
    update.set("callback_query", callback);
    return update;
}

selfvoy::Config botConfig() {
    selfvoy::Config config;
    config.jwtSecret = "telegram-test-secret-0123456789abcdef";
    config.telegramBotToken = "1:test-token";
    config.telegramBotUsername = "selfvoy_test_bot";
    config.telegramPoll = false;
    return config;
}

std::string newToken() {
    std::string token = selfvoy::crypto::base64UrlEncode(selfvoy::crypto::randomBytes(32));
    while (!token.empty() && token.back() == '=') token.pop_back();
    return token;
}

// Запись с одной встречей через day суток (встроенная БД).
long long makeBooking(selfvoy::IDatabase& db, long long ownerId, const std::string& token, double days = 1.0) {
    selfvoy::BookingPageRecord page;
    page.ownerId = ownerId;
    page.slug = "tgpage" + std::to_string(ownerId % 10000);
    while (page.slug.size() < 10) page.slug += "0";
    page.title = "Разбор";
    long long pageId = 0;
    db.createBookingPage(page, pageId);
    const std::time_t start = std::time(nullptr) + static_cast<std::time_t>(days * 86400);
    Json occurrence = Json::object();
    occurrence.set("start", Json(selfvoy::isoUtc(start)));
    occurrence.set("end", Json(selfvoy::isoUtc(start + 3600)));
    occurrence.set("day", Json(selfvoy::isoUtc(start).substr(0, 10)));
    occurrence.set("local", Json(selfvoy::isoUtc(start).substr(0, 16)));
    occurrence.set("status", Json("active"));
    Json occurrences = Json::array();
    occurrences.push(occurrence);
    selfvoy::BookingRecord booking;
    booking.pageId = pageId;
    booking.ownerId = ownerId;
    booking.manageHash = selfvoy::BookingManager::tokenHash(token);
    booking.guestName = "Гость";
    booking.occurrences = occurrences;
    long long id = 0;
    db.createBooking(booking, id);
    return id;
}

bool hasChat(selfvoy::IDatabase& db, long long bookingId, long long chatId) {
    const auto chats = db.bookingTelegramChats(bookingId);
    return std::find(chats.begin(), chats.end(), chatId) != chats.end();
}

// Сервер Fixture с включённым ботом (без опроса) и поддельным Bot API.
FakeTelegram* enableBot(Fixture& fixture, const std::string& aiUrl = "", const std::string& publicUrl = "") {
    fixture.config.telegramBotToken = "1:test-token";
    fixture.config.telegramBotUsername = "selfvoy_test_bot";
    fixture.config.telegramPoll = false;
    if (!aiUrl.empty()) fixture.config.aiServiceUrl = aiUrl;
    fixture.config.publicUrl = publicUrl;
    fixture.server->stop();
    fixture.server = std::make_unique<selfvoy::Server>(fixture.config);
    std::string error;
    if (!fixture.server->start(error)) std::cout << "  не удалось перезапустить сервер: " << error << "\n";
    auto fake = std::make_unique<FakeTelegram>();
    FakeTelegram* raw = fake.get();
    fixture.server->telegram().setTransport(std::move(fake));
    return raw;
}

// payload из ссылки https://t.me/<бот>?start=<payload>.
std::string startPayload(const std::string& url) {
    const auto at = url.find("?start=");
    return at == std::string::npos ? std::string() : url.substr(at + 7);
}

}  // namespace growth_test

TEST(net_https_local_tls_and_ca) {
#ifdef SELFVOY_HAVE_OPENSSL
    CHECK(selfvoy::tls::available());
    growth_test::TlsServer server(2);
    const std::string url = "https://127.0.0.1:" + std::to_string(server.port) + "/v1/ping";
    // Свой корневой сертификат (SELFVOY_CA_FILE): сертификат и IP сверяются, ответ читается
    // по Content-Length, хотя сервер не прислал close_notify.
    selfvoy::net::setCaFile(server.certPath);
    std::string error;
    const auto ok = selfvoy::net::httpRequest("GET", url, {}, "", 5000, error);
    CHECK_EQ(error, std::string());
    CHECK_EQ(ok.status, 200);
    CHECK_EQ(ok.body, std::string("ok"));
    // Без своего CA самоподписанный сертификат не принимается.
    selfvoy::net::setCaFile("");
    error.clear();
    const auto rejected = selfvoy::net::httpRequest("GET", url, {}, "", 5000, error);
    CHECK_EQ(rejected.status, 0);
    CHECK(error.find("сертификат") != std::string::npos);
    {
        std::lock_guard<std::mutex> lock(server.mutex);
        CHECK_EQ(server.requests.size(), std::size_t(1));
        if (!server.requests.empty()) {
            CHECK(server.requests[0].find("GET /v1/ping HTTP/1.1") == 0);
            CHECK(server.requests[0].find("Host: 127.0.0.1:" + std::to_string(server.port)) != std::string::npos);
        }
    }
    // Нечитаемый файл CA — понятная ошибка до рукопожатия.
    selfvoy::net::setCaFile("/nonexistent/ca.pem");
    error.clear();
    selfvoy::net::httpRequest("GET", url, {}, "", 2000, error);
    CHECK(error.find("SELFVOY_CA_FILE") != std::string::npos);
    selfvoy::net::setCaFile("");
#else
    // Сборка без TLS: https-запрос — понятная ошибка, а не открытый текст.
    CHECK(!selfvoy::tls::available());
    std::string error;
    const auto response = selfvoy::net::httpRequest("GET", "https://127.0.0.1:1/", {}, "", 1000, error);
    CHECK_EQ(response.status, 0);
    CHECK(error.find("сборка без TLS") != std::string::npos);
#endif
}

// Долгий запрос (long polling) прерывается флагом остановки, а не ждёт таймаута.
TEST(net_request_cancel) {
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    ::listen(listener, 4);  // соединение примет ядро; ответа не будет
    socklen_t len = sizeof addr;
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len);
    std::atomic<bool> cancel{false};
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        cancel.store(true);
    });
    const auto started = std::chrono::steady_clock::now();
    std::string error;
    const auto response = selfvoy::net::httpRequest(
        "POST", "http://127.0.0.1:" + std::to_string(ntohs(addr.sin_port)) + "/getUpdates", {}, "{}", 20000, error,
        &cancel);
    stopper.join();
    CHECK_EQ(response.status, 0);
    CHECK_EQ(error, std::string("запрос прерван"));
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(3));
    ::close(listener);
}

TEST(telegram_signed_payloads) {
    selfvoy::Config config = growth_test::botConfig();
    selfvoy::DatabaseManager database(selfvoy::makeEmbeddedDatabase(""));
    selfvoy::TelegramBot bot(database, config, std::make_unique<growth_test::FakeTelegram>());
    const std::time_t now = std::time(nullptr);
    const auto safe = [](const std::string& payload) {
        return payload.size() <= 64 && std::all_of(payload.begin(), payload.end(), [](unsigned char ch) {
                   return std::isalnum(ch) != 0 || ch == '_' || ch == '-';
               });
    };

    // Владелец: подпись, срок 15 минут, подмена любой части.
    const std::string owner = bot.ownerPayload(123456789012LL, now + 900);
    CHECK(safe(owner));
    CHECK_EQ(bot.sign("x").size(), std::size_t(22));
    long long userId = 0;
    CHECK(bot.verifyOwner(owner, now, userId));
    CHECK_EQ(userId, 123456789012LL);
    CHECK(!bot.verifyOwner(owner, now + 901, userId));  // истекла
    CHECK_EQ(userId, 0LL);
    CHECK(!bot.verifyOwner(bot.ownerPayload(5, now + 7200), now, userId));  // срок дальше, чем выдаёт сервер
    std::string tampered = owner;
    tampered.back() = tampered.back() == 'A' ? 'B' : 'A';
    CHECK(!bot.verifyOwner(tampered, now, userId));
    std::string otherUser = owner;
    otherUser.replace(2, 1, otherUser[2] == '1' ? "2" : "1");
    CHECK(!bot.verifyOwner(otherUser, now, userId));
    const std::string longer = owner.substr(0, owner.rfind('_')) + "0" + owner.substr(owner.rfind('_'));
    CHECK(!bot.verifyOwner(longer, now, userId));  // продлённый срок — другая подпись
    CHECK(!bot.verifyOwner("o_1_2_short", now, userId));
    CHECK(!bot.verifyOwner("g_" + owner.substr(2), now, userId));
    CHECK(!bot.verifyOwner("", now, userId));

    // Гость: подпись над manage_hash — перевыпуск ссылки управления её отменяет.
    const std::string hash(64, 'a');
    const std::string guest = bot.guestPayload(42, hash);
    CHECK(safe(guest));
    CHECK(bot.verifyGuest(guest, 42, hash));
    CHECK(!bot.verifyGuest(guest, 43, hash));
    CHECK(!bot.verifyGuest(guest, 42, std::string(64, 'b')));
    CHECK(!bot.verifyGuest(guest, 42, ""));
    long long bookingId = 0;
    std::string sig;
    CHECK(selfvoy::TelegramBot::parseGuest(guest, bookingId, sig));
    CHECK_EQ(bookingId, 42LL);
    CHECK(!selfvoy::TelegramBot::parseGuest("g_042_" + sig, bookingId, sig));
    CHECK(!selfvoy::TelegramBot::parseGuest("g_-1_" + std::string(22, 'a'), bookingId, sig));
    CHECK(!selfvoy::TelegramBot::parseGuest("g_1_" + std::string(21, 'a') + "!", bookingId, sig));
    CHECK_EQ(bot.linkUrl(guest), "https://t.me/selfvoy_test_bot?start=" + guest);

    // Другой секрет сервера — подписи не совпадают.
    selfvoy::Config other = config;
    other.jwtSecret = "another-secret-0123456789abcdef0123";
    selfvoy::TelegramBot stranger(database, other, std::make_unique<growth_test::FakeTelegram>());
    CHECK(!stranger.verifyOwner(owner, now, userId));
    CHECK(!stranger.verifyGuest(guest, 42, hash));
}

TEST(telegram_bot_updates_with_fake_transport) {
    using namespace growth_test;
    selfvoy::Config config = botConfig();
    selfvoy::DatabaseManager database(selfvoy::makeEmbeddedDatabase(""));
    auto& db = database.db();
    auto fakeOwned = std::make_unique<FakeTelegram>();
    FakeTelegram* fake = fakeOwned.get();
    selfvoy::TelegramBot bot(database, config, std::move(fakeOwned));
    long long anna = 0, boris = 0;
    CHECK(db.createUser("tg-anna@example.com", "argon2id$x", "Анна", anna).ok);
    CHECK(db.createUser("tg-boris@example.com", "argon2id$x", "Борис", boris).ok);
    const std::time_t now = std::time(nullptr);

    // Владелец: /start o_… → чат привязан, ответ с подсказкой /stop.
    bot.handleUpdate(textUpdate(1001, "/start " + bot.ownerPayload(anna, now + 900), "private", "anna_tg"));
    bot.flush();
    CHECK_EQ(db.findUserById(anna)->telegramChatId, 1001LL);
    CHECK_EQ(db.findUserById(anna)->telegramUsername, std::string("anna_tg"));
    CHECK(fake->last(1001).find("Готово: сюда будут приходить новые записи") == 0);
    // Тот же чат другим аккаунтом — перепривязка к новому.
    bot.handleUpdate(textUpdate(1001, "/start@selfvoy_test_bot " + bot.ownerPayload(boris, now + 900)));
    bot.flush();
    CHECK_EQ(db.findUserById(anna)->telegramChatId, 0LL);
    CHECK_EQ(db.findUserById(boris)->telegramChatId, 1001LL);
    // Просроченная/поддельная ссылка и группа — не привязывают.
    bot.handleUpdate(textUpdate(1002, "/start " + bot.ownerPayload(anna, now - 1)));
    bot.handleUpdate(textUpdate(1003, "/start " + bot.ownerPayload(anna, now + 900), "group"));
    std::string forged = bot.ownerPayload(anna, now + 900);
    forged.back() = forged.back() == 'x' ? 'y' : 'x';
    bot.handleUpdate(textUpdate(1004, "/start " + forged));
    bot.flush();
    CHECK_EQ(db.findUserById(anna)->telegramChatId, 0LL);
    CHECK(fake->last(1002).find("недействительна") != std::string::npos);
    CHECK(fake->texts(1003).empty());
    CHECK(fake->last(1004).find("недействительна") != std::string::npos);
    // Любой другой текст — справка.
    bot.handleUpdate(textUpdate(1005, "привет"));
    bot.flush();
    CHECK(fake->last(1005).find("/stop") != std::string::npos);

    // Гость: /start g_… → чат записи; не больше 3 чатов.
    const std::string token = newToken();
    const long long booking = makeBooking(db, anna, token);
    const auto record = db.findBooking(booking);
    CHECK(record.has_value());
    const std::string guest = bot.guestPayload(booking, record->manageHash);
    CHECK_EQ(bot.guestUrl(*record), "https://t.me/selfvoy_test_bot?start=" + guest);
    bot.handleUpdate(textUpdate(2002, "/start " + guest));
    bot.handleUpdate(textUpdate(2003, "/start " + bot.guestPayload(booking, std::string(64, 'f'))));
    bot.flush();
    CHECK(hasChat(db, booking, 2002));
    CHECK(!hasChat(db, booking, 2003));
    CHECK(fake->last(2002).find("Готово") == 0);
    CHECK(fake->last(2003).find("недействительна") != std::string::npos);
    bot.handleUpdate(textUpdate(2004, "/start " + guest));
    bot.handleUpdate(textUpdate(2005, "/start " + guest));
    bot.handleUpdate(textUpdate(2006, "/start " + guest));
    bot.handleUpdate(textUpdate(2002, "/start " + guest));  // повтор тем же чатом — не новый
    bot.flush();
    CHECK_EQ(db.bookingTelegramChats(booking).size(), std::size_t(3));
    CHECK(!hasChat(db, booking, 2006));
    CHECK(fake->last(2006).find("уже подключено 3 чата") != std::string::npos);

    // «Приду»: из чужого чата — отказ; из привязанного — хук BookingManager (здесь нет — «недоступно»).
    const std::string cf = "cf:" + std::to_string(booking) + ":" + std::to_string(static_cast<long long>(now + 86400));
    bot.handleUpdate(callbackUpdate(3003, cf));
    bot.handleUpdate(callbackUpdate(2004, cf));
    bot.flush();
    const auto answers = fake->of("answerCallbackQuery");
    CHECK_EQ(answers.size(), std::size_t(2));
    if (answers.size() == 2) {
        CHECK(answers[0].body.getString("text").find("не для этого чата") != std::string::npos);
        CHECK(answers[1].body.getString("text").find("недоступно") != std::string::npos);
    }
    CHECK(fake->of("editMessageReplyMarkup").empty());

    // /stop — чат отвязан от записи (и от пользователя).
    bot.handleUpdate(textUpdate(2002, "/stop"));
    bot.handleUpdate(textUpdate(1001, "/STOP"));
    bot.flush();
    CHECK(!hasChat(db, booking, 2002));
    CHECK_EQ(db.findUserById(boris)->telegramChatId, 0LL);
    CHECK(fake->last(2002).find("больше не пишу") != std::string::npos);

    // 403 (бот заблокирован) — чат отвязывается везде.
    {
        std::lock_guard<std::mutex> lock(fake->mutex);
        fake->blocked.push_back(2004);
    }
    bot.sendMessage(2004, "напоминание");
    bot.flush();
    CHECK(!hasChat(db, booking, 2004));
    CHECK(hasChat(db, booking, 2005));

    // Опрос: апдейты обрабатываются, смещение — в server_state.
    {
        std::lock_guard<std::mutex> lock(fake->mutex);
        Json update = textUpdate(2005, "/stop");
        update.set("update_id", Json(41LL));
        fake->updates.push(update);
    }
    CHECK(bot.pollOnce(0));
    bot.flush();
    CHECK_EQ(db.getState(selfvoy::TelegramBot::kOffsetKey), std::string("42"));
    CHECK(!hasChat(db, booking, 2005));
    CHECK(bot.pollOnce(0));
    const auto polls = fake->of("getUpdates");
    CHECK_EQ(polls.size(), std::size_t(2));
    if (polls.size() == 2) {
        CHECK_EQ(polls[1].body.getInt("offset"), 42LL);
        CHECK_EQ(polls[1].body.get("allowed_updates").size(), std::size_t(2));
    }
    bot.stop();
    bot.sendMessage(2005, "после остановки");  // не принимается и не падает
}

TEST(server_state_and_manage_token_field) {
    auto db = selfvoy::makeEmbeddedDatabase("");
    CHECK_EQ(db->getState("telegram.offset"), std::string());
    CHECK(db->setState("telegram.offset", "10").ok);
    CHECK(db->setState("telegram.offset", "11").ok);
    CHECK(db->setState("other", "x").ok);
    CHECK_EQ(db->getState("telegram.offset"), std::string("11"));
    CHECK_EQ(db->getState("other"), std::string("x"));
    CHECK(db->tryLeaderLock(selfvoy::TelegramBot::kLeaderLockKey));

    // Шифр поля: тот же ключ и AAD — исходный текст; иначе — отказ.
    const std::string key = selfvoy::crypto::sha256("k");
    const std::string sealed = selfvoy::crypto::sealField(key, "token-value", "selfvoy-manage-token");
    CHECK(!sealed.empty());
    std::string plain;
    CHECK(selfvoy::crypto::openField(key, sealed, "selfvoy-manage-token", plain));
    CHECK_EQ(plain, std::string("token-value"));
    CHECK(!selfvoy::crypto::openField(key, sealed, "selfvoy-2fa", plain));
    CHECK(!selfvoy::crypto::openField(selfvoy::crypto::sha256("other"), sealed, "selfvoy-manage-token", plain));
    CHECK(!selfvoy::crypto::openField(key, "", "selfvoy-manage-token", plain));
}

TEST(plan_computation_and_admin_set_plan) {
    selfvoy::UserRecord user;
    const std::time_t now = std::time(nullptr);
    CHECK(!selfvoy::plan::isPro(user, now));
    user.planUntil = selfvoy::isoUtc(now + 3600);
    CHECK(selfvoy::plan::isPro(user, now));
    CHECK(!selfvoy::plan::isPro(user, now + 7200));
    user.planUntil = "мусор";
    CHECK(!selfvoy::plan::isPro(user, now));

    ::setenv("SELFVOY_ADMIN_EMAILS", "planadmin@example.com", 1);
    Fixture fixture;
    ::unsetenv("SELFVOY_ADMIN_EMAILS");
    admin_test::Sockets sockets;
    auto admin = sockets.open(fixture, "plan-admin");
    fixture.registerUser(admin, "planadmin@example.com", "Админ");
    auto member = sockets.open(fixture, "plan-user");
    const Json login = fixture.registerUser(member, "planuser@example.com", "Вера");
    const long long memberId = admin_test::userIdOf(login);
    // В каждом user — plan; бета (enforced=false): Pro-функции открыты и без Pro.
    const Json plan = login.get("payload").get("user").get("plan");
    CHECK_EQ(plan.getString("name"), std::string("free"));
    CHECK_EQ(plan.getString("until"), std::string());
    CHECK_EQ(plan.getBool("enforced", true), false);
    CHECK(fixture.server->proAllowed(memberId));

    Json payload = admin_test::byId(memberId);
    payload.set("days", Json(30LL));
    CHECK_EQ(fixture.call(member, "admin.setPlan", payload).getString("code"), std::string("forbidden"));
    const Json granted = fixture.call(admin, "admin.setPlan", payload);
    CHECK_EQ(granted.getString("type"), std::string("ok"));
    const Json grantedPlan = integration::payloadOf(granted).get("user").get("plan");
    CHECK_EQ(grantedPlan.getString("name"), std::string("pro"));
    std::time_t until = 0;
    CHECK(selfvoy::parseIsoUtc(grantedPlan.getString("until"), until));
    CHECK(std::llabs(static_cast<long long>(until - (now + 30 * 86400))) < 120);
    CHECK_EQ(integration::payloadOf(fixture.call(member, "auth.me")).get("plan").getString("name"), std::string("pro"));

    for (const Json& bad : {Json(-1LL), Json(3651LL), Json(1.5), Json("30")}) {
        Json wrong = admin_test::byId(memberId);
        wrong.set("days", bad);
        CHECK_EQ(fixture.call(admin, "admin.setPlan", wrong).getString("code"), std::string("bad_request"));
    }
    Json missing = admin_test::byId(999999);
    missing.set("days", Json(1LL));
    CHECK_EQ(fixture.call(admin, "admin.setPlan", missing).getString("code"), std::string("not_found"));
    bool audited = false;
    for (const auto& entry : fixture.server->database().db().listAudit(memberId, 20)) {
        if (entry.kind == "admin_plan_changed" && entry.detail.getInt("days") == 30) audited = true;
    }
    CHECK(audited);

    // Принудительные тарифы: без Pro — нельзя, с Pro — можно; enforced виден в user.plan.
    fixture.config.plansEnforced = true;
    fixture.server->stop();
    fixture.server = std::make_unique<selfvoy::Server>(fixture.config);
    std::string error;
    CHECK(fixture.server->start(error));
    CHECK(fixture.server->proAllowed(memberId));
    const long long adminId = fixture.server->database().db().findUserByEmail("planadmin@example.com")->id;
    CHECK(!fixture.server->proAllowed(adminId));
    auto relogged = sockets.open(fixture, "plan-relogin");
    const Json again = fixture.call(relogged, "auth.login", admin_test::loginPayload("planuser@example.com"));
    CHECK_EQ(again.get("payload").get("user").get("plan").getBool("enforced"), true);
    // Сброс (days = 0) — снова Free.
    auto adminAgain = sockets.open(fixture, "plan-admin2");
    fixture.call(adminAgain, "auth.login", admin_test::loginPayload("planadmin@example.com"));
    Json reset = admin_test::byId(memberId);
    reset.set("days", Json(0LL));
    const Json cleared = integration::payloadOf(fixture.call(adminAgain, "admin.setPlan", reset)).get("user");
    CHECK_EQ(cleared.get("plan").getString("name"), std::string("free"));
    CHECK_EQ(cleared.get("plan").getString("until"), std::string());
    CHECK(!fixture.server->proAllowed(memberId));
    CHECK_EQ(std::string(selfvoy::protocol::code::kProRequired), std::string("pro_required"));
    selfvoy::plan::setEnforced(false);
}

TEST(telegram_protocol_and_owner_notifications) {
    using namespace growth_test;
    Fixture fixture;
    admin_test::Sockets sockets;
    // Бот не настроен: статус выключен, ссылка — bad_request.
    auto plain = sockets.open(fixture, "tg-plain");
    fixture.registerUser(plain, "tgplain@example.com", "Пётр");
    const Json off = integration::payloadOf(fixture.call(plain, "telegram.status"));
    CHECK_EQ(off.getBool("enabled", true), false);
    const Json noBot = fixture.call(plain, "telegram.link");
    CHECK_EQ(noBot.getString("code"), std::string("bad_request"));
    CHECK_EQ(noBot.getString("message"), std::string("Telegram-бот не настроен на сервере"));
    CHECK_EQ(integration::payloadOf(fixture.call(plain, "server.info")).getString("telegram_bot"), std::string());

    FakeTelegram* fake = enableBot(fixture);
    auto owner = sockets.open(fixture, "tg-owner");
    fixture.registerUser(owner, "tgowner@example.com", "Ольга");
    const long long ownerId = fixture.server->database().db().findUserByEmail("tgowner@example.com")->id;
    auto anonymous = sockets.open(fixture, "tg-anon");
    CHECK_EQ(integration::payloadOf(fixture.call(anonymous, "server.info")).getString("telegram_bot"),
             std::string("selfvoy_test_bot"));
    CHECK_EQ(fixture.call(anonymous, "telegram.status").getString("code"), std::string("unauthorized"));
    const Json status = integration::payloadOf(fixture.call(owner, "telegram.status"));
    CHECK(status.getBool("enabled"));
    CHECK_EQ(status.getString("bot_username"), std::string("selfvoy_test_bot"));
    CHECK_EQ(status.getBool("linked", true), false);
    const Json link = integration::payloadOf(fixture.call(owner, "telegram.link"));
    const std::string url = link.getString("url");
    CHECK(url.find("https://t.me/selfvoy_test_bot?start=o_" + std::to_string(ownerId) + "_") == 0);
    std::time_t expires = 0;
    CHECK(selfvoy::parseIsoUtc(link.getString("expires_at"), expires));
    CHECK(expires > std::time(nullptr) + 800 && expires <= std::time(nullptr) + 900);

    auto& bot = fixture.server->telegram();
    bot.handleUpdate(textUpdate(7001, "/start " + startPayload(url), "private", "olga"));
    bot.flush();
    const Json linked = integration::payloadOf(fixture.call(owner, "telegram.status"));
    CHECK(linked.getBool("linked"));
    CHECK_EQ(linked.getString("username"), std::string("olga"));

    // Уведомления владельцу: booking.*, task.due — в чат; прочие виды — нет.
    auto& notifications = fixture.server->notifications();
    notifications.create(ownerId, "booking.created", "Анна записалась на встречу", "Консультация");
    notifications.create(ownerId, "login.new", "Новый вход в Selfvoy", "127.0.0.1");
    notifications.create(ownerId, "task.due", "Напоминание", "Позвонить");
    bot.flush();
    auto texts = fake->texts(7001);
    CHECK_EQ(texts.size(), std::size_t(3));  // ответ на /start + два уведомления
    if (texts.size() == 3) {
        CHECK_EQ(texts[1], std::string("Анна записалась на встречу\nКонсультация"));
        CHECK_EQ(texts[2], std::string("Напоминание\nПозвонить"));
    }
    // Отключённый вид и prefs.notifications.telegram = false — не уходят.
    Json prefs = Json::object();
    Json notify = Json::object();
    Json muted = Json::array();
    muted.push(Json("booking.cancelled"));
    notify.set("muted_kinds", muted);
    prefs.set("notifications", notify);
    CHECK_EQ(fixture.call(owner, "prefs.set", prefs).getString("type"), std::string("ok"));
    notifications.create(ownerId, "booking.cancelled", "Отмена", "Консультация");
    notify.set("telegram", Json(false));
    prefs.set("notifications", notify);
    CHECK_EQ(fixture.call(owner, "prefs.set", prefs).getString("type"), std::string("ok"));
    notifications.create(ownerId, "booking.rescheduled", "Перенос", "Консультация");
    bot.flush();
    CHECK_EQ(fake->texts(7001).size(), std::size_t(3));

    const Json unlinked = integration::payloadOf(fixture.call(owner, "telegram.unlink"));
    CHECK_EQ(unlinked.getBool("linked", true), false);
    CHECK_EQ(integration::payloadOf(fixture.call(owner, "telegram.status")).getBool("linked", true), false);
}

// Запись гостя: ссылка Telegram в ответе, письме и виде гостя; шифр токена ссылки
// управления; извещения гостю уходят и в привязанный чат.
TEST(telegram_booking_guest_channel) {
    using namespace booking_test;
    using namespace growth_test;
    const std::time_t first = future(30);
    const std::string day = selfvoy::isoUtc(first).substr(0, 10);
    FakeAi ai([&] {
        Json slots = Json::array();
        slots.push(daySlot(first, 60, day));
        slots.push(daySlot(first + 7200, 60, day));
        Json body = Json::object();
        body.set("slots", slots);
        return body.dump();
    });
    Fixture fixture;
    FakeTelegram* fake = enableBot(fixture, ai.url(), "https://selfvoy.example.com");
    admin_test::Sockets sockets;
    auto owner = sockets.open(fixture, "tgb-owner");
    fixture.registerUser(owner, "tgcoach@example.com", "Ольга");
    const std::string slug =
        integration::payloadOf(fixture.call(owner, "booking.pageCreate", pagePayload("Разбор"))).get("page").getString("slug");
    auto guest = sockets.open(fixture, "tgb-guest");
    Json payload = bookPayload(slug, first, "Пётр");
    payload.set("guest_email", Json("tg-guest@example.com"));
    selfvoy::devOutbox();
    const Json booked = integration::payloadOf(fixture.call(guest, "booking.book", payload));
    const std::string token = booked.getString("token");
    const std::string telegramUrl = booked.getString("telegram_url");
    CHECK(telegramUrl.find("https://t.me/selfvoy_test_bot?start=g_") == 0);
    CHECK_EQ(booked.get("booking").getString("telegram_url"), telegramUrl);
    CHECK_EQ(booked.get("booking").getBool("telegram_linked", true), false);

    // Письмо-подтверждение — со строкой «Напоминания в Telegram: <url>».
    bool mailed = false;
    for (const auto& mail : settledOutbox()) {
        if (mail.to != "tg-guest@example.com" || mail.kind != "booking.created") continue;
        mailed = std::find(mail.footer.begin(), mail.footer.end(), "Напоминания в Telegram: " + telegramUrl) !=
                 mail.footer.end();
    }
    CHECK(mailed);

    // Токен ссылки управления хранится шифром; хэш остаётся ключом поиска.
    auto& db = fixture.server->database().db();
    const auto stored = db.findBookingByToken(selfvoy::BookingManager::tokenHash(token));
    CHECK(stored.has_value());
    if (!stored) return;
    CHECK(!stored->manageTokenEnc.empty());
    CHECK(stored->manageTokenEnc.find(token) == std::string::npos);
    CHECK_EQ(fixture.server->bookings().manageTokenOf(*stored), token);
    selfvoy::BookingRecord legacy = *stored;
    legacy.manageTokenEnc.clear();
    CHECK_EQ(fixture.server->bookings().manageTokenOf(legacy), std::string());

    // Гость подключает Telegram по ссылке — вид гостя это показывает.
    auto& bot = fixture.server->telegram();
    bot.handleUpdate(textUpdate(8001, "/start " + startPayload(telegramUrl)));
    bot.flush();
    CHECK(fake->last(8001).find("Готово: напоминания о записи «Разбор» (Ольга)") == 0);
    CHECK(fake->last(8001).find("Ближайшая встреча:") != std::string::npos);
    const Json managed = integration::payloadOf(fixture.call(guest, "booking.manage", tokenPayload(token))).get("booking");
    CHECK(managed.getBool("telegram_linked"));
    CHECK_EQ(managed.getString("telegram_url"), telegramUrl);

    // Перевыпуск ссылки управления: шифр — новый токен, прежняя Telegram-ссылка недействительна.
    Json resend = slugPayload(slug);
    resend.set("email", Json("tg-guest@example.com"));
    CHECK_EQ(fixture.call(guest, "booking.resendLink", resend).getString("type"), std::string("ok"));
    fixture.server->bookings().flushMail();
    const auto reissued = db.findBooking(stored->id);
    CHECK(reissued && reissued->manageHash != stored->manageHash);
    if (reissued) {
        const std::string fresh = fixture.server->bookings().manageTokenOf(*reissued);
        CHECK_EQ(selfvoy::BookingManager::tokenHash(fresh), reissued->manageHash);
        CHECK(!bot.verifyGuest(startPayload(telegramUrl), reissued->id, reissued->manageHash));
        // Новая ссылка ушла и в Telegram-чат гостя (url-кнопкой: адрес https).
        bot.flush();
        const auto sent = fake->of("sendMessage");
        bool button = false;
        for (const auto& item : sent) {
            if (item.body.getInt("chat_id") != 8001) continue;
            const Json keyboard = item.body.get("reply_markup").get("inline_keyboard");
            if (keyboard.size() == 1 && keyboard.at(0).at(0).getString("url") ==
                                            "https://selfvoy.example.com/#/m/" + fresh) {
                button = true;
            }
        }
        CHECK(button);
        // Гость отменяет — извещение в чат.
        CHECK_EQ(fixture.call(guest, "booking.cancel", tokenPayload(fresh)).getString("type"), std::string("ok"));
        fixture.server->bookings().flushMail();
        bot.flush();
        CHECK(fake->last(8001).find("Запись отменена") == 0);
        CHECK(fake->last(8001).find("«Разбор» — Ольга") != std::string::npos);
        CHECK(fake->last(8001).find("Пётр") == std::string::npos);  // имя гостя в извещения не попадает
    }
}

#ifdef SELFVOY_WITH_LIBPQ
// PostgreSQL: состояние, тариф, Telegram-привязки и лидерство опроса (SELFVOY_TEST_PG_URL).
TEST(pg_growth_methods) {
    const char* url = std::getenv("SELFVOY_TEST_PG_URL");
    if (url == nullptr || *url == '\0') {
        std::cout << "  (пропущен: SELFVOY_TEST_PG_URL не задан)\n";
        return;
    }
    auto first = selfvoy::makePostgresDatabase(url);
    auto second = selfvoy::makePostgresDatabase(url);
    std::string error;
    CHECK(first && first->connect(error));
    CHECK(second && second->connect(error));
    if (!first || !second) return;
    const std::string key = "test." + selfvoy::crypto::randomHex(4);
    CHECK_EQ(first->getState(key), std::string());
    CHECK(first->setState(key, "1").ok);
    CHECK(first->setState(key, "2").ok);
    CHECK_EQ(second->getState(key), std::string("2"));

    // Лидер: повторный вызов у держателя — true; другой процесс (соединение) — false,
    // после release — может взять.
    const long long lockKey = 0x53564F59544C00LL + (std::time(nullptr) % 200);
    CHECK(first->tryLeaderLock(lockKey));
    CHECK(first->tryLeaderLock(lockKey));
    CHECK(!second->tryLeaderLock(lockKey));
    first->releaseLeaderLock(lockKey);
    CHECK(second->tryLeaderLock(lockKey));
    CHECK(!first->tryLeaderLock(lockKey));
    second->releaseLeaderLock(lockKey);

    long long a = 0, b = 0;
    const std::string suffix = selfvoy::crypto::randomHex(4);
    CHECK(first->createUser("pg-tg-a-" + suffix + "@example.com", "argon2id$x", "А", a).ok);
    CHECK(first->createUser("pg-tg-b-" + suffix + "@example.com", "argon2id$x", "Б", b).ok);
    const long long chat = 900000000000LL + (std::time(nullptr) % 100000);
    CHECK(first->setUserTelegram(a, chat, "anna").ok);
    CHECK_EQ(first->findUserById(a)->telegramChatId, chat);
    CHECK(first->setUserTelegram(b, chat, "boris").ok);  // перепривязка
    CHECK_EQ(first->findUserById(a)->telegramChatId, 0LL);
    CHECK_EQ(first->findUserById(b)->telegramUsername, std::string("boris"));
    CHECK(first->setUserPlanDays(b, 10).ok);
    CHECK(selfvoy::plan::isPro(*first->findUserById(b)));
    CHECK(first->setUserPlanDays(b, 0).ok);
    CHECK_EQ(first->findUserById(b)->planUntil, std::string());

    const std::string token = growth_test::newToken();
    const long long booking = growth_test::makeBooking(*first, a, token);
    CHECK(booking > 0);
    CHECK(first->linkBookingTelegram(booking, chat, 3));
    CHECK(first->linkBookingTelegram(booking, chat, 3));
    CHECK(first->linkBookingTelegram(booking, chat + 1, 3));
    CHECK(first->linkBookingTelegram(booking, chat + 2, 3));
    CHECK(!first->linkBookingTelegram(booking, chat + 3, 3));
    CHECK_EQ(first->bookingTelegramChats(booking).size(), std::size_t(3));
    CHECK(first->unlinkTelegramChat(chat).ok);
    CHECK_EQ(first->bookingTelegramChats(booking).size(), std::size_t(2));
    CHECK_EQ(first->findUserById(b)->telegramChatId, 0LL);

    auto record = first->findBooking(booking);
    CHECK(record && record->manageTokenEnc.empty());
    if (record) {
        record->manageTokenEnc = "sealed-value";
        CHECK(first->updateBooking(*record).ok);
        CHECK_EQ(first->findBooking(booking)->manageTokenEnc, std::string("sealed-value"));
        record->manageTokenEnc.clear();  // пустое не затирает
        CHECK(first->updateBooking(*record).ok);
        CHECK_EQ(first->findBooking(booking)->manageTokenEnc, std::string("sealed-value"));
    }
}
#endif

// ------------------------------------------- F6: пост «свободные окна» и ICS-подписка
namespace feed_test {

// Склеить сложенные строки RFC 5545 обратно (CRLF + пробел — продолжение).
std::string unfold(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text.compare(i, 3, "\r\n ") == 0) {
            i += 2;
            continue;
        }
        out.push_back(text[i]);
    }
    return out;
}

// Каждая физическая строка не длиннее 75 октетов, заканчивается CRLF и не начинается
// с продолжающего байта UTF-8 (символ не разорван).
bool foldedWell(const std::string& text) {
    if (text.size() < 2 || text.compare(text.size() - 2, 2, "\r\n") != 0) return false;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find("\r\n", start);
        if (end == std::string::npos) return false;
        if (end - start > 75) return false;
        if (text.find('\n', start) < end || text.find('\r', start) < end) return false;
        std::size_t first = start;
        if (start != 0 && first < end && text[first] == ' ') ++first;
        if (first < end && (static_cast<unsigned char>(text[first]) & 0xC0) == 0x80) return false;
        start = end + 2;
    }
    return true;
}

// Слот AI с местным временем владельца (UTC+3, как Europe/Moscow).
Json localSlot(std::time_t start, int minutes) {
    Json json = invite_test::slot(start, minutes);
    const std::string local = selfvoy::isoUtc(start + 3 * 3600).substr(0, 16);
    json.set("day", Json(local.substr(0, 10)));
    json.set("local", Json(local));
    return json;
}

std::string httpGet(Fixture& fixture, const std::string& path, selfvoy::net::HttpResponse& response,
                    const std::string& method = "GET") {
    std::string error;
    response = selfvoy::net::httpRequest(method, "http://127.0.0.1:" + std::to_string(fixture.server->port()) + path,
                                         {}, "", 3000, error);
    return error;
}

}  // namespace feed_test

TEST(ics_escape_fold_and_stamp) {
    using selfvoy::ics::escapeText;
    using selfvoy::ics::foldLine;
    CHECK_EQ(escapeText("a\\b;c,d"), std::string("a\\\\b\\;c\\,d"));
    CHECK_EQ(escapeText("строка 1\nстрока 2\r\nстрока 3\rконец"),
             std::string("строка 1\\nстрока 2\\nстрока 3\\nконец"));
    CHECK_EQ(escapeText(std::string("x\x01y\x7Fz\tw")), std::string("xyz\tw"));
    CHECK_EQ(escapeText("Иван: «привет»"), std::string("Иван: «привет»"));  // «:» в TEXT не экранируется

    CHECK_EQ(foldLine("SUMMARY:коротко"), std::string("SUMMARY:коротко\r\n"));
    CHECK_EQ(foldLine(""), std::string("\r\n"));
    const std::string ascii = "DESCRIPTION:" + std::string(200, 'a');
    const std::string foldedAscii = foldLine(ascii);
    CHECK(feed_test::foldedWell(foldedAscii));
    CHECK_EQ(foldedAscii.substr(0, 77), ascii.substr(0, 75) + "\r\n");
    CHECK_EQ(foldedAscii.substr(77, 1), std::string(" "));
    CHECK_EQ(feed_test::unfold(foldedAscii), ascii + "\r\n");
    // Кириллица (2 октета), «€» (3), эмодзи (4) и сдвиг на нечётную позицию: граница
    // строки не попадает внутрь символа, склейка возвращает исходную строку.
    for (const std::string& unit : {std::string("я"), std::string("€"), std::string("\xF0\x9F\x99\x82")}) {
        for (int shift = 0; shift < 4; ++shift) {
            std::string line = "SUMMARY:" + std::string(static_cast<std::size_t>(shift), 'x');
            for (int i = 0; i < 60; ++i) line += unit;
            const std::string folded = foldLine(line);
            CHECK(feed_test::foldedWell(folded));
            CHECK_EQ(feed_test::unfold(folded), line + "\r\n");
            CHECK(folded.find("\r\n ") != std::string::npos);
        }
    }
    CHECK_EQ(selfvoy::ics::utcStamp(0), std::string("19700101T000000Z"));
    std::time_t moment = 0;
    CHECK(selfvoy::parseIsoUtc("2026-10-21T07:30:05Z", moment));
    CHECK_EQ(selfvoy::ics::utcStamp(moment), std::string("20261021T073005Z"));
    CHECK_EQ(selfvoy::CalendarFeed::tokenHashFromPath("/ics/short.ics"), std::string());
    CHECK_EQ(selfvoy::CalendarFeed::tokenHashFromPath("/ics/" + std::string(43, '/') + ".ics"), std::string());
    CHECK_EQ(selfvoy::CalendarFeed::tokenHashFromPath("/ics/" + std::string(43, 'A') + ".ics").size(), std::size_t(64));
    CHECK_EQ(selfvoy::CalendarFeed::tokenHashFromPath("/ics/" + std::string(43, 'A') + ".ics?x=1"),
             selfvoy::CalendarFeed::tokenHashFromPath("/ics/" + std::string(43, 'A') + ".ics"));
}

TEST(slotspost_render_text) {
    namespace sp = selfvoy::slotspost;
    CHECK_EQ(sp::dayLabel("2026-10-19"), std::string("Пн, 19 окт"));
    CHECK_EQ(sp::dayLabel("2026-12-31"), std::string("Чт, 31 дек"));
    CHECK_EQ(sp::dayLabel("2026-13-01"), std::string());
    CHECK_EQ(sp::durationLabel(50), std::string("50 мин"));
    CHECK_EQ(sp::durationLabel(60), std::string("1 ч"));
    CHECK_EQ(sp::durationLabel(90), std::string("1 ч 30 мин"));
    CHECK_EQ(sp::moneyLabel(250000, "RUB"), std::string("2 500 ₽"));
    CHECK_EQ(sp::moneyLabel(123456789, "USD"), std::string("1 234 567,89 $"));
    CHECK_EQ(sp::moneyLabel(50000, "EUR"), std::string("500 €"));
    CHECK_EQ(sp::moneyLabel(0, "RUB"), std::string());

    sp::Input input;
    input.title = "Консультация";
    input.durationMinutes = 50;
    input.price = 250000;
    input.locals = {"2026-10-19T10:00", "2026-10-19T11:30", "2026-10-19T15:00", "2026-10-20T12:00"};
    input.url = "https://selfvoy.example.com/#/b/abcdefghij";
    CHECK_EQ(sp::render(input), std::string("Свободные окна: Консультация (50 мин, 2 500 ₽)\n"
                                            "\n"
                                            "Пн, 19 окт: 10:00, 11:30, 15:00\n"
                                            "Вт, 20 окт: 12:00\n"
                                            "\n"
                                            "Записаться: https://selfvoy.example.com/#/b/abcdefghij"));
    input.price = 0;  // цена не задана — в заголовке только длительность
    input.format = "vk";
    CHECK_EQ(sp::render(input), std::string("Свободные окна: Консультация (50 мин)\n"
                                            "\n"
                                            "Пн, 19 окт: 10:00, 11:30, 15:00\n"
                                            "Вт, 20 окт: 12:00\n"
                                            "\n"
                                            "Запись онлайн: https://selfvoy.example.com/#/b/abcdefghij"));
    input.format = "plain";
    CHECK_EQ(sp::render(input), std::string("Свободные окна: Консультация (50 мин)\n"
                                            "Пн, 19 окт: 10:00, 11:30, 15:00\n"
                                            "Вт, 20 окт: 12:00\n"
                                            "Записаться: https://selfvoy.example.com/#/b/abcdefghij"));
    // Больше 8 времён в дне — «и ещё N».
    input.format = "telegram";
    input.locals.clear();
    for (int hour = 8; hour < 19; ++hour) {
        input.locals.push_back("2026-10-19T" + std::to_string(hour / 10) + std::to_string(hour % 10) + ":00");
    }
    const std::string many = sp::render(input);
    CHECK(many.find("Пн, 19 окт: 08:00, 09:00, 10:00, 11:00, 12:00, 13:00, 14:00, 15:00 и ещё 3\n") !=
          std::string::npos);
    // Окон нет.
    input.locals.clear();
    CHECK_EQ(sp::render(input), std::string("Свободные окна: Консультация (50 мин)\n"
                                            "\n"
                                            "На ближайшие 7 дней свободных окон нет\n"
                                            "\n"
                                            "Записаться: https://selfvoy.example.com/#/b/abcdefghij"));
    input.days = 3;
    CHECK(sp::render(input).find("На ближайшие 3 дня свободных окон нет") != std::string::npos);
    input.days = 1;
    CHECK(sp::render(input).find("На ближайший день свободных окон нет") != std::string::npos);
    input.days = 11;
    input.url.clear();
    CHECK_EQ(sp::render(input), std::string("Свободные окна: Консультация (50 мин)\n\n"
                                            "На ближайшие 11 дней свободных окон нет"));
}

TEST(booking_slots_post_flow) {
    using namespace booking_test;
    const std::time_t first = future(26);
    const std::time_t second = first + 2 * 3600;
    const std::time_t far = first + 10 * 86400;  // за пределами days=7
    FakeAi ai([&] {
        Json slots = Json::array();
        slots.push(feed_test::localSlot(first, 60));
        slots.push(feed_test::localSlot(second, 60));
        slots.push(feed_test::localSlot(far, 60));
        Json body = Json::object();
        body.set("slots", slots);
        return body.dump();
    });
    Fixture fixture;
    invite_test::useAi(fixture, ai.url(), "https://selfvoy.example.com");
    admin_test::Sockets sockets;
    auto owner = sockets.open(fixture, "post-owner");
    fixture.registerUser(owner, "post@example.com", "Ольга");
    const Json page = integration::payloadOf(fixture.call(owner, "booking.pageCreate", pagePayload("Консультация")))
                          .get("page");
    const long long pageId = page.getInt("id");
    const std::string slug = page.getString("slug");

    Json request = Json::object();
    request.set("page_id", Json(pageId));
    const Json posted = fixture.call(owner, "booking.slotsPost", request);
    CHECK_EQ(posted.getString("type"), std::string("ok"));
    const Json post = integration::payloadOf(posted);
    const std::string text = post.getString("text");
    CHECK_EQ(post.getInt("slots"), 2LL);
    CHECK_EQ(post.getString("url"), "https://selfvoy.example.com/#/b/" + slug);
    CHECK(text.rfind("Свободные окна: Консультация (1 ч)\n\n", 0) == 0);
    CHECK(text.find("Записаться: https://selfvoy.example.com/#/b/" + slug) != std::string::npos);
    const std::string firstLocal = selfvoy::isoUtc(first + 3 * 3600);
    const std::string secondLocal = selfvoy::isoUtc(second + 3 * 3600);
    CHECK(text.find(selfvoy::slotspost::dayLabel(firstLocal.substr(0, 10)) + ": " + firstLocal.substr(11, 5)) !=
          std::string::npos);
    CHECK(text.find(selfvoy::slotspost::dayLabel(selfvoy::isoUtc(far + 3 * 3600).substr(0, 10))) ==
          std::string::npos);

    // Занятое гостем окно в пост не попадает (та же логика, что booking.get).
    auto guest = sockets.open(fixture, "post-guest");
    CHECK_EQ(fixture.call(guest, "booking.book", bookPayload(slug, second, "Пётр")).getString("type"),
             std::string("ok"));
    const Json afterBook = integration::payloadOf(fixture.call(owner, "booking.slotsPost", request));
    CHECK_EQ(afterBook.getInt("slots"), 1LL);
    CHECK(afterBook.getString("text").find(secondLocal.substr(11, 5)) == std::string::npos ||
          secondLocal.substr(11, 5) == firstLocal.substr(11, 5));
    request.set("days", Json(14LL));
    request.set("format", Json("plain"));
    const Json wide = integration::payloadOf(fixture.call(owner, "booking.slotsPost", request));
    CHECK_EQ(wide.getInt("slots"), 2LL);
    CHECK(wide.getString("text").find("\n\n") == std::string::npos);

    // Проверка ввода и владельца.
    Json bad = request;
    bad.set("days", Json(15LL));
    CHECK_EQ(fixture.call(owner, "booking.slotsPost", bad).getString("code"), std::string("bad_request"));
    bad.set("days", Json(2.5));
    CHECK_EQ(fixture.call(owner, "booking.slotsPost", bad).getString("code"), std::string("bad_request"));
    bad = request;
    bad.set("format", Json("html"));
    CHECK_EQ(fixture.call(owner, "booking.slotsPost", bad).getString("code"), std::string("bad_request"));
    CHECK_EQ(fixture.call(guest, "booking.slotsPost", request).getString("code"), std::string("unauthorized"));
    auto stranger = sockets.open(fixture, "post-stranger");
    fixture.registerUser(stranger, "stranger@example.com", "Чужой");
    CHECK_EQ(fixture.call(stranger, "booking.slotsPost", request).getString("code"), std::string("not_found"));

    // Лимит — 30 постов в час на владельца.
    int limitedAt = 0;
    for (int i = 0; i < 40 && limitedAt == 0; ++i) {
        if (fixture.call(owner, "booking.slotsPost", request).getString("code") == "forbidden") limitedAt = i + 1;
    }
    CHECK(limitedAt > 0 && limitedAt <= selfvoy::BookingManager::kSlotsPostPerHour);
}

TEST(booking_slots_post_ai_unavailable) {
    using namespace booking_test;
    Fixture fixture;  // AI-сервис недоступен (порт 1)
    admin_test::Sockets sockets;
    auto owner = sockets.open(fixture, "post-owner");
    fixture.registerUser(owner, "post2@example.com", "Ольга");
    const Json page = integration::payloadOf(fixture.call(owner, "booking.pageCreate", pagePayload("Созвон")))
                          .get("page");
    Json request = Json::object();
    request.set("page_id", Json(page.getInt("id")));
    CHECK_EQ(fixture.call(owner, "booking.slotsPost", request).getString("code"), std::string("upstream_error"));
}

TEST(calendar_feed_ics_over_http) {
    using namespace booking_test;
    const std::time_t first = future(26);
    FakeAi ai([&] {
        Json slots = Json::array();
        slots.push(feed_test::localSlot(first, 60));
        Json body = Json::object();
        body.set("slots", slots);
        return body.dump();
    });
    Fixture fixture;
    fixture.config.serverPublicUrl = "https://api.example.com";
    invite_test::useAi(fixture, ai.url(), "https://selfvoy.example.com");
    admin_test::Sockets sockets;
    auto owner = sockets.open(fixture, "feed-owner");
    fixture.registerUser(owner, "feed@example.com", "Ольга");
    const long long ownerId = fixture.server->database().db().findUserByEmail("feed@example.com")->id;
    const std::string slug =
        integration::payloadOf(fixture.call(owner, "booking.pageCreate", pagePayload("Разбор; проекта, v2")))
            .get("page")
            .getString("slug");
    auto guest = sockets.open(fixture, "feed-guest");
    Json book = bookPayload(slug, first, "Пётр");
    book.set("comment", Json("Про стартап, деньги; планы\nи команду"));
    const Json booked = fixture.call(guest, "booking.book", book);
    CHECK_EQ(booked.getString("type"), std::string("ok"));
    const auto ownBookings = fixture.server->database().db().listBookings(ownerId, 0, true, 10);
    CHECK_EQ(ownBookings.size(), std::size_t(1));
    const long long bookingId = ownBookings.empty() ? 0 : ownBookings.front().id;

    // Принятое приглашение тоже попадает в календарь, открытое — нет.
    selfvoy::InviteRecord invite;
    invite.ownerId = ownerId;
    invite.tokenHash = std::string(64, 'c');
    invite.topic = "Созвон";
    invite.expiresAt = selfvoy::isoUtc(std::time(nullptr) + 86400);
    long long inviteId = 0;
    CHECK(fixture.server->database().db().createInvite(invite, inviteId).ok);
    selfvoy::InviteRecord response;
    response.guestName = "Анна";
    response.chosen = Json::object();
    response.chosen.set("start", Json(selfvoy::isoUtc(first + 86400)));
    response.chosen.set("end", Json(selfvoy::isoUtc(first + 86400 + 1800)));
    CHECK(fixture.server->database().db().closeInvite(inviteId, "accepted", response));
    invite.tokenHash = std::string(64, 'd');
    long long openId = 0;
    CHECK(fixture.server->database().db().createInvite(invite, openId).ok);

    // До первой ссылки подписка выключена.
    Json feed = integration::payloadOf(fixture.call(owner, "calendar.feed"));
    CHECK(!feed.getBool("enabled", true));
    CHECK_EQ(feed.getString("path"), std::string());
    CHECK_EQ(fixture.call(guest, "calendar.feed").getString("code"), std::string("unauthorized"));

    const Json reset = integration::payloadOf(fixture.call(owner, "calendar.feedReset"));
    CHECK(reset.getBool("enabled"));
    const std::string path = reset.getString("path");
    CHECK_EQ(path.size(), std::string("/ics/").size() + 43 + 4);
    CHECK(path.rfind("/ics/", 0) == 0);
    CHECK_EQ(reset.getString("url"), "https://api.example.com" + path);
    // В БД — только хэш токена; повторный calendar.feed ссылку не показывает.
    const std::string token = path.substr(5, 43);
    CHECK_EQ(fixture.server->database().db().icsTokenHash(ownerId),
             selfvoy::crypto::toHex(selfvoy::crypto::sha256(token)));
    feed = integration::payloadOf(fixture.call(owner, "calendar.feed"));
    CHECK(feed.getBool("enabled"));
    CHECK_EQ(feed.getString("url"), std::string());
    CHECK_EQ(feed.getString("path"), std::string());

    selfvoy::net::HttpResponse http;
    CHECK_EQ(feed_test::httpGet(fixture, path, http), std::string());
    CHECK_EQ(http.status, 200);
    CHECK_EQ(http.header("content-type"), std::string("text/calendar; charset=utf-8"));
    CHECK_EQ(http.header("cache-control"), std::string("no-store"));
    const std::string body = http.body;
    CHECK(body.rfind("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n", 0) == 0);
    CHECK(body.find("X-WR-CALNAME:Selfvoy\r\n") != std::string::npos);
    CHECK(body.size() >= 15 && body.compare(body.size() - 15, 15, "END:VCALENDAR\r\n") == 0);
    CHECK(feed_test::foldedWell(body));
    const std::string ics = feed_test::unfold(body);
    CHECK(ics.find("UID:booking-" + std::to_string(bookingId) + "-" + std::to_string(first) + "@selfvoy\r\n") !=
          std::string::npos);
    CHECK(ics.find("DTSTART:" + selfvoy::ics::utcStamp(first) + "\r\n") != std::string::npos);
    CHECK(ics.find("DTEND:" + selfvoy::ics::utcStamp(first + 3600) + "\r\n") != std::string::npos);
    CHECK(ics.find("SUMMARY:Разбор\\; проекта\\, v2: Пётр\r\n") != std::string::npos);
    CHECK(ics.find("DESCRIPTION:Про стартап\\, деньги\\; планы\\nи команду\r\n") != std::string::npos);
    CHECK(ics.find("STATUS:CONFIRMED\r\n") != std::string::npos);
    CHECK(ics.find("DTSTAMP:") != std::string::npos);
    CHECK(ics.find("UID:invite-" + std::to_string(inviteId) + "-" + std::to_string(first + 86400) + "@selfvoy") !=
          std::string::npos);
    CHECK(ics.find("SUMMARY:Созвон: Анна\r\n") != std::string::npos);
    CHECK(ics.find("invite-" + std::to_string(openId) + "-") == std::string::npos);
    CHECK(ics.find("feed@example.com") == std::string::npos);

    // Отменённая встреча из календаря пропадает.
    Json cancel = Json::object();
    cancel.set("booking_id", Json(bookingId));
    CHECK_EQ(fixture.call(owner, "booking.ownerCancel", cancel).getString("type"), std::string("ok"));
    CHECK(fixture.server->calendarFeed().render(ownerId, std::time(nullptr)).find("UID:booking-") ==
          std::string::npos);

    // Только GET; неизвестный и старый токен — 404; /healthz отвечает.
    CHECK_EQ(feed_test::httpGet(fixture, path, http, "POST"), std::string());
    CHECK_EQ(http.status, 405);
    CHECK_EQ(http.header("allow"), std::string("GET"));
    CHECK_EQ(feed_test::httpGet(fixture, "/ics/" + std::string(43, 'A') + ".ics", http), std::string());
    CHECK_EQ(http.status, 404);
    CHECK_EQ(http.header("cache-control"), std::string("no-store"));
    CHECK_EQ(feed_test::httpGet(fixture, "/ics/../../etc/passwd", http), std::string());
    CHECK_EQ(http.status, 404);
    const std::string newPath = integration::payloadOf(fixture.call(owner, "calendar.feedReset")).getString("path");
    CHECK(newPath != path);
    CHECK_EQ(feed_test::httpGet(fixture, path, http), std::string());
    CHECK_EQ(http.status, 404);
    CHECK_EQ(feed_test::httpGet(fixture, newPath, http), std::string());
    CHECK_EQ(http.status, 200);
    CHECK_EQ(feed_test::httpGet(fixture, "/healthz", http), std::string());
    CHECK_EQ(http.status, 200);
    CHECK_EQ(http.body, std::string("{\"status\":\"ok\"}"));
    feed_test::httpGet(fixture, "/other", http);
    CHECK_EQ(http.status, 400);  // прочие HTTP-запросы — как раньше

    // Заблокированный владелец — 404; выключенная подписка — 404.
    CHECK(fixture.server->database().db().setUserActive(ownerId, false).ok);
    CHECK_EQ(feed_test::httpGet(fixture, newPath, http), std::string());
    CHECK_EQ(http.status, 404);
    CHECK(fixture.server->database().db().setUserActive(ownerId, true).ok);
    const Json disabled = integration::payloadOf(fixture.call(owner, "calendar.feedDisable"));
    CHECK(!disabled.getBool("enabled", true));
    CHECK(!integration::payloadOf(fixture.call(owner, "calendar.feed")).getBool("enabled", true));
    CHECK_EQ(feed_test::httpGet(fixture, newPath, http), std::string());
    CHECK_EQ(http.status, 404);

    // Лимит по IP: 60 запросов в минуту, дальше 429 (до поиска токена).
    auto& calendar = fixture.server->calendarFeed();
    const std::string probe = "/ics/" + std::string(43, 'B') + ".ics";
    std::string last;
    for (int i = 0; i < selfvoy::CalendarFeed::kFetchPerMinute; ++i) last = calendar.serveHttp("GET", probe, "203.0.113.9");
    CHECK(last.rfind("HTTP/1.1 404", 0) == 0);
    last = calendar.serveHttp("GET", probe, "203.0.113.9");
    CHECK(last.rfind("HTTP/1.1 429", 0) == 0);
    CHECK(last.find("Cache-Control: no-store\r\n") != std::string::npos);
    CHECK(calendar.serveHttp("GET", probe, "203.0.113.10").rfind("HTTP/1.1 404", 0) == 0);
    CHECK_EQ(calendar.serveHttp("GET", "/healthz", "203.0.113.9"), std::string());  // не маршрут ICS
}
