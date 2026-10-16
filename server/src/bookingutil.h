// bookingutil.h — общие проверки ввода и мелкие помощники страниц записи
// (bookingmanager.cpp, packages.cpp). Внутренний заголовок сервера.
#pragma once

#include <algorithm>
#include <cctype>
#include <string>

#include "selfvoy/json.h"
#include "selfvoy/slotspost.h"

namespace selfvoy::bookingutil {

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

inline std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

// Числовое поле payload: отсутствует → fallback; не целое или вне [min, max] → false.
inline bool intField(const Json& payload, const std::string& key, long long min, long long max, long long& out) {
    if (!payload.contains(key)) return true;
    const Json value = payload.get(key);
    if (!value.isNumber()) return false;
    const double number = value.asDouble();
    if (!(number >= -9.0e15 && number <= 9.0e15)) return false;
    if (number != static_cast<double>(static_cast<long long>(number))) return false;
    out = static_cast<long long>(number);
    return out >= min && out <= max;
}

// Первые maxChars символов UTF-8 (без разрыва многобайтового символа).
inline std::string utf8Prefix(const std::string& value, std::size_t maxChars) {
    std::size_t chars = 0;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if ((static_cast<unsigned char>(value[i]) & 0xC0) != 0x80) {
            if (chars == maxChars) return value.substr(0, i);
            ++chars;
        }
    }
    return value;
}

inline std::string moneyOf(const Json& payment) {
    return slotspost::moneyLabel(payment.getInt("amount"), payment.getString("currency", "RUB"));
}

inline Json withStatus(const Json& payment, const std::string& status) {
    Json updated = payment;
    updated.set("status", Json(status));
    return updated;
}

}  // namespace selfvoy::bookingutil
