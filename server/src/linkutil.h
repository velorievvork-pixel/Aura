// linkutil.h — общие помощники публичных ссылок (invite.*, booking.*):
// проверка текстовых полей гостя, токены ссылок, слоты {start, end}.
#pragma once

#include <cctype>
#include <ctime>
#include <string>

#include "aura/crypto.h"
#include "aura/json.h"
#include "aura/memorymanager.h"

namespace aura::linkutil {

constexpr std::size_t kTokenBytes = 32;   // 256 бит случайности
constexpr std::size_t kTokenLength = 43;  // base64url без '=' от 32 байт

inline std::string trim(const std::string& value) {
    const std::size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const std::size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

// Длина в символах UTF-8 (продолжающие байты 10xxxxxx не считаются).
inline std::size_t utf8Length(const std::string& value) {
    std::size_t count = 0;
    for (const unsigned char ch : value) {
        if ((ch & 0xC0) != 0x80) ++count;
    }
    return count;
}

// Длина в символах и отсутствие управляющих символов (переводы строк — только
// в многострочных полях).
inline bool validText(const std::string& value, std::size_t maxChars, bool multiline) {
    if (utf8Length(value) > maxChars) return false;
    for (const unsigned char ch : value) {
        if (ch == 0x7F) return false;
        if (ch < 0x20 && !(multiline && (ch == '\n' || ch == '\r' || ch == '\t'))) return false;
    }
    return true;
}

inline bool plausibleEmail(const std::string& email) {
    if (email.size() < 3 || email.size() > 254) return false;
    const std::size_t at = email.find('@');
    if (at == std::string::npos || at == 0 || at + 1 >= email.size()) return false;
    if (email.find('@', at + 1) != std::string::npos) return false;
    for (const unsigned char ch : email) {
        if (ch <= 0x20 || ch == 0x7F || ch == '<' || ch == '>' || ch == '"' || ch == ',' || ch == ';') return false;
    }
    return true;
}

inline std::string joinUrl(const std::string& base, const std::string& path) {
    std::string url = base;
    if (!url.empty() && url.back() == '/') url.pop_back();
    return url + path;
}

// Новый секретный токен ссылки (43 символа base64url).
inline std::string newToken() {
    std::string token = crypto::base64UrlEncode(crypto::randomBytes(kTokenBytes));
    while (!token.empty() && token.back() == '=') token.pop_back();
    return token;
}

// Токен → ключ поиска в БД (hex sha256); "" — токен заведомо неверный.
inline std::string tokenHash(const std::string& token) {
    if (token.size() != kTokenLength) return "";
    for (const unsigned char ch : token) {
        if (std::isalnum(ch) == 0 && ch != '-' && ch != '_') return "";
    }
    return crypto::toHex(crypto::sha256(token));
}

// Интервал слота {start, end}; false — запись повреждена.
inline bool slotTimes(const Json& slot, std::time_t& start, std::time_t& end) {
    return parseIsoUtc(slot.getString("start"), start) && parseIsoUtc(slot.getString("end"), end) && end > start;
}

inline Json slotJson(std::time_t start, std::time_t end) {
    Json slot = Json::object();
    slot.set("start", Json(isoUtc(start)));
    slot.set("end", Json(isoUtc(end)));
    return slot;
}

}  // namespace aura::linkutil
