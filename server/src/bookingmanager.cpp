// aura/bookingmanager.cpp
#include "aura/bookingmanager.h"

#include <algorithm>
#include <set>

#include "aura/config.h"
#include "aura/crypto.h"
#include "aura/databasemanager.h"
#include "aura/log.h"
#include "aura/memorymanager.h"
#include "aura/net.h"
#include "aura/protocol.h"
#include "aura/referralmanager.h"
#include "aura/session.h"
#include "linkutil.h"
#include "notificationsmanager.h"

namespace aura {

namespace {

using namespace linkutil;

constexpr std::time_t kDay = 24 * 3600;
constexpr std::time_t kWeek = 7 * kDay;
// AI отдаёт первые per_day вариантов дня по времени: с шагом 60 минут (уроки от часа)
// 24 — это весь день.
constexpr int kMaxSlotsPerDay = 24;
constexpr int kMaxSlots = 200;
// Без l, o, 0, 1 — slug читают и переписывают руками.
constexpr char kSlugAlphabet[] = "abcdefghijkmnpqrstuvwxyz23456789";
constexpr std::size_t kSlugLength = 10;

long long intOf(const Json& item) {
    return item.isNumber() ? static_cast<long long>(item.asDouble()) : -1;
}

bool contains(const Json& array, long long value) {
    for (const auto& item : array.items()) {
        if (intOf(item) == value) return true;
    }
    return false;
}

// Список целых из payload: все в [low, high], без повторов, по возрастанию.
bool intList(const Json& value, long long low, long long high, Json& out) {
    if (!value.isArray()) return false;
    std::set<long long> seen;
    for (const auto& item : value.items()) {
        if (!item.isNumber()) return false;
        const double raw = item.asDouble();
        const long long number = static_cast<long long>(raw);
        if (static_cast<double>(number) != raw || number < low || number > high) return false;
        seen.insert(number);
    }
    out = Json::array();
    for (const long long number : seen) out.push(Json(number));
    return true;
}

bool overlaps(const std::vector<std::pair<std::time_t, std::time_t>>& busy, std::time_t start, std::time_t end) {
    return std::any_of(busy.begin(), busy.end(),
                       [&](const auto& interval) { return start < interval.second && interval.first < end; });
}

}  // namespace

std::vector<BookingOccurrence> bookingOccurrences(const BookingRecord& record) {
    std::vector<BookingOccurrence> occurrences;
    std::time_t first = 0;
    if (!parseIsoUtc(record.firstStart, first)) return occurrences;
    const int count = record.weekly ? std::max(1, record.weeks) : 1;
    const std::time_t duration = static_cast<std::time_t>(record.durationMinutes) * 60;
    for (int index = 0; index < count; ++index) {
        BookingOccurrence occurrence;
        occurrence.index = index;
        occurrence.start = first + index * kWeek;
        std::time_t moved = 0;
        if (parseIsoUtc(record.moved.getString(std::to_string(index)), moved)) occurrence.start = moved;
        occurrence.end = occurrence.start + duration;
        bool skipped = false;
        for (const auto& item : record.skipped.items()) {
            if (intOf(item) == index) skipped = true;
        }
        occurrence.cancelled = skipped || record.status != "active";
        occurrences.push_back(occurrence);
    }
    return occurrences;
}

BookingManager::BookingManager(DatabaseManager& database, MemoryManager& memory, NotificationsManager* notifications,
                               ReferralManager& referrals, const Config& config)
    : database_(database), memory_(memory), notifications_(notifications), referrals_(referrals), config_(config) {}

// ------------------------------------------------------------------ JSON

Json BookingManager::pageJson(const BookingPageRecord& page) const {
    Json json = Json::object();
    json.set("id", Json(page.id));
    json.set("slug", Json(page.slug));
    json.set("url", Json(config_.publicUrl.empty() ? std::string() : joinUrl(config_.publicUrl, "/#/b/" + page.slug)));
    json.set("title", Json(page.title));
    json.set("description", Json(page.description));
    json.set("duration_minutes", Json(static_cast<long long>(page.durationMinutes)));
    json.set("window_days", Json(static_cast<long long>(page.windowDays)));
    json.set("min_notice_hours", Json(static_cast<long long>(page.minNoticeHours)));
    json.set("buffer_minutes", Json(static_cast<long long>(page.bufferMinutes)));
    json.set("hours", page.hours);
    json.set("weekdays", page.weekdays);
    json.set("allow_weekly", Json(page.allowWeekly));
    json.set("active", Json(page.active));
    json.set("created_at", Json(page.createdAt));
    json.set("updated_at", Json(page.updatedAt));
    return json;
}

Json BookingManager::publicPageJson(const BookingPageRecord& page) {
    Json json = Json::object();
    json.set("title", Json(page.title));
    json.set("description", Json(page.description));
    json.set("duration_minutes", Json(static_cast<long long>(page.durationMinutes)));
    std::string ownerName = "Пользователь Aura";
    std::string ownerTimezone = "UTC";
    if (const auto owner = database_.db().findUserById(page.ownerId)) {
        if (!owner->displayName.empty()) ownerName = owner->displayName;
        if (!owner->timezone.empty()) ownerTimezone = owner->timezone;
    }
    json.set("owner_name", Json(ownerName));
    json.set("owner_timezone", Json(ownerTimezone));
    json.set("allow_weekly", Json(page.allowWeekly));
    json.set("max_weeks", Json(static_cast<long long>(referrals_.maxWeeks(page.ownerId))));
    json.set("referral_code", Json(referrals_.codeFor(page.ownerId)));
    return json;
}

Json BookingManager::ownerBookingJson(const BookingRecord& booking, std::time_t from, std::time_t to) const {
    Json json = Json::object();
    json.set("id", Json(booking.id));
    json.set("page_id", Json(booking.pageId));
    json.set("title", Json(booking.title));
    json.set("guest_name", Json(booking.guestName));
    json.set("guest_email", Json(booking.guestEmail));
    json.set("guest_comment", Json(booking.guestComment));
    json.set("duration_minutes", Json(static_cast<long long>(booking.durationMinutes)));
    json.set("weekly", Json(booking.weekly));
    json.set("weeks", Json(static_cast<long long>(booking.weeks)));
    json.set("status", Json(booking.status));
    json.set("created_at", Json(booking.createdAt));
    Json occurrences = Json::array();
    for (const auto& occurrence : bookingOccurrences(booking)) {
        if (occurrence.end < from || occurrence.start > to) continue;
        Json item = slotJson(occurrence.start, occurrence.end);
        item.set("index", Json(static_cast<long long>(occurrence.index)));
        item.set("status", Json(std::string(occurrence.cancelled ? "cancelled" : "booked")));
        occurrences.push(item);
    }
    json.set("occurrences", occurrences);
    return json;
}

Json BookingManager::guestBookingJson(const BookingRecord& booking) {
    Json json = Json::object();
    json.set("title", Json(booking.title));
    std::string ownerName = "Пользователь Aura";
    std::string ownerTimezone = "UTC";
    if (const auto owner = database_.db().findUserById(booking.ownerId)) {
        if (!owner->displayName.empty()) ownerName = owner->displayName;
        if (!owner->timezone.empty()) ownerTimezone = owner->timezone;
    }
    json.set("owner_name", Json(ownerName));
    json.set("owner_timezone", Json(ownerTimezone));
    json.set("duration_minutes", Json(static_cast<long long>(booking.durationMinutes)));
    json.set("weekly", Json(booking.weekly));
    json.set("weeks", Json(static_cast<long long>(booking.weeks)));
    json.set("status", Json(booking.status));
    json.set("guest_name", Json(booking.guestName));
    const std::time_t now = std::time(nullptr);
    Json occurrences = Json::array();
    for (const auto& occurrence : bookingOccurrences(booking)) {
        if (occurrence.end < now - kDay) continue;
        Json item = slotJson(occurrence.start, occurrence.end);
        item.set("index", Json(static_cast<long long>(occurrence.index)));
        item.set("status", Json(std::string(occurrence.cancelled ? "cancelled" : "booked")));
        occurrences.push(item);
    }
    json.set("occurrences", occurrences);
    json.set("referral_code", Json(referrals_.codeFor(booking.ownerId)));
    return json;
}

// ----------------------------------------------------------- служебное

bool BookingManager::limited(const std::string& key, int limit, int windowSec) {
    const std::time_t now = std::time(nullptr);
    std::lock_guard<std::mutex> lock(limitsMutex_);
    if (limits_.size() > 10000) {
        for (auto it = limits_.begin(); it != limits_.end();) {
            it = now - it->second.start > 3600 ? limits_.erase(it) : std::next(it);
        }
    }
    auto& window = limits_[key];
    if (window.count == 0 || now - window.start >= windowSec) {
        window.start = now;
        window.count = 0;
    }
    if (window.count >= limit) return true;
    ++window.count;
    return false;
}

std::string BookingManager::newSlug() {
    const std::string bytes = crypto::randomBytes(kSlugLength);
    std::string slug;
    for (const unsigned char byte : bytes) slug.push_back(kSlugAlphabet[byte & 31]);
    return slug;
}

BookingManager::Busy BookingManager::busyWithBuffer(long long ownerId, int bufferMinutes) {
    Busy busy = memory_.busy(ownerId);
    const std::time_t buffer = static_cast<std::time_t>(bufferMinutes) * 60;
    for (auto& [start, end] : busy) {
        start -= buffer;
        end += buffer;
    }
    return busy;
}

bool BookingManager::pageSlots(const BookingPageRecord& page, Json& slots, std::string& error) {
    Json request = Json::object();
    request.set("context", memory_.context(page.ownerId, page.title));
    request.set("duration_minutes", Json(static_cast<long long>(page.durationMinutes)));
    request.set("window_hours", Json(static_cast<long long>(page.windowDays) * 24));
    request.set("max_slots", Json(static_cast<long long>(kMaxSlots)));
    request.set("per_day", Json(static_cast<long long>(kMaxSlotsPerDay)));
    request.set("min_notice_minutes", Json(static_cast<long long>(page.minNoticeHours) * 60));
    request.set("buffer_minutes", Json(static_cast<long long>(page.bufferMinutes)));
    request.set("step_minutes", Json(page.durationMinutes >= 60 ? 60LL : 30LL));
    if (!page.hours.items().empty()) request.set("work_hours", page.hours);
    request.set("weekdays", page.weekdays);

    std::map<std::string, std::string> headers{{"Content-Type", "application/json"}};
    if (!config_.aiServiceToken.empty()) headers["X-Aura-Token"] = config_.aiServiceToken;
    const net::HttpResponse response = net::httpRequest(
        "POST", joinUrl(config_.aiServiceUrl, "/v1/agent/slots"), headers, request.dump(), config_.aiTimeoutMs, error);
    if (!response.ok()) {
        if (error.empty()) error = "HTTP " + std::to_string(response.status);
        return false;
    }
    const Json answer = Json::parse(response.body, &error);
    if (!answer.isObject() || !answer.get("slots").isArray()) {
        error = "некорректный ответ /v1/agent/slots";
        return false;
    }

    // AI не источник истины: длительность, срок записи, окно и занятость
    // (с буфером) сервер проверяет сам.
    const std::time_t now = std::time(nullptr);
    const std::time_t earliest = now + static_cast<std::time_t>(page.minNoticeHours) * 3600;
    const std::time_t latest = now + static_cast<std::time_t>(page.windowDays) * kDay;
    const std::time_t duration = static_cast<std::time_t>(page.durationMinutes) * 60;
    const Busy busy = busyWithBuffer(page.ownerId, page.bufferMinutes);
    std::set<std::time_t> starts;
    const Json offered = answer.get("slots");
    for (const auto& slot : offered.items()) {
        std::time_t start = 0;
        std::time_t end = 0;
        if (!slotTimes(slot, start, end) || end - start != duration) continue;
        if (start < earliest || end > latest || overlaps(busy, start, end)) continue;
        starts.insert(start);
        if (static_cast<int>(starts.size()) >= kMaxSlots) break;
    }
    slots = Json::array();
    for (const std::time_t start : starts) slots.push(slotJson(start, start + duration));
    return true;
}

// -------------------------------------------------------------- владелец

BookingManager::Result BookingManager::listPages(long long ownerId) {
    Json pages = Json::array();
    for (const auto& page : database_.db().listBookingPages(ownerId)) pages.push(pageJson(page));
    Result result;
    result.ok = true;
    result.payload.set("pages", pages);
    result.payload.set("plan", referrals_.plan(ownerId));
    return result;
}

BookingManager::Result BookingManager::savePage(long long ownerId, const Json& payload) {
    const long long id = payload.getInt("id");
    BookingPageRecord page;
    if (id > 0) {
        const auto existing = database_.db().findBookingPage(id);
        if (!existing || existing->ownerId != ownerId) {
            return Result::failure(protocol::code::kNotFound, "страница записи не найдена");
        }
        page = *existing;
    } else {
        if (static_cast<int>(database_.db().listBookingPages(ownerId).size()) >= referrals_.maxPages(ownerId)) {
            return Result::failure(protocol::code::kForbidden,
                                   referrals_.isPro(ownerId) ? "на тарифе Pro — не больше 5 страниц записи"
                                                             : "на тарифе Free — 1 страница записи");
        }
        page.ownerId = ownerId;
        page.weekdays = Json::array();
        for (long long day = 0; day <= 5; ++day) page.weekdays.push(Json(day));
    }

    page.title = trim(payload.getString("title", page.title));
    if (page.title.empty() || !validText(page.title, 120, false)) {
        return Result::failure(protocol::code::kBadRequest, "название — от 1 до 120 символов в одну строку");
    }
    page.description = trim(payload.getString("description", page.description));
    if (!validText(page.description, 1000, true)) {
        return Result::failure(protocol::code::kBadRequest, "описание — не больше 1000 символов");
    }
    struct Bound {
        const char* key;
        int* field;
        long long low;
        long long high;
        const char* message;
    };
    const Bound bounds[] = {
        {"duration_minutes", &page.durationMinutes, 15, 240, "длительность — от 15 до 240 минут"},
        {"window_days", &page.windowDays, 1, 60, "запись вперёд — от 1 до 60 дней"},
        {"min_notice_hours", &page.minNoticeHours, 0, 168, "минимум до занятия — от 0 до 168 часов"},
        {"buffer_minutes", &page.bufferMinutes, 0, 120, "перерыв между занятиями — от 0 до 120 минут"},
    };
    for (const auto& bound : bounds) {
        const long long value = payload.getInt(bound.key, *bound.field);
        if (value < bound.low || value > bound.high) return Result::failure(protocol::code::kBadRequest, bound.message);
        *bound.field = static_cast<int>(value);
    }
    if (payload.contains("hours")) {
        if (!intList(payload.get("hours"), 0, 23, page.hours)) {
            return Result::failure(protocol::code::kBadRequest, "часы — числа от 0 до 23");
        }
    }
    if (payload.contains("weekdays")) {
        Json weekdays;
        if (!intList(payload.get("weekdays"), 0, 6, weekdays) || weekdays.items().empty()) {
            return Result::failure(protocol::code::kBadRequest, "дни недели — от 1 до 7 чисел 0–6 (0 — понедельник)");
        }
        page.weekdays = weekdays;
    }
    page.allowWeekly = payload.getBool("allow_weekly", page.allowWeekly);
    page.active = payload.getBool("active", page.active);

    if (id > 0) {
        const DatabaseError saved = database_.db().updateBookingPage(page);
        if (!saved.ok) return Result::failure(protocol::code::kInternal, "не удалось сохранить страницу");
    } else {
        long long newId = 0;
        for (int attempt = 0; attempt < 3 && newId == 0; ++attempt) {
            page.slug = newSlug();
            if (!database_.db().createBookingPage(page, newId).ok) newId = 0;
        }
        if (newId == 0) return Result::failure(protocol::code::kInternal, "не удалось создать страницу");
        page.id = newId;
    }
    const auto stored = database_.db().findBookingPage(page.id);
    Result result;
    result.ok = true;
    result.payload.set("page", pageJson(stored ? *stored : page));
    return result;
}

BookingManager::Result BookingManager::deletePage(long long ownerId, const Json& payload) {
    const auto page = database_.db().findBookingPage(payload.getInt("id"));
    if (!page || page->ownerId != ownerId) return Result::failure(protocol::code::kNotFound, "страница записи не найдена");
    if (!database_.db().deleteBookingPage(page->id).ok) {
        return Result::failure(protocol::code::kInternal, "не удалось удалить страницу");
    }
    Result result;
    result.ok = true;
    result.payload.set("deleted", Json(true));
    return result;
}

BookingManager::Result BookingManager::rotatePage(long long ownerId, const Json& payload) {
    auto page = database_.db().findBookingPage(payload.getInt("id"));
    if (!page || page->ownerId != ownerId) return Result::failure(protocol::code::kNotFound, "страница записи не найдена");
    bool saved = false;
    for (int attempt = 0; attempt < 3 && !saved; ++attempt) {
        page->slug = newSlug();
        saved = database_.db().updateBookingPage(*page).ok;
    }
    if (!saved) return Result::failure(protocol::code::kInternal, "не удалось выдать новую ссылку");
    const auto stored = database_.db().findBookingPage(page->id);
    Result result;
    result.ok = true;
    result.payload.set("page", pageJson(stored ? *stored : *page));
    return result;
}

BookingManager::Result BookingManager::list(long long ownerId, const Json& payload) {
    const long long days = std::max(1LL, std::min(120LL, payload.getInt("days", 30)));
    const bool includeCancelled = payload.getBool("include_cancelled", false);
    const std::time_t now = std::time(nullptr);
    const std::time_t from = now - kDay;
    const std::time_t to = now + days * kDay;

    std::vector<std::pair<std::time_t, Json>> items;
    const auto records = includeCancelled ? database_.db().listBookings(ownerId, 500)
                                          : database_.db().listActiveBookings(ownerId);
    for (const auto& booking : records) {
        Json json = ownerBookingJson(booking, from, to);
        // Ближайшее занятие для сортировки: первое не отменённое, иначе первое в окне.
        std::time_t next = 0;
        const Json occurrences = json.get("occurrences");
        for (const auto& occurrence : occurrences.items()) {
            std::time_t start = 0;
            if (!parseIsoUtc(occurrence.getString("start"), start)) continue;
            if (next == 0 || (occurrence.getString("status") == "booked" && start < next)) next = start;
        }
        if (occurrences.items().empty()) continue;
        items.emplace_back(next, json);
    }
    std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    Json bookings = Json::array();
    for (const auto& item : items) bookings.push(item.second);
    Result result;
    result.ok = true;
    result.payload.set("bookings", bookings);
    return result;
}

BookingManager::Result BookingManager::cancelOccurrence(BookingRecord booking, const Json& payload, bool byGuest) {
    if (booking.status != "active") return Result::failure(protocol::code::kConflict, "запись уже отменена");
    const std::time_t now = std::time(nullptr);
    long long index = -1;
    if (payload.contains("index")) {
        index = payload.getInt("index", -1);
        const auto occurrences = bookingOccurrences(booking);
        const auto it = std::find_if(occurrences.begin(), occurrences.end(),
                                     [index](const BookingOccurrence& o) { return o.index == index; });
        if (it == occurrences.end()) return Result::failure(protocol::code::kBadRequest, "нет такого занятия");
        if (it->cancelled) return Result::failure(protocol::code::kConflict, "занятие уже отменено");
        if (it->start <= now) return Result::failure(protocol::code::kConflict, "занятие уже началось или прошло");
        booking.skipped.push(Json(index));
        // Отменены все будущие занятия — запись закрыта.
        bool anyFuture = false;
        for (const auto& occurrence : bookingOccurrences(booking)) {
            if (!occurrence.cancelled && occurrence.start > now) anyFuture = true;
        }
        if (!anyFuture) booking.status = "cancelled";
    } else {
        booking.status = "cancelled";
    }
    if (!database_.db().updateBooking(booking).ok) {
        return Result::failure(protocol::code::kInternal, "не удалось отменить");
    }
    if (byGuest && notifications_) {
        Json notify = Json::object();
        notify.set("booking_id", Json(booking.id));
        notify.set("index", Json(index));
        notify.set("guest_name", Json(booking.guestName));
        notifications_->create(booking.ownerId, "booking.cancelled",
                               booking.guestName + (index >= 0 ? " отменил(а) занятие" : " отменил(а) запись"),
                               booking.title, notify);
    }
    const auto stored = database_.db().findBooking(booking.id);
    Result result;
    result.ok = true;
    if (byGuest) {
        result.payload.set("booking", guestBookingJson(stored ? *stored : booking));
    } else {
        result.payload.set("booking", ownerBookingJson(stored ? *stored : booking, now - kDay, now + 120 * kDay));
    }
    return result;
}

BookingManager::Result BookingManager::cancelByOwner(long long ownerId, const Json& payload) {
    const auto booking = database_.db().findBooking(payload.getInt("booking_id"));
    if (!booking || booking->ownerId != ownerId) return Result::failure(protocol::code::kNotFound, "запись не найдена");
    std::lock_guard<std::mutex> lock(bookMutex_);
    return cancelOccurrence(*booking, payload, false);
}

// ----------------------------------------------------------------- гость

BookingManager::Result BookingManager::get(const Json& payload, const std::string& remoteAddr) {
    if (limited("booking.get:" + Session::ipOf(remoteAddr), kGetPerMinute, 60)) {
        return Result::failure(protocol::code::kForbidden, "слишком много запросов, попробуйте через минуту");
    }
    const auto page = database_.db().findBookingPageBySlug(payload.getString("slug"));
    if (!page || !page->active) return Result::failure(protocol::code::kNotFound, "страница записи не найдена");
    Json slots;
    std::string error;
    if (!pageSlots(*page, slots, error)) {
        AURA_LOG(log::Level::Warn, "booking") << "подбор слотов не удался: " << error;
        return Result::failure(protocol::code::kUpstream, "не удалось подобрать время — попробуйте позже");
    }
    Result result;
    result.ok = true;
    result.payload.set("page", publicPageJson(*page));
    result.payload.set("slots", slots);
    return result;
}

BookingManager::Result BookingManager::book(const Json& payload, const std::string& remoteAddr) {
    BookingRecord booking;
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
    std::time_t start = 0;
    if (!parseIsoUtc(payload.getString("start"), start)) {
        return Result::failure(protocol::code::kBadRequest, "start — начало одного из свободных слотов");
    }
    const auto page = database_.db().findBookingPageBySlug(payload.getString("slug"));
    if (!page || !page->active) return Result::failure(protocol::code::kNotFound, "страница записи не найдена");
    const bool weekly = payload.getBool("weekly", false);
    if (weekly && !page->allowWeekly) {
        return Result::failure(protocol::code::kBadRequest, "на этой странице нельзя записаться на каждую неделю");
    }
    if (limited("booking.book:" + Session::ipOf(remoteAddr), kBookPerHour, 3600)) {
        return Result::failure(protocol::code::kForbidden, "слишком много записей с этого адреса, попробуйте позже");
    }

    Json slots;
    std::string error;
    if (!pageSlots(*page, slots, error)) {
        AURA_LOG(log::Level::Warn, "booking") << "подбор слотов не удался: " << error;
        return Result::failure(protocol::code::kUpstream, "не удалось проверить время — попробуйте позже");
    }
    const bool offered = std::any_of(slots.items().begin(), slots.items().end(), [start](const Json& slot) {
        std::time_t slotStart = 0;
        return parseIsoUtc(slot.getString("start"), slotStart) && slotStart == start;
    });
    if (!offered) return Result::failure(protocol::code::kConflict, "это время уже занято — выберите другое");

    std::lock_guard<std::mutex> lock(bookMutex_);
    const std::time_t duration = static_cast<std::time_t>(page->durationMinutes) * 60;
    const Busy busy = busyWithBuffer(page->ownerId, page->bufferMinutes);
    if (overlaps(busy, start, start + duration)) {
        return Result::failure(protocol::code::kConflict, "это время уже занято — выберите другое");
    }

    booking.ownerId = page->ownerId;
    booking.pageId = page->id;
    booking.title = page->title;
    booking.durationMinutes = page->durationMinutes;
    booking.firstStart = isoUtc(start);
    booking.weekly = weekly;
    booking.weeks = weekly ? referrals_.maxWeeks(page->ownerId) : 1;
    Json skippedDates = Json::array();
    for (int index = 1; index < booking.weeks; ++index) {
        const std::time_t at = start + index * kWeek;
        if (overlaps(busy, at, at + duration)) {
            booking.skipped.push(Json(static_cast<long long>(index)));
            skippedDates.push(Json(isoUtc(at)));
        }
    }

    std::string token;
    long long id = 0;
    for (int attempt = 0; attempt < 3 && id == 0; ++attempt) {
        token = newToken();
        booking.manageTokenHash = linkutil::tokenHash(token);
        if (!database_.db().createBooking(booking, id).ok) id = 0;
    }
    if (id == 0) return Result::failure(protocol::code::kInternal, "не удалось сохранить запись");
    booking.id = id;

    if (notifications_) {
        Json notify = Json::object();
        notify.set("booking_id", Json(id));
        notify.set("start", Json(booking.firstStart));
        notify.set("weekly", Json(weekly));
        notify.set("guest_name", Json(booking.guestName));
        notifications_->create(page->ownerId, "booking.created", "Новая запись: " + booking.guestName, page->title,
                               notify);
    }
    referrals_.onActivity(page->ownerId);

    const auto stored = database_.db().findBooking(id);
    Result result;
    result.ok = true;
    result.payload.set("booking", guestBookingJson(stored ? *stored : booking));
    result.payload.set("manage_token", Json(token));
    result.payload.set("manage_url",
                       Json(config_.publicUrl.empty() ? std::string() : joinUrl(config_.publicUrl, "/#/m/" + token)));
    result.payload.set("skipped", skippedDates);
    return result;
}

BookingManager::Result BookingManager::manageGet(const Json& payload, const std::string& remoteAddr) {
    if (limited("booking.manage:" + Session::ipOf(remoteAddr), kGetPerMinute, 60)) {
        return Result::failure(protocol::code::kForbidden, "слишком много запросов, попробуйте через минуту");
    }
    const std::string hash = linkutil::tokenHash(payload.getString("token"));
    const auto booking = hash.empty() ? std::nullopt : database_.db().findBookingByToken(hash);
    if (!booking) return Result::failure(protocol::code::kNotFound, "запись не найдена");
    Json slots = Json::array();
    if (booking->status == "active" && booking->pageId > 0) {
        const auto page = database_.db().findBookingPage(booking->pageId);
        std::string error;
        if (page && page->active && !pageSlots(*page, slots, error)) {
            AURA_LOG(log::Level::Warn, "booking") << "подбор слотов для переноса не удался: " << error;
            slots = Json::array();
        }
    }
    Result result;
    result.ok = true;
    result.payload.set("booking", guestBookingJson(*booking));
    result.payload.set("slots", slots);
    return result;
}

BookingManager::Result BookingManager::reschedule(const Json& payload, const std::string& remoteAddr) {
    if (limited("booking.change:" + Session::ipOf(remoteAddr), kManagePerMinute, 60)) {
        return Result::failure(protocol::code::kForbidden, "слишком много попыток, попробуйте через минуту");
    }
    const std::string hash = linkutil::tokenHash(payload.getString("token"));
    auto booking = hash.empty() ? std::nullopt : database_.db().findBookingByToken(hash);
    if (!booking) return Result::failure(protocol::code::kNotFound, "запись не найдена");
    if (booking->status != "active") return Result::failure(protocol::code::kConflict, "запись отменена");
    const long long index = payload.getInt("index", -1);
    std::time_t start = 0;
    if (!parseIsoUtc(payload.getString("start"), start)) {
        return Result::failure(protocol::code::kBadRequest, "start — начало одного из свободных слотов");
    }
    const auto occurrences = bookingOccurrences(*booking);
    const auto it = std::find_if(occurrences.begin(), occurrences.end(),
                                 [index](const BookingOccurrence& o) { return o.index == index; });
    if (it == occurrences.end()) return Result::failure(protocol::code::kBadRequest, "нет такого занятия");
    const std::time_t now = std::time(nullptr);
    if (it->cancelled) return Result::failure(protocol::code::kConflict, "занятие отменено");
    if (it->start <= now) return Result::failure(protocol::code::kConflict, "занятие уже началось или прошло");
    const auto page = booking->pageId > 0 ? database_.db().findBookingPage(booking->pageId) : std::nullopt;
    if (!page || !page->active) {
        return Result::failure(protocol::code::kConflict, "перенос недоступен — страница записи выключена");
    }
    Json slots;
    std::string error;
    if (!pageSlots(*page, slots, error)) {
        AURA_LOG(log::Level::Warn, "booking") << "подбор слотов для переноса не удался: " << error;
        return Result::failure(protocol::code::kUpstream, "не удалось проверить время — попробуйте позже");
    }
    const bool offered = std::any_of(slots.items().begin(), slots.items().end(), [start](const Json& slot) {
        std::time_t slotStart = 0;
        return parseIsoUtc(slot.getString("start"), slotStart) && slotStart == start;
    });
    if (!offered) return Result::failure(protocol::code::kConflict, "это время уже занято — выберите другое");

    std::lock_guard<std::mutex> lock(bookMutex_);
    const std::time_t duration = static_cast<std::time_t>(booking->durationMinutes) * 60;
    if (overlaps(busyWithBuffer(booking->ownerId, page->bufferMinutes), start, start + duration)) {
        return Result::failure(protocol::code::kConflict, "это время уже занято — выберите другое");
    }
    const std::string from = isoUtc(it->start);
    booking->moved.set(std::to_string(index), Json(isoUtc(start)));
    // Напоминание по новому времени придёт заново.
    Json reminded = Json::array();
    for (const auto& item : booking->reminded.items()) {
        if (intOf(item) != index) reminded.push(item);
    }
    booking->reminded = reminded;
    if (!database_.db().updateBooking(*booking).ok) {
        return Result::failure(protocol::code::kInternal, "не удалось перенести занятие");
    }
    if (notifications_) {
        Json notify = Json::object();
        notify.set("booking_id", Json(booking->id));
        notify.set("index", Json(index));
        notify.set("from", Json(from));
        notify.set("to", Json(isoUtc(start)));
        notify.set("guest_name", Json(booking->guestName));
        notifications_->create(booking->ownerId, "booking.rescheduled", booking->guestName + " перенес(ла) занятие",
                               booking->title, notify);
    }
    const auto stored = database_.db().findBooking(booking->id);
    Result result;
    result.ok = true;
    result.payload.set("booking", guestBookingJson(stored ? *stored : *booking));
    return result;
}

BookingManager::Result BookingManager::cancelByGuest(const Json& payload, const std::string& remoteAddr) {
    if (limited("booking.change:" + Session::ipOf(remoteAddr), kManagePerMinute, 60)) {
        return Result::failure(protocol::code::kForbidden, "слишком много попыток, попробуйте через минуту");
    }
    const std::string hash = linkutil::tokenHash(payload.getString("token"));
    const auto booking = hash.empty() ? std::nullopt : database_.db().findBookingByToken(hash);
    if (!booking) return Result::failure(protocol::code::kNotFound, "запись не найдена");
    std::lock_guard<std::mutex> lock(bookMutex_);
    return cancelOccurrence(*booking, payload, true);
}

// ------------------------------------------------------------ напоминания

std::size_t BookingManager::remindTick(long long ownerId) {
    const std::time_t now = std::time(nullptr);
    std::size_t sent = 0;
    std::lock_guard<std::mutex> lock(bookMutex_);
    for (auto booking : database_.db().listActiveBookings(ownerId)) {
        bool changed = false;
        for (const auto& occurrence : bookingOccurrences(booking)) {
            if (occurrence.cancelled || occurrence.start <= now || occurrence.start > now + kReminderLeadSec) continue;
            if (contains(booking.reminded, occurrence.index)) continue;
            booking.reminded.push(Json(static_cast<long long>(occurrence.index)));
            changed = true;
            if (notifications_) {
                Json notify = Json::object();
                notify.set("booking_id", Json(booking.id));
                notify.set("index", Json(static_cast<long long>(occurrence.index)));
                notify.set("start", Json(isoUtc(occurrence.start)));
                notify.set("guest_name", Json(booking.guestName));
                notifications_->create(booking.ownerId, "booking.reminder", "Скоро занятие: " + booking.guestName,
                                       booking.title, notify);
            }
            ++sent;
        }
        if (changed) database_.db().updateBooking(booking);
    }
    return sent;
}

}  // namespace aura
