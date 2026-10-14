// aura/bookingmanager.h — запись по постоянной ссылке (booking.*).
//
// Владелец (репетитор) создаёт страницу записи с публичной ссылкой /#/b/<slug>:
// длительность, окно записи, минимальный срок до занятия, буфер, часы и дни.
// Гость без аккаунта выбирает свободное время (слоты считает AI-сервис по
// календарю владельца, сервер перепроверяет), может записаться «каждую неделю»
// (серия из 8 занятий на Free, 26 на Pro) и получает личную ссылку /#/m/<токен>
// для переноса и отмены. Занятия занимают время владельца в календаре
// (MemoryManager::busy / context), за 15 минут приходит напоминание.
#pragma once

#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aura/json.h"

namespace aura {

class DatabaseManager;
class MemoryManager;
class NotificationsManager;
class ReferralManager;
struct BookingPageRecord;
struct BookingRecord;
struct Config;

// Занятие записи: index — номер в серии (0 для разовой).
struct BookingOccurrence {
    int index = 0;
    std::time_t start = 0;
    std::time_t end = 0;
    bool cancelled = false;
};

// Все занятия записи (с учётом переносов и отмен), по возрастанию index.
std::vector<BookingOccurrence> bookingOccurrences(const BookingRecord& record);

class BookingManager {
public:
    struct Result {
        bool ok = false;
        std::string code;
        std::string message;
        Json payload = Json::object();

        static Result failure(std::string code, std::string message) {
            Result result;
            result.code = std::move(code);
            result.message = std::move(message);
            return result;
        }
    };

    static constexpr int kReminderLeadSec = 15 * 60;
    static constexpr int kGetPerMinute = 30;
    static constexpr int kBookPerHour = 5;
    static constexpr int kManagePerMinute = 20;

    BookingManager(DatabaseManager& database, MemoryManager& memory, NotificationsManager* notifications,
                   ReferralManager& referrals, const Config& config);

    // Владелец.
    Result listPages(long long ownerId);
    Result savePage(long long ownerId, const Json& payload);
    Result deletePage(long long ownerId, const Json& payload);
    Result rotatePage(long long ownerId, const Json& payload);
    Result list(long long ownerId, const Json& payload);
    Result cancelByOwner(long long ownerId, const Json& payload);

    // Гость (публичные методы).
    Result get(const Json& payload, const std::string& remoteAddr);
    Result book(const Json& payload, const std::string& remoteAddr);
    Result manageGet(const Json& payload, const std::string& remoteAddr);
    Result reschedule(const Json& payload, const std::string& remoteAddr);
    Result cancelByGuest(const Json& payload, const std::string& remoteAddr);

    // Напоминания владельцу за 15 минут до занятия (ownerId = 0 — всем);
    // возвращает число отправленных.
    std::size_t remindTick(long long ownerId = 0);

private:
    using Busy = std::vector<std::pair<std::time_t, std::time_t>>;

    Json pageJson(const BookingPageRecord& page) const;
    Json publicPageJson(const BookingPageRecord& page);
    Json ownerBookingJson(const BookingRecord& booking, std::time_t from, std::time_t to) const;
    Json guestBookingJson(const BookingRecord& booking);
    // Свободные слоты страницы (AI + перепроверка). false — AI-сервис недоступен.
    bool pageSlots(const BookingPageRecord& page, Json& slots, std::string& error);
    Busy busyWithBuffer(long long ownerId, int bufferMinutes);
    Result cancelOccurrence(BookingRecord booking, const Json& payload, bool byGuest);
    bool limited(const std::string& key, int limit, int windowSec);
    std::string newSlug();

    DatabaseManager& database_;
    MemoryManager& memory_;
    NotificationsManager* notifications_;
    ReferralManager& referrals_;
    const Config& config_;

    // Проверка «свободно» и запись — под одной блокировкой (один процесс сервера).
    std::mutex bookMutex_;

    struct Window {
        std::time_t start = 0;
        int count = 0;
    };
    std::mutex limitsMutex_;
    std::unordered_map<std::string, Window> limits_;
};

}  // namespace aura
