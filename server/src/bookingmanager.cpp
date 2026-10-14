// aura/bookingmanager.cpp
#include "aura/bookingmanager.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>

#include "aura/config.h"
#include "aura/crypto.h"
#include "aura/databasemanager.h"
#include "aura/log.h"
#include "aura/memorymanager.h"
#include "aura/net.h"
#include "aura/protocol.h"
#include "aura/referralmanager.h"
#include "aura/session.h"
#include "notificationsmanager.h"

namespace aura {

namespace {

constexpr std::size_t kTokenBytes = 32;   // 256 бит случайности
constexpr std::size_t kTokenLength = 43;  // base64url без '=' от 32 байт
constexpr std::size_t kSlugLength = 10;
constexpr int kReminderLeadSec = 15 * 60;
constexpr int kWeekSec = 7 * 24 * 3600;
constexpr int kAiPerDay = 8;     // кандидатов в день от AI
constexpr int kAiMaxSlots = 120;
constexpr int kCacheSeconds = 60;

std::string trim(const std::string& value) {
    const std::size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const std::size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

// Длина в символах UTF-8 (продолжающие байты 10xxxxxx не считаются).
std::size_t utf8Length(const std::string& value) {
    std::size_t count = 0;
    for (const unsigned char ch : value) {
        if ((ch & 0xC0) != 0x80) ++count;
    }
    return count;
}

bool validText(const std::string& value, std::size_t maxChars, bool multiline) {
    if (utf8Length(value) > maxChars) return false;
    for (const unsigned char ch : value) {
        if (ch == 0x7F) return false;
        if (ch < 0x20 && !(multiline && (ch == '\n' || ch == '\r' || ch == '\t'))) return false;
    }
    return true;
}

bool plausibleEmail(const std::string& email) {
    if (email.size() < 3 || email.size() > 254) return false;
    const std::size_t at = email.find('@');
    if (at == std::string::npos || at == 0 || at + 1 >= email.size()) return false;
    if (email.find('@', at + 1) != std::string::npos) return false;
    for (const unsigned char ch : email) {
        if (ch <= 0x20 || ch == 0x7F || ch == '<' || ch == '>' || ch == '"' || ch == ',' || ch == ';') return false;
    }
    return true;
}

bool validDay(const std::string& day) {
    if (day.size() != 10 || day[4] != '-' || day[7] != '-') return false;
    for (std::size_t i = 0; i < day.size(); ++i) {
        if (i == 4 || i == 7) continue;
        if (std::isdigit(static_cast<unsigned char>(day[i])) == 0) return false;
    }
    std::time_t probe = 0;
    return parseIsoUtc(day + "T00:00:00Z", probe);
}

// Дата YYYY-MM-DD плюс days суток.
std::string addDays(const std::string& day, int days) {
    std::time_t base = 0;
    if (!parseIsoUtc(day + "T00:00:00Z", base)) return day;
    return isoUtc(base + static_cast<std::time_t>(days) * 86400).substr(0, 10);
}

// Числовое поле payload: отсутствует → fallback; не целое или вне [min, max] → false.
bool intField(const Json& payload, const std::string& key, long long min, long long max, long long& out) {
    if (!payload.contains(key)) return true;
    const Json value = payload.get(key);
    if (!value.isNumber()) return false;
    const double number = value.asDouble();
    if (number != static_cast<double>(static_cast<long long>(number))) return false;
    out = static_cast<long long>(number);
    return out >= min && out <= max;
}

bool isFree(const BookingManager::Busy& busy, std::time_t start, std::time_t end) {
    return std::none_of(busy.begin(), busy.end(),
                        [&](const auto& interval) { return start < interval.second && interval.first < end; });
}

Json occurrenceView(const Json& occurrence) {
    Json json = Json::object();
    json.set("start", Json(occurrence.getString("start")));
    json.set("end", Json(occurrence.getString("end")));
    json.set("status", Json(occurrence.getString("status")));
    return json;
}

}  // namespace

BookingManager::BookingManager(DatabaseManager& database, MemoryManager& memory, NotificationsManager* notifications,
                               ReferralManager& referrals, const Config& config)
    : database_(database), memory_(memory), notifications_(notifications), referrals_(referrals), config_(config) {}

std::string BookingManager::tokenHash(const std::string& token) {
    if (token.size() != kTokenLength) return "";
    for (const unsigned char ch : token) {
        if (std::isalnum(ch) == 0 && ch != '-' && ch != '_') return "";
    }
    return crypto::toHex(crypto::sha256(token));
}

bool BookingManager::validSlug(const std::string& slug) {
    if (slug.size() != kSlugLength) return false;
    return std::all_of(slug.begin(), slug.end(), [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
    });
}

bool BookingManager::limited(const std::string& key, int perWindow, int windowSeconds) {
    const auto now = std::chrono::steady_clock::now();
    const auto window = std::chrono::seconds(windowSeconds);
    std::lock_guard<std::mutex> lock(limitsMutex_);
    if (limits_.size() > 10000) {
        for (auto it = limits_.begin(); it != limits_.end();) {
            it = now - it->second.start > std::chrono::hours(1) ? limits_.erase(it) : std::next(it);
        }
    }
    auto& entry = limits_[key];
    if (entry.count == 0 || now - entry.start > window) {
        entry.start = now;
        entry.count = 0;
    }
    if (entry.count >= perWindow) return true;
    ++entry.count;
    return false;
}

void BookingManager::ownerInfo(long long ownerId, std::string& name, std::string& timezone) const {
    name = "Пользователь Aura";
    timezone = "UTC";
    if (const auto owner = database_.db().findUserById(ownerId)) {
        if (!owner->displayName.empty()) name = owner->displayName;
        if (!owner->timezone.empty()) timezone = owner->timezone;
    }
}

// ------------------------------------------------------------------ виды

Json BookingManager::pageJson(const BookingPageRecord& page) {
    const std::time_t now = std::time(nullptr);
    long long upcoming = 0;
    for (const auto& booking : database_.db().listBookings(page.ownerId, page.id, true, 1000)) {
        for (const auto& occurrence : booking.occurrences.items()) {
            std::time_t start = 0;
            if (occurrence.getString("status") == "active" && parseIsoUtc(occurrence.getString("start"), start) &&
                start > now) {
                ++upcoming;
            }
        }
    }
    Json json = Json::object();
    json.set("id", Json(page.id));
    json.set("slug", Json(page.slug));
    json.set("url", Json(config_.publicUrl.empty() ? std::string() : config_.publicUrl + "/#/b/" + page.slug));
    json.set("title", Json(page.title));
    json.set("description", Json(page.description));
    json.set("duration_minutes", Json(static_cast<long long>(page.durationMinutes)));
    json.set("days_ahead", Json(static_cast<long long>(page.daysAhead)));
    json.set("max_per_day", Json(static_cast<long long>(page.maxPerDay)));
    json.set("notice_hours", Json(static_cast<long long>(page.noticeHours)));
    json.set("max_series", Json(static_cast<long long>(page.maxSeries)));
    json.set("weekends", Json(page.weekends));
    json.set("status", Json(page.status));
    json.set("upcoming", Json(upcoming));
    json.set("created_at", Json(page.createdAt));
    json.set("updated_at", Json(page.updatedAt));
    return json;
}

Json BookingManager::bookingJson(const BookingRecord& booking, const std::string& pageTitle) const {
    Json occurrences = Json::array();
    for (const auto& occurrence : booking.occurrences.items()) occurrences.push(occurrenceView(occurrence));
    Json json = Json::object();
    json.set("id", Json(booking.id));
    json.set("page_id", Json(booking.pageId));
    json.set("page_title", Json(pageTitle));
    json.set("guest_name", Json(booking.guestName));
    json.set("guest_email", Json(booking.guestEmail));
    json.set("guest_comment", Json(booking.guestComment));
    json.set("status", Json(booking.status));
    json.set("cancelled_by", Json(booking.cancelledBy));
    json.set("reschedules", Json(static_cast<long long>(booking.reschedules)));
    json.set("occurrences", occurrences);
    json.set("created_at", Json(booking.createdAt));
    json.set("updated_at", Json(booking.updatedAt));
    return json;
}

// Вид для гостя по ссылке управления: без email владельца и данных других гостей.
Json BookingManager::guestBookingJson(const BookingRecord& booking, const BookingPageRecord& page, bool withSlots) {
    const std::time_t now = std::time(nullptr);
    std::string ownerName;
    std::string ownerTimezone;
    ownerInfo(page.ownerId, ownerName, ownerTimezone);
    Json occurrences = Json::array();
    bool upcoming = false;
    for (const auto& occurrence : booking.occurrences.items()) {
        occurrences.push(occurrenceView(occurrence));
        std::time_t start = 0;
        if (occurrence.getString("status") == "active" && parseIsoUtc(occurrence.getString("start"), start) &&
            start > now) {
            upcoming = true;
        }
    }
    const bool canChange = booking.status == "active" && upcoming && page.status != "archived";
    const int left = std::max(0, kMaxReschedules - booking.reschedules);
    Json json = Json::object();
    json.set("page_title", Json(page.title));
    json.set("page_slug", Json(page.status == "active" ? page.slug : std::string()));
    json.set("owner_name", Json(ownerName));
    json.set("owner_timezone", Json(ownerTimezone));
    json.set("duration_minutes", Json(static_cast<long long>(page.durationMinutes)));
    json.set("guest_name", Json(booking.guestName));
    json.set("status", Json(booking.status));
    json.set("cancelled_by", Json(booking.cancelledBy));
    json.set("occurrences", occurrences);
    json.set("can_change", Json(canChange));
    json.set("reschedules_left", Json(static_cast<long long>(left)));
    Json slots = Json::array();
    bool slotsUnavailable = false;
    if (withSlots && canChange && left > 0 && page.status == "active") {
        std::vector<Offer> candidates;
        if (offers(page, candidates)) {
            // Слоты для переноса ближайшей встречи (её booking.reschedule двигает по умолчанию):
            // её собственный интервал и место в лимите дня не мешают.
            const int nearest = findOccurrence(booking, "", now);
            const std::string skipStart =
                nearest >= 0 ? booking.occurrences.items()[static_cast<std::size_t>(nearest)].getString("start")
                             : std::string();
            for (const auto& offer : available(page, candidates, booking.id, skipStart)) {
                Json slot = Json::object();
                slot.set("start", Json(isoUtc(offer.start)));
                slot.set("end", Json(isoUtc(offer.end)));
                slots.push(slot);
            }
        } else {
            slotsUnavailable = true;
        }
    }
    json.set("slots", slots);
    json.set("slots_unavailable", Json(slotsUnavailable));
    json.set("ref", Json(referrals_.codeFor(page.ownerId)));
    return json;
}

// ------------------------------------------------------------ свободное время

bool BookingManager::offers(const BookingPageRecord& page, std::vector<Offer>& out) {
    const std::string version = page.updatedAt + "|" + std::to_string(page.durationMinutes) + "|" +
                                std::to_string(page.daysAhead) + "|" + std::to_string(page.noticeHours) + "|" +
                                (page.weekends ? "1" : "0");
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        const auto it = cache_.find(page.id);
        if (it != cache_.end() && it->second.version == version &&
            std::chrono::steady_clock::now() - it->second.at < std::chrono::seconds(kCacheSeconds)) {
            out = it->second.offers;
            return true;
        }
    }

    Json request = Json::object();
    request.set("context", memory_.context(page.ownerId, page.title));
    request.set("duration_minutes", Json(static_cast<long long>(page.durationMinutes)));
    request.set("window_hours", Json(static_cast<long long>(page.daysAhead) * 24));
    request.set("max_slots", Json(static_cast<long long>(kAiMaxSlots)));
    request.set("per_day", Json(static_cast<long long>(kAiPerDay)));
    request.set("lead_minutes", Json(static_cast<long long>(page.noticeHours) * 60));
    request.set("weekends", Json(page.weekends));

    std::map<std::string, std::string> headers{{"Content-Type", "application/json"}};
    if (!config_.aiServiceToken.empty()) headers["X-Aura-Token"] = config_.aiServiceToken;
    std::string error;
    std::string url = config_.aiServiceUrl;
    while (!url.empty() && url.back() == '/') url.pop_back();
    const net::HttpResponse response =
        net::httpRequest("POST", url + "/v1/agent/slots", headers, request.dump(), config_.aiTimeoutMs, error);
    if (!response.ok()) {
        AURA_LOG(log::Level::Warn, "booking")
            << "подбор слотов не удался: " << (error.empty() ? "HTTP " + std::to_string(response.status) : error);
        return false;
    }
    const Json answer = Json::parse(response.body, &error);
    if (!answer.isObject() || !answer.get("slots").isArray()) {
        AURA_LOG(log::Level::Warn, "booking") << "некорректный ответ /v1/agent/slots";
        return false;
    }

    // AI-сервис не источник истины: длительность, окно и пересечения проверяем сами.
    // Запас до начала и занятость — при каждом показе (они меняются со временем).
    const std::time_t now = std::time(nullptr);
    const std::time_t windowEnd = now + static_cast<std::time_t>(page.daysAhead) * 86400;
    const std::time_t duration = static_cast<std::time_t>(page.durationMinutes) * 60;
    std::vector<Offer> accepted;
    const Json offered = answer.get("slots");
    for (const auto& slot : offered.items()) {
        Offer offer;
        if (!parseIsoUtc(slot.getString("start"), offer.start) || !parseIsoUtc(slot.getString("end"), offer.end)) {
            continue;
        }
        if (offer.end - offer.start != duration || offer.start <= now || offer.end > windowEnd) continue;
        const bool overlaps = std::any_of(accepted.begin(), accepted.end(), [&](const Offer& other) {
            return offer.start < other.end && other.start < offer.end;
        });
        if (overlaps) continue;
        offer.day = slot.getString("day");
        if (!validDay(offer.day)) offer.day = isoUtc(offer.start).substr(0, 10);
        accepted.push_back(offer);
        if (static_cast<int>(accepted.size()) >= kAiMaxSlots) break;
    }
    std::sort(accepted.begin(), accepted.end(), [](const Offer& a, const Offer& b) { return a.start < b.start; });
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (cache_.size() > 1000) cache_.clear();
        cache_[page.id] = Cached{std::chrono::steady_clock::now(), version, accepted};
    }
    out = std::move(accepted);
    return true;
}

std::unordered_map<std::string, int> BookingManager::dayCounts(const BookingPageRecord& page, long long skipBooking,
                                                               const std::string& skipStart) {
    std::unordered_map<std::string, int> counts;
    for (const auto& booking : database_.db().listBookings(page.ownerId, page.id, true, 1000)) {
        for (const auto& occurrence : booking.occurrences.items()) {
            if (occurrence.getString("status") != "active") continue;
            if (booking.id == skipBooking && occurrence.getString("start") == skipStart) continue;
            ++counts[occurrence.getString("day")];
        }
    }
    return counts;
}

std::vector<BookingManager::Offer> BookingManager::available(const BookingPageRecord& page,
                                                             const std::vector<Offer>& candidates,
                                                             long long skipBooking, const std::string& skipStart) {
    const std::time_t now = std::time(nullptr);
    const std::time_t earliest = now + static_cast<std::time_t>(page.noticeHours) * 3600;
    Busy busy = memory_.busy(page.ownerId);
    if (skipBooking > 0 && !skipStart.empty()) {
        // Переносимая встреча не мешает сама себе.
        if (const auto booking = database_.db().findBooking(skipBooking)) {
            for (const auto& occurrence : booking->occurrences.items()) {
                std::time_t start = 0;
                std::time_t end = 0;
                if (occurrence.getString("start") != skipStart || occurrence.getString("status") != "active" ||
                    !parseIsoUtc(occurrence.getString("start"), start) ||
                    !parseIsoUtc(occurrence.getString("end"), end)) {
                    continue;
                }
                const auto it = std::find(busy.begin(), busy.end(), std::make_pair(start, end));
                if (it != busy.end()) busy.erase(it);
            }
        }
    }
    const auto counts = dayCounts(page, skipBooking, skipStart);
    std::vector<Offer> result;
    for (const auto& offer : candidates) {
        if (offer.start <= now || offer.start < earliest) continue;
        if (!isFree(busy, offer.start, offer.end)) continue;
        const auto count = counts.find(offer.day);
        if (count != counts.end() && count->second >= page.maxPerDay) continue;
        result.push_back(offer);
    }
    return result;
}

// ------------------------------------------------------------------ владелец

BookingManager::Result BookingManager::pageCreate(long long ownerId, const Json& payload) {
    BookingPageRecord page;
    page.ownerId = ownerId;
    page.title = trim(payload.getString("title"));
    if (page.title.empty() || !validText(page.title, 120, false)) {
        return Result::failure(protocol::code::kBadRequest, "название — от 1 до 120 символов в одну строку");
    }
    page.description = trim(payload.getString("description"));
    if (!validText(page.description, 1000, true)) {
        return Result::failure(protocol::code::kBadRequest, "описание — не больше 1000 символов");
    }
    long long duration = 30, daysAhead = 14, maxPerDay = 4, notice = 2, maxSeries = 1;
    if (!intField(payload, "duration_minutes", 15, 240, duration)) {
        return Result::failure(protocol::code::kBadRequest, "длительность — от 15 до 240 минут");
    }
    if (!intField(payload, "days_ahead", 1, 60, daysAhead)) {
        return Result::failure(protocol::code::kBadRequest, "запись вперёд — от 1 до 60 дней");
    }
    if (!intField(payload, "max_per_day", 1, 16, maxPerDay)) {
        return Result::failure(protocol::code::kBadRequest, "записей в день — от 1 до 16");
    }
    if (!intField(payload, "notice_hours", 0, 168, notice)) {
        return Result::failure(protocol::code::kBadRequest, "запас до встречи — от 0 до 168 часов");
    }
    if (!intField(payload, "max_series", 1, 12, maxSeries)) {
        return Result::failure(protocol::code::kBadRequest, "серия — от 1 до 12 встреч");
    }
    if (payload.contains("weekends") && !payload.get("weekends").isBool()) {
        return Result::failure(protocol::code::kBadRequest, "weekends — true или false");
    }
    page.durationMinutes = static_cast<int>(duration);
    page.daysAhead = static_cast<int>(daysAhead);
    page.maxPerDay = static_cast<int>(maxPerDay);
    page.noticeHours = static_cast<int>(notice);
    page.maxSeries = static_cast<int>(maxSeries);
    page.weekends = payload.getBool("weekends", false);

    if (database_.db().listBookingPages(ownerId).size() >= static_cast<std::size_t>(kMaxPages)) {
        return Result::failure(protocol::code::kForbidden, "не больше 10 страниц записи — удалите ненужные");
    }

    long long id = 0;
    for (int attempt = 0; attempt < 5 && id == 0; ++attempt) {
        const std::string bytes = crypto::randomBytes(kSlugLength);
        page.slug.clear();
        for (const unsigned char byte : bytes) page.slug.push_back("abcdefghijklmnopqrstuvwxyz0123456789"[byte % 36]);
        const DatabaseError saved = database_.db().createBookingPage(page, id);
        if (!saved.ok) {
            AURA_LOG(log::Level::Warn, "booking") << "страница записи не сохранена: " << saved.message;
            id = 0;
        }
    }
    if (id == 0) return Result::failure(protocol::code::kInternal, "не удалось сохранить страницу записи");
    const auto stored = database_.db().findBookingPage(id);
    Result result;
    result.ok = true;
    result.payload.set("page", pageJson(stored ? *stored : page));
    return result;
}

BookingManager::Result BookingManager::pageList(long long ownerId) {
    Json pages = Json::array();
    for (const auto& page : database_.db().listBookingPages(ownerId)) pages.push(pageJson(page));
    Result result;
    result.ok = true;
    result.payload.set("pages", pages);
    return result;
}

BookingManager::Result BookingManager::pageUpdate(long long ownerId, const Json& payload) {
    const long long id = payload.getInt("page_id");
    auto page = id > 0 ? database_.db().findBookingPage(id) : std::nullopt;
    if (!page || page->ownerId != ownerId || page->status == "archived") {
        return Result::failure(protocol::code::kNotFound, "страница записи не найдена");
    }
    if (payload.contains("title")) {
        page->title = trim(payload.getString("title"));
        if (page->title.empty() || !validText(page->title, 120, false)) {
            return Result::failure(protocol::code::kBadRequest, "название — от 1 до 120 символов в одну строку");
        }
    }
    if (payload.contains("description")) {
        page->description = trim(payload.getString("description"));
        if (!validText(page->description, 1000, true)) {
            return Result::failure(protocol::code::kBadRequest, "описание — не больше 1000 символов");
        }
    }
    long long duration = page->durationMinutes, daysAhead = page->daysAhead, maxPerDay = page->maxPerDay,
              notice = page->noticeHours, maxSeries = page->maxSeries;
    if (!intField(payload, "duration_minutes", 15, 240, duration)) {
        return Result::failure(protocol::code::kBadRequest, "длительность — от 15 до 240 минут");
    }
    if (!intField(payload, "days_ahead", 1, 60, daysAhead)) {
        return Result::failure(protocol::code::kBadRequest, "запись вперёд — от 1 до 60 дней");
    }
    if (!intField(payload, "max_per_day", 1, 16, maxPerDay)) {
        return Result::failure(protocol::code::kBadRequest, "записей в день — от 1 до 16");
    }
    if (!intField(payload, "notice_hours", 0, 168, notice)) {
        return Result::failure(protocol::code::kBadRequest, "запас до встречи — от 0 до 168 часов");
    }
    if (!intField(payload, "max_series", 1, 12, maxSeries)) {
        return Result::failure(protocol::code::kBadRequest, "серия — от 1 до 12 встреч");
    }
    if (payload.contains("weekends")) {
        if (!payload.get("weekends").isBool()) {
            return Result::failure(protocol::code::kBadRequest, "weekends — true или false");
        }
        page->weekends = payload.getBool("weekends");
    }
    if (payload.contains("status")) {
        const std::string status = payload.getString("status");
        if (status != "active" && status != "paused") {
            return Result::failure(protocol::code::kBadRequest, "status — active или paused");
        }
        page->status = status;
    }
    page->durationMinutes = static_cast<int>(duration);
    page->daysAhead = static_cast<int>(daysAhead);
    page->maxPerDay = static_cast<int>(maxPerDay);
    page->noticeHours = static_cast<int>(notice);
    page->maxSeries = static_cast<int>(maxSeries);
    const DatabaseError saved = database_.db().updateBookingPage(*page);
    if (!saved.ok) return Result::failure(protocol::code::kInternal, "не удалось сохранить страницу записи");
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.erase(page->id);
    }
    const auto stored = database_.db().findBookingPage(page->id);
    Result result;
    result.ok = true;
    result.payload.set("page", pageJson(stored ? *stored : *page));
    return result;
}

BookingManager::Result BookingManager::pageDelete(long long ownerId, const Json& payload) {
    const long long id = payload.getInt("page_id");
    auto page = id > 0 ? database_.db().findBookingPage(id) : std::nullopt;
    if (!page || page->ownerId != ownerId || page->status == "archived") {
        return Result::failure(protocol::code::kNotFound, "страница записи не найдена");
    }
    // Страница уходит в архив: ссылка перестаёт работать, уже сделанные записи
    // остаются в силе (их можно отменить через booking.ownerCancel).
    page->status = "archived";
    if (!database_.db().updateBookingPage(*page).ok) {
        return Result::failure(protocol::code::kInternal, "не удалось удалить страницу записи");
    }
    Result result;
    result.ok = true;
    result.payload.set("deleted", Json(true));
    return result;
}

BookingManager::Result BookingManager::list(long long ownerId, const Json& payload) {
    long long limit = payload.getInt("limit", 50);
    limit = std::max(1LL, std::min(200LL, limit));
    const long long pageId = std::max(0LL, payload.getInt("page_id"));
    std::unordered_map<long long, std::string> titles;
    Json bookings = Json::array();
    for (const auto& booking : database_.db().listBookings(ownerId, pageId, payload.getBool("active_only", false),
                                                           static_cast<int>(limit))) {
        auto title = titles.find(booking.pageId);
        if (title == titles.end()) {
            const auto page = database_.db().findBookingPage(booking.pageId);
            title = titles.emplace(booking.pageId, page ? page->title : std::string()).first;
        }
        bookings.push(bookingJson(booking, title->second));
    }
    Result result;
    result.ok = true;
    result.payload.set("bookings", bookings);
    return result;
}

// ------------------------------------------------------------ встречи записи

long long BookingManager::createTask(const BookingRecord& booking, const std::string& pageTitle, std::time_t start) {
    TaskRecord task;
    task.userId = booking.ownerId;
    task.title = "Запись: " + pageTitle + " — " + booking.guestName;
    std::string notes;
    if (!booking.guestEmail.empty()) notes += "Email гостя: " + booking.guestEmail;
    if (!booking.guestComment.empty()) notes += (notes.empty() ? "" : "\n") + booking.guestComment;
    task.notes = notes;
    task.dueAt = isoUtc(start);
    task.remindAt = isoUtc(std::max(std::time(nullptr), start - kReminderLeadSec));
    long long taskId = 0;
    const DatabaseError saved = database_.db().createTask(task, taskId);
    if (!saved.ok) AURA_LOG(log::Level::Warn, "booking") << "задача встречи не создана: " << saved.message;
    return taskId;
}

int BookingManager::findOccurrence(const BookingRecord& booking, const std::string& start, std::time_t now) {
    const auto& items = booking.occurrences.items();
    int nearest = -1;
    std::time_t nearestStart = 0;
    for (std::size_t i = 0; i < items.size(); ++i) {
        std::time_t at = 0;
        if (items[i].getString("status") != "active" || !parseIsoUtc(items[i].getString("start"), at) || at <= now) {
            continue;
        }
        if (!start.empty()) {
            std::time_t wanted = 0;
            if (parseIsoUtc(start, wanted) && wanted == at) return static_cast<int>(i);
            continue;
        }
        if (nearest < 0 || at < nearestStart) {
            nearest = static_cast<int>(i);
            nearestStart = at;
        }
    }
    return nearest;
}

int BookingManager::cancelOccurrences(BookingRecord& booking, int index, std::time_t now) {
    int cancelled = 0;
    Json updated = Json::array();
    const auto& items = booking.occurrences.items();
    for (std::size_t i = 0; i < items.size(); ++i) {
        Json occurrence = items[i];
        std::time_t at = 0;
        const bool future = parseIsoUtc(occurrence.getString("start"), at) && at > now;
        const bool target = index < 0 ? future : static_cast<int>(i) == index;
        if (target && occurrence.getString("status") == "active") {
            occurrence.set("status", Json(std::string("cancelled")));
            const long long taskId = occurrence.getInt("task_id");
            if (taskId > 0) database_.db().deleteTask(taskId);
            occurrence.set("task_id", Json(0LL));
            ++cancelled;
        }
        updated.push(occurrence);
    }
    booking.occurrences = updated;
    const bool anyActive = std::any_of(updated.items().begin(), updated.items().end(), [now](const Json& item) {
        std::time_t at = 0;
        return item.getString("status") == "active" && parseIsoUtc(item.getString("end"), at) && at > now;
    });
    if (!anyActive) booking.status = "cancelled";
    return cancelled;
}

BookingManager::Result BookingManager::ownerCancel(long long ownerId, const Json& payload) {
    const long long id = payload.getInt("booking_id");
    std::lock_guard<std::mutex> lock(changeMutex_);
    auto booking = id > 0 ? database_.db().findBooking(id) : std::nullopt;
    if (!booking || booking->ownerId != ownerId) return Result::failure(protocol::code::kNotFound, "запись не найдена");
    if (booking->status != "active") return Result::failure(protocol::code::kConflict, "запись уже отменена");
    const std::time_t now = std::time(nullptr);
    int index = -1;
    if (payload.contains("occurrence")) {
        index = findOccurrence(*booking, payload.getString("occurrence"), now);
        if (index < 0) return Result::failure(protocol::code::kBadRequest, "нет такой предстоящей встречи");
    }
    if (cancelOccurrences(*booking, index, now) == 0) {
        return Result::failure(protocol::code::kConflict, "предстоящих встреч нет");
    }
    if (booking->status == "cancelled") booking->cancelledBy = "owner";
    if (!database_.db().updateBooking(*booking).ok) {
        return Result::failure(protocol::code::kInternal, "не удалось сохранить запись");
    }
    const auto page = database_.db().findBookingPage(booking->pageId);
    Result result;
    result.ok = true;
    result.payload.set("booking", bookingJson(*booking, page ? page->title : std::string()));
    return result;
}

// ------------------------------------------------------------------ гость

BookingManager::Result BookingManager::get(const Json& payload, const std::string& remoteAddr) {
    if (limited("booking.get:" + Session::ipOf(remoteAddr), kGetPerMinute)) {
        return Result::failure(protocol::code::kForbidden, "слишком много запросов, попробуйте через минуту");
    }
    const std::string slug = payload.getString("slug");
    const auto page = validSlug(slug) ? database_.db().findBookingPageBySlug(slug) : std::nullopt;
    if (!page || page->status == "archived") return Result::failure(protocol::code::kNotFound, "страница не найдена");

    std::string ownerName;
    std::string ownerTimezone;
    ownerInfo(page->ownerId, ownerName, ownerTimezone);
    Json slots = Json::array();
    bool unavailable = false;
    if (page->status == "active") {
        std::vector<Offer> candidates;
        if (offers(*page, candidates)) {
            for (const auto& offer : available(*page, candidates)) {
                Json slot = Json::object();
                slot.set("start", Json(isoUtc(offer.start)));
                slot.set("end", Json(isoUtc(offer.end)));
                slot.set("day", Json(offer.day));
                slots.push(slot);
            }
        } else {
            unavailable = true;
        }
    }
    Json view = Json::object();
    view.set("slug", Json(page->slug));
    view.set("title", Json(page->title));
    view.set("description", Json(page->description));
    view.set("duration_minutes", Json(static_cast<long long>(page->durationMinutes)));
    view.set("max_series", Json(static_cast<long long>(page->maxSeries)));
    view.set("owner_name", Json(ownerName));
    view.set("owner_timezone", Json(ownerTimezone));
    view.set("status", Json(page->status));
    view.set("slots", slots);
    view.set("slots_unavailable", Json(unavailable));
    view.set("ref", Json(referrals_.codeFor(page->ownerId)));
    Result result;
    result.ok = true;
    result.payload.set("page", view);
    return result;
}

BookingManager::Result BookingManager::book(const Json& payload, const std::string& remoteAddr) {
    const std::string ip = Session::ipOf(remoteAddr);
    if (limited("booking.book:" + ip, kBookPerMinute) || limited("booking.book.h:" + ip, kBookPerHour, 3600)) {
        return Result::failure(protocol::code::kForbidden, "слишком много записей, попробуйте позже");
    }
    const std::string slug = payload.getString("slug");
    const auto page = validSlug(slug) ? database_.db().findBookingPageBySlug(slug) : std::nullopt;
    if (!page || page->status == "archived") return Result::failure(protocol::code::kNotFound, "страница не найдена");
    if (page->status != "active") return Result::failure(protocol::code::kConflict, "запись временно закрыта");

    BookingRecord booking;
    booking.pageId = page->id;
    booking.ownerId = page->ownerId;
    booking.guestName = trim(payload.getString("guest_name"));
    if (booking.guestName.empty() || !validText(booking.guestName, 80, false)) {
        return Result::failure(protocol::code::kBadRequest, "укажите имя — до 80 символов");
    }
    booking.guestEmail = trim(payload.getString("guest_email"));
    if (!booking.guestEmail.empty() && !plausibleEmail(booking.guestEmail)) {
        return Result::failure(protocol::code::kBadRequest, "некорректный email");
    }
    booking.guestComment = trim(payload.getString("comment"));
    if (!validText(booking.guestComment, 500, true)) {
        return Result::failure(protocol::code::kBadRequest, "комментарий — не больше 500 символов");
    }
    long long count = 1;
    if (!intField(payload, "count", 1, page->maxSeries, count)) {
        return Result::failure(protocol::code::kBadRequest,
                               "число встреч — от 1 до " + std::to_string(page->maxSeries));
    }
    std::time_t wanted = 0;
    if (!parseIsoUtc(payload.getString("start"), wanted)) {
        return Result::failure(protocol::code::kBadRequest, "start — время одного из свободных слотов");
    }

    std::vector<Offer> candidates;
    if (!offers(*page, candidates)) {
        return Result::failure(protocol::code::kUpstream, "не удалось проверить свободное время — попробуйте позже");
    }
    std::lock_guard<std::mutex> lock(changeMutex_);
    const auto free = available(*page, candidates);
    const auto chosen = std::find_if(free.begin(), free.end(), [wanted](const Offer& o) { return o.start == wanted; });
    if (chosen == free.end()) {
        const bool offered = std::any_of(candidates.begin(), candidates.end(),
                                         [wanted](const Offer& o) { return o.start == wanted; });
        return offered ? Result::failure(protocol::code::kConflict, "это время уже занято — выберите другое")
                       : Result::failure(protocol::code::kBadRequest, "этого времени нет среди свободных");
    }

    // Серия: та же встреча каждую неделю (через 7×24 ч). Каждая должна быть свободна
    // и не превышать лимит записей в свой день.
    const Busy busy = memory_.busy(page->ownerId);
    auto counts = dayCounts(*page, 0, "");
    Json occurrences = Json::array();
    for (long long k = 0; k < count; ++k) {
        const std::time_t start = chosen->start + static_cast<std::time_t>(k) * kWeekSec;
        const std::time_t end = chosen->end + static_cast<std::time_t>(k) * kWeekSec;
        const std::string day = addDays(chosen->day, static_cast<int>(k * 7));
        if (!isFree(busy, start, end) || counts[day] >= page->maxPerDay) {
            return Result::failure(protocol::code::kConflict, "в неделю " + day +
                                                                  " это время занято — выберите меньше встреч или "
                                                                  "другое время");
        }
        ++counts[day];
        Json occurrence = Json::object();
        occurrence.set("start", Json(isoUtc(start)));
        occurrence.set("end", Json(isoUtc(end)));
        occurrence.set("day", Json(day));
        occurrence.set("status", Json(std::string("active")));
        occurrence.set("task_id", Json(0LL));
        occurrences.push(occurrence);
    }

    std::string token;
    long long id = 0;
    booking.occurrences = occurrences;
    for (int attempt = 0; attempt < 3 && id == 0; ++attempt) {
        token = crypto::base64UrlEncode(crypto::randomBytes(kTokenBytes));
        while (!token.empty() && token.back() == '=') token.pop_back();
        booking.manageHash = tokenHash(token);
        if (booking.manageHash.empty()) continue;
        const DatabaseError saved = database_.db().createBooking(booking, id);
        if (!saved.ok) {
            AURA_LOG(log::Level::Warn, "booking") << "запись не сохранена: " << saved.message;
            id = 0;
        }
    }
    if (id == 0) return Result::failure(protocol::code::kInternal, "не удалось сохранить запись");
    booking.id = id;

    // Задачи с напоминанием — на каждую встречу.
    Json withTasks = Json::array();
    for (const auto& occurrence : booking.occurrences.items()) {
        Json item = occurrence;
        std::time_t start = 0;
        parseIsoUtc(item.getString("start"), start);
        item.set("task_id", Json(createTask(booking, page->title, start)));
        withTasks.push(item);
    }
    booking.occurrences = withTasks;
    if (!database_.db().updateBooking(booking).ok) {
        AURA_LOG(log::Level::Warn, "booking") << "id задач записи " << id << " не сохранены";
    }

    if (notifications_) {
        Json notify = Json::object();
        notify.set("booking_id", Json(id));
        notify.set("page_id", Json(page->id));
        notify.set("start", Json(isoUtc(chosen->start)));
        notify.set("end", Json(isoUtc(chosen->end)));
        notify.set("count", Json(count));
        notify.set("guest_name", Json(booking.guestName));
        notifications_->create(page->ownerId, "booking.created",
                               booking.guestName + (count > 1 ? " записался(ась) на серию встреч"
                                                              : " записался(ась) на встречу"),
                               page->title, notify);
    }

    const auto stored = database_.db().findBooking(id);
    Result result;
    result.ok = true;
    result.payload.set("booking", guestBookingJson(stored ? *stored : booking, *page, false));
    result.payload.set("token", Json(token));
    result.payload.set("url", Json(config_.publicUrl.empty() ? std::string() : config_.publicUrl + "/#/m/" + token));
    return result;
}

BookingManager::Result BookingManager::manage(const Json& payload, const std::string& remoteAddr) {
    if (limited("booking.manage:" + Session::ipOf(remoteAddr), kGetPerMinute)) {
        return Result::failure(protocol::code::kForbidden, "слишком много запросов, попробуйте через минуту");
    }
    const std::string hash = tokenHash(payload.getString("token"));
    const auto booking = hash.empty() ? std::nullopt : database_.db().findBookingByToken(hash);
    const auto page = booking ? database_.db().findBookingPage(booking->pageId) : std::nullopt;
    if (!booking || !page) return Result::failure(protocol::code::kNotFound, "запись не найдена");
    Result result;
    result.ok = true;
    result.payload.set("booking", guestBookingJson(*booking, *page, true));
    return result;
}

BookingManager::Result BookingManager::reschedule(const Json& payload, const std::string& remoteAddr) {
    if (limited("booking.change:" + Session::ipOf(remoteAddr), kChangePerMinute)) {
        return Result::failure(protocol::code::kForbidden, "слишком много попыток, попробуйте через минуту");
    }
    const std::string hash = tokenHash(payload.getString("token"));
    auto booking = hash.empty() ? std::nullopt : database_.db().findBookingByToken(hash);
    const auto page = booking ? database_.db().findBookingPage(booking->pageId) : std::nullopt;
    if (!booking || !page) return Result::failure(protocol::code::kNotFound, "запись не найдена");
    if (page->status != "active") return Result::failure(protocol::code::kConflict, "запись временно закрыта");
    std::time_t wanted = 0;
    if (!parseIsoUtc(payload.getString("start"), wanted)) {
        return Result::failure(protocol::code::kBadRequest, "start — время одного из свободных слотов");
    }
    std::vector<Offer> candidates;
    if (!offers(*page, candidates)) {
        return Result::failure(protocol::code::kUpstream, "не удалось проверить свободное время — попробуйте позже");
    }

    std::lock_guard<std::mutex> lock(changeMutex_);
    booking = database_.db().findBookingByToken(hash);  // перечитываем под блокировкой
    if (!booking || booking->status != "active") return Result::failure(protocol::code::kConflict, "запись отменена");
    if (booking->reschedules >= kMaxReschedules) {
        return Result::failure(protocol::code::kConflict, "перенести можно не больше трёх раз — отмените запись");
    }
    const std::time_t now = std::time(nullptr);
    const int index = findOccurrence(*booking, payload.getString("occurrence"), now);
    if (index < 0) return Result::failure(protocol::code::kBadRequest, "нет такой предстоящей встречи");
    Json occurrence = booking->occurrences.items()[static_cast<std::size_t>(index)];
    const std::string from = occurrence.getString("start");

    const auto free = available(*page, candidates, booking->id, from);
    const auto chosen = std::find_if(free.begin(), free.end(), [wanted](const Offer& o) { return o.start == wanted; });
    if (chosen == free.end()) {
        const bool offered = std::any_of(candidates.begin(), candidates.end(),
                                         [wanted](const Offer& o) { return o.start == wanted; });
        return offered ? Result::failure(protocol::code::kConflict, "это время уже занято — выберите другое")
                       : Result::failure(protocol::code::kBadRequest, "этого времени нет среди свободных");
    }
    // Новое время не должно задевать другие встречи той же серии.
    const auto& items = booking->occurrences.items();
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (static_cast<int>(i) == index || items[i].getString("status") != "active") continue;
        std::time_t start = 0;
        std::time_t end = 0;
        if (parseIsoUtc(items[i].getString("start"), start) && parseIsoUtc(items[i].getString("end"), end) &&
            chosen->start < end && start < chosen->end) {
            return Result::failure(protocol::code::kConflict, "это время совпадает с другой встречей серии");
        }
    }

    const long long oldTask = occurrence.getInt("task_id");
    if (oldTask > 0) database_.db().deleteTask(oldTask);
    occurrence.set("start", Json(isoUtc(chosen->start)));
    occurrence.set("end", Json(isoUtc(chosen->end)));
    occurrence.set("day", Json(chosen->day));
    occurrence.set("task_id", Json(createTask(*booking, page->title, chosen->start)));
    Json updated = Json::array();
    for (std::size_t i = 0; i < items.size(); ++i) {
        updated.push(static_cast<int>(i) == index ? occurrence : items[i]);
    }
    booking->occurrences = updated;
    booking->reschedules += 1;
    if (!database_.db().updateBooking(*booking).ok) {
        return Result::failure(protocol::code::kInternal, "не удалось сохранить запись");
    }

    if (notifications_) {
        Json notify = Json::object();
        notify.set("booking_id", Json(booking->id));
        notify.set("page_id", Json(page->id));
        notify.set("from", Json(from));
        notify.set("start", Json(isoUtc(chosen->start)));
        notify.set("end", Json(isoUtc(chosen->end)));
        notify.set("guest_name", Json(booking->guestName));
        notifications_->create(page->ownerId, "booking.rescheduled", booking->guestName + " перенес(ла) встречу",
                               page->title, notify);
    }
    Result result;
    result.ok = true;
    result.payload.set("booking", guestBookingJson(*booking, *page, true));
    return result;
}

BookingManager::Result BookingManager::cancel(const Json& payload, const std::string& remoteAddr) {
    if (limited("booking.change:" + Session::ipOf(remoteAddr), kChangePerMinute)) {
        return Result::failure(protocol::code::kForbidden, "слишком много попыток, попробуйте через минуту");
    }
    const std::string reason = trim(payload.getString("reason"));
    if (!validText(reason, 500, true)) {
        return Result::failure(protocol::code::kBadRequest, "причина — не больше 500 символов");
    }
    const std::string hash = tokenHash(payload.getString("token"));
    std::lock_guard<std::mutex> lock(changeMutex_);
    auto booking = hash.empty() ? std::nullopt : database_.db().findBookingByToken(hash);
    const auto page = booking ? database_.db().findBookingPage(booking->pageId) : std::nullopt;
    if (!booking || !page) return Result::failure(protocol::code::kNotFound, "запись не найдена");
    if (booking->status != "active") return Result::failure(protocol::code::kConflict, "запись уже отменена");
    const std::time_t now = std::time(nullptr);
    int index = -1;
    if (payload.contains("occurrence")) {
        index = findOccurrence(*booking, payload.getString("occurrence"), now);
        if (index < 0) return Result::failure(protocol::code::kBadRequest, "нет такой предстоящей встречи");
    }
    const int cancelled = cancelOccurrences(*booking, index, now);
    if (cancelled == 0) return Result::failure(protocol::code::kConflict, "предстоящих встреч нет");
    if (booking->status == "cancelled") booking->cancelledBy = "guest";
    if (!database_.db().updateBooking(*booking).ok) {
        return Result::failure(protocol::code::kInternal, "не удалось сохранить запись");
    }
    if (notifications_) {
        Json notify = Json::object();
        notify.set("booking_id", Json(booking->id));
        notify.set("page_id", Json(page->id));
        notify.set("cancelled", Json(static_cast<long long>(cancelled)));
        notify.set("guest_name", Json(booking->guestName));
        notify.set("reason", Json(reason));
        notifications_->create(page->ownerId, "booking.cancelled",
                               booking->guestName + (cancelled > 1 ? " отменил(а) встречи" : " отменил(а) встречу"),
                               page->title, notify);
    }
    Result result;
    result.ok = true;
    result.payload.set("booking", guestBookingJson(*booking, *page, true));
    return result;
}

}  // namespace aura
