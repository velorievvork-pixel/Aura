// aura/memorymanager.cpp
#include "aura/memorymanager.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <map>
#include <regex>
#include <set>

#include "aura/log.h"
#include "aura/net.h"

namespace aura {

// «YYYY-MM-DDTHH:MM[:SS][.fff](Z|±HH:MM)» → секунды UTC; false, если это не ISO-метка.
bool parseIsoUtc(const std::string& value, std::time_t& out) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
    int consumed = 0;
    if (std::sscanf(value.c_str(), "%4d-%2d-%2dT%2d:%2d%n", &y, &mo, &d, &h, &mi, &consumed) != 5) return false;
    std::size_t pos = static_cast<std::size_t>(consumed);
    if (pos < value.size() && value[pos] == ':') {
        int n = 0;
        if (std::sscanf(value.c_str() + pos, ":%2d%n", &sec, &n) != 1) return false;
        pos += static_cast<std::size_t>(n);
    }
    if (pos < value.size() && value[pos] == '.') {
        ++pos;
        while (pos < value.size() && std::isdigit(static_cast<unsigned char>(value[pos])) != 0) ++pos;
    }
    long offset = 0;
    if (pos < value.size() && (value[pos] == '+' || value[pos] == '-')) {
        int oh = 0, om = 0;
        if (std::sscanf(value.c_str() + pos + 1, "%2d:%2d", &oh, &om) != 2) return false;
        offset = (value[pos] == '-' ? -1 : 1) * (oh * 3600L + om * 60L);
    }
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || sec > 60) return false;
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = sec;
    out = timegm(&tm) - offset;
    return true;
}

std::string isoUtc(std::time_t t) {
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

// Запись расписания из памяти → занятый слот для AI ({title,start,end}) или null.
// Напоминание хранится как «Напоминание: <текст> (<ISO-время>)» (ToolManager::createReminder):
// время берём из скобок, слот — 30 минут. Записи без точного времени («по средам
// бассейн») остаются только в memory: CalendarEvent в AI требует start и end, и
// одна такая запись иначе ломала бы весь запрос (422).
// Встреча из приглашения хранится интервалом «(<start>/<end>)» — занят весь интервал
// (не больше суток; иначе — те же 30 минут).
Json scheduleEvent(const MemoryRecord& record) {
    const auto open = record.text.rfind('(');
    const auto close = record.text.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close < open) return Json();
    const std::string inside = record.text.substr(open + 1, close - open - 1);
    const auto slash = inside.find('/');
    std::time_t start = 0;
    if (!parseIsoUtc(inside.substr(0, slash), start)) return Json();
    std::time_t end = start + 30 * 60;
    if (slash != std::string::npos) {
        std::time_t until = 0;
        if (parseIsoUtc(inside.substr(slash + 1), until) && until > start && until - start <= 24 * 3600) {
            end = until;
        }
    }
    std::string title = record.text.substr(0, open);
    while (!title.empty() && std::isspace(static_cast<unsigned char>(title.back())) != 0) title.pop_back();
    Json event = Json::object();
    event.set("title", Json(title.empty() ? record.text : title));
    event.set("start", Json(isoUtc(start)));
    event.set("end", Json(isoUtc(end)));
    return event;
}

// Простая релевантность: совпадение подстрок запроса в тексте записи + вес.
double relevance(const std::string& text, const std::vector<std::string>& tokens) {
    if (tokens.empty()) return 0.0;
    const std::string haystack = lower(text);
    double score = 0.0;
    for (const auto& token : tokens) {
        if (token.size() < 4) continue;
        if (haystack.find(lower(token)) != std::string::npos) score += 1.0;
        else if (haystack.find(lower(token).substr(0, 4)) != std::string::npos) score += 0.6;
    }
    return score;
}

std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> result;
    std::string current;
    for (const char ch : text) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        // UTF-8: буквы кириллицы/латиницы и цифры
        if (std::isalnum(byte) != 0 || byte >= 0x80) {
            current.push_back(ch);
        } else if (!current.empty()) {
            result.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) result.push_back(current);
    return result;
}

}  // namespace

MemoryManager::MemoryManager(DatabaseManager& database, const Config& config)
    : database_(database), config_(config) {}

Json MemoryManager::load(long long userId, const std::string& query, int limit) {
    std::vector<MemoryRecord> records = database_.db().listMemory(userId, limit > 0 ? limit * 2 : 40);
    const std::vector<std::string> tokens = words(query);
    if (!tokens.empty()) {
        std::stable_sort(records.begin(), records.end(),
                         [&tokens](const MemoryRecord& a, const MemoryRecord& b) {
                             return relevance(a.text, tokens) + a.weight >
                                    relevance(b.text, tokens) + b.weight;
                         });
    }
    if (limit > 0 && records.size() > static_cast<std::size_t>(limit)) {
        records.resize(static_cast<std::size_t>(limit));
    }

    Json entries = Json::array();
    for (const auto& record : records) entries.push(record.toJson());
    Json result = Json::object();
    result.set("user_id", Json(userId));
    result.set("entries", entries);
    return result;
}

Json MemoryManager::context(long long userId, const std::string& focusMessage) {
    Json context = Json::object();
    context.set("user_id", Json(userId));
    if (const auto user = database_.db().findUserById(userId)) {
        context.set("email", Json(user->email));
        context.set("display_name", Json(user->displayName));
        context.set("timezone", Json(user->timezone));
    }
    context.set("now", Json(isoNow()));
    context.set("message", Json(focusMessage));
    context.set("preferences", preferences(userId));
    context.set("memory", load(userId, focusMessage, 20).get("entries"));

    Json calendar = Json::array();
    for (const auto& record : schedule(userId)) {
        Json event = scheduleEvent(record);
        if (event.isNull()) continue;
        event.set("source", Json("user_memory"));
        event.set("tags", [&record] {
            Json tags = Json::array();
            for (const auto& tag : record.tags) tags.push(Json(tag));
            return tags;
        }());
        calendar.push(event);
    }
    // Записи гостей со страниц записи (booking.*) тоже занимают время.
    for (const auto& [event, interval] : bookingEvents(userId)) {
        (void)interval;
        calendar.push(event);
    }
    context.set("calendar", calendar);
    return context;
}

std::size_t MemoryManager::remember(long long userId, const Json& updates) {
    if (!updates.isArray()) return 0;
    std::size_t saved = 0;
    for (const auto& entry : updates.items()) {
        const std::string text = entry.getString("text");
        if (text.empty()) continue;
        std::vector<std::string> tags;
        const Json tagArray = entry.get("tags");
        for (const auto& tag : tagArray.items()) {
            if (!tag.asString().empty()) tags.push_back(tag.asString());
        }
        const DatabaseError result = database_.db().upsertMemory(
            userId, entry.getString("kind", "fact"), text, entry.getDouble("weight", 1.0), tags);
        if (result.ok) {
            ++saved;
        } else {
            AURA_LOG(log::Level::Warn, "memory") << "не удалось сохранить факт: " << result.message;
        }
    }
    AURA_LOG(log::Level::Debug, "memory") << "user=" << userId << " сохранено фактов: " << saved;
    return saved;
}

Json MemoryManager::extractLocally(const std::string& message) {
    static const std::vector<std::pair<const char*, const char*>> patterns = {
        {R"(я\s+(люблю|не люблю|предпочитаю|ем|не ем|живу|работаю)\s+([^.;!?]{2,60}))", "preference"},
        {R"(у меня\s+(аллергия на|есть|нет)\s+([^.;!?]{2,60}))", "fact"},
        {R"(мо(?:й|я|ё)\s+(друг|подруга|коллега|начальник)\s+([А-ЯЁA-Z][а-яёa-zA-Z]{1,30}))", "contact"},
    };

    Json entries = Json::array();
    for (const auto& pattern : patterns) {
        try {
            const std::regex expression(pattern.first, std::regex::icase);
            auto begin = std::sregex_iterator(message.begin(), message.end(), expression);
            const auto end = std::sregex_iterator();
            for (auto it = begin; it != end; ++it) {
                const std::smatch& match = *it;
                std::string text;
                for (std::size_t group = 1; group < match.size(); ++group) {
                    if (match[group].matched) {
                        if (!text.empty()) text += " ";
                        text += match[group].str();
                    }
                }
                if (text.empty()) continue;
                Json entry = Json::object();
                entry.set("kind", Json(pattern.second));
                entry.set("text", Json(text));
                Json tags = Json::array();
                tags.push(Json("extracted"));
                entry.set("tags", tags);
                entries.push(entry);
            }
        } catch (const std::regex_error& error) {
            AURA_LOG(log::Level::Error, "memory") << "regex: " << error.what();
        }
    }
    return entries;
}

Json MemoryManager::extract(long long userId, const std::string& message, bool commit, std::string& error) {
    Json request = Json::object();
    request.set("user_key", Json(userId != 0 ? std::to_string(userId) : std::string("anonymous")));
    request.set("message", Json(message));
    request.set("commit", Json(commit));
    Json context = Json::object();
    context.set("user_id", Json(userId));
    context.set("message", Json(message));
    request.set("context", context);

    std::map<std::string, std::string> headers;
    if (!config_.aiServiceToken.empty()) headers["X-Aura-Token"] = config_.aiServiceToken;

    std::string url = config_.aiServiceUrl;
    if (!url.empty() && url.back() == '/') url.pop_back();
    const net::HttpResponse response =
        net::httpRequest("POST", url + "/v1/memory/extract", headers, request.dump(), config_.aiTimeoutMs, error);

    Json result = Json::object();
    if (response.ok()) {
        const Json parsed = Json::parse(response.body, &error);
        if (!parsed.isNull()) {
            const Json entries = parsed.get("entries");
            if (commit) remember(userId, entries);
            result.set("entries", entries);
            result.set("source", Json("ai"));
            return result;
        }
    }

    AURA_LOG(log::Level::Warn, "memory")
        << "AI-сервис недоступен (" << error << "), извлекаю факты локально";
    const Json local = extractLocally(message);
    if (commit) remember(userId, local);
    result.set("entries", local);
    result.set("source", Json("local"));
    error.clear();
    return result;
}

Json MemoryManager::defaultPrivacy() {
    Json privacy = Json::object();
    privacy.set("share_free_slots_only", Json(true));
    privacy.set("hide_event_titles", Json(true));
    privacy.set("allow_place_choice", Json(true));
    return privacy;
}

Json MemoryManager::preferences(long long userId) {
    Json preferences = database_.db().getPreferences(userId);
    if (!preferences.isObject()) preferences = Json::object();
    preferences.set("user_id", Json(userId));
    // Старые строки настроек без privacy (или с частью ключей) — дополняем
    // значениями по умолчанию, чтобы клиенты всегда видели все три флага.
    Json privacy = defaultPrivacy();
    const Json stored = preferences.get("privacy");
    if (stored.isObject()) {
        for (const auto& member : stored.members()) {
            if (privacy.contains(member.first) && member.second.isBool()) privacy.set(member.first, member.second);
        }
    }
    preferences.set("privacy", privacy);
    return preferences;
}

Json MemoryManager::privacy(long long userId) { return preferences(userId).get("privacy"); }

bool MemoryManager::setPreferences(long long userId, const Json& preferences, std::string& error, bool* invalid) {
    Json toSave = preferences;
    if (invalid) *invalid = false;
    // privacy: только известные булевы флаги; частичное обновление сливаем с текущими.
    if (toSave.contains("privacy")) {
        const Json incoming = toSave.get("privacy");
        if (!incoming.isObject()) {
            error = "privacy — объект с флагами";
            if (invalid) *invalid = true;
            return false;
        }
        Json merged = privacy(userId);
        for (const auto& member : incoming.members()) {
            if (!merged.contains(member.first) || !member.second.isBool()) {
                error = "privacy." + member.first + ": неизвестный флаг или не true/false";
                if (invalid) *invalid = true;
                return false;
            }
            merged.set(member.first, member.second);
        }
        toSave.set("privacy", merged);
    }
    // Онбординг-опрос после регистрации: как только пользователь прислал
    // хотя бы одно поле анкеты (день рождения, аллергии, диета, город, …),
    // помечаем опрос пройденным — клиенты по этому флагу показывают анкету.
    static const char* kOnboardingKeys[] = {"birthday", "allergies", "diet",   "transport",
                                            "city",     "budget_limit", "preferred_hours",
                                            "work_hours"};
    if (!toSave.contains("onboarded")) {
        for (const char* key : kOnboardingKeys) {
            if (toSave.contains(key)) {
                toSave.set("onboarded", Json(true));
                break;
            }
        }
    }
    const DatabaseError result = database_.db().setPreferences(userId, toSave);
    if (!result.ok) {
        error = result.message;
        return false;
    }
    return true;
}

std::vector<std::pair<std::time_t, std::time_t>> MemoryManager::busy(long long userId) {
    std::vector<std::pair<std::time_t, std::time_t>> intervals;
    for (const auto& record : schedule(userId)) {
        const Json event = scheduleEvent(record);
        if (event.isNull()) continue;
        std::time_t start = 0;
        std::time_t end = 0;
        if (parseIsoUtc(event.getString("start"), start) && parseIsoUtc(event.getString("end"), end)) {
            intervals.emplace_back(start, end);
        }
    }
    for (const auto& [event, interval] : bookingEvents(userId)) {
        (void)event;
        intervals.push_back(interval);
    }
    return intervals;
}

std::vector<std::pair<Json, std::pair<std::time_t, std::time_t>>> MemoryManager::bookingEvents(long long userId) {
    std::vector<std::pair<Json, std::pair<std::time_t, std::time_t>>> events;
    const std::time_t now = std::time(nullptr);
    for (const auto& booking : database_.db().listUpcomingBookings(userId, 0, 5000)) {
        for (const auto& occurrence : booking.occurrences.items()) {
            if (occurrence.getString("status") != "active") continue;
            std::time_t start = 0;
            std::time_t end = 0;
            if (!parseIsoUtc(occurrence.getString("start"), start) || !parseIsoUtc(occurrence.getString("end"), end) ||
                end <= start || end <= now) {
                continue;
            }
            Json event = Json::object();
            event.set("title", Json("Запись: " + booking.guestName));
            event.set("start", Json(isoUtc(start)));
            event.set("end", Json(isoUtc(end)));
            event.set("source", Json("booking"));
            Json tags = Json::array();
            tags.push(Json("meeting"));
            tags.push(Json("booking"));
            event.set("tags", tags);
            events.emplace_back(event, std::make_pair(start, end));
        }
    }
    return events;
}

std::vector<MemoryRecord> MemoryManager::schedule(long long userId) {
    std::vector<MemoryRecord> result;
    // Расписание — из всей памяти, а не из 200 самых «весомых» записей: встреча с
    // малым весом иначе выпадала бы из занятости (двойная запись на то же время).
    for (const auto& record : database_.db().listMemory(userId, 5000)) {
        const bool isSchedule = record.kind == "schedule" ||
                                std::find(record.tags.begin(), record.tags.end(), "reminder") != record.tags.end();
        if (isSchedule) result.push_back(record);
    }
    return result;
}

}  // namespace aura
