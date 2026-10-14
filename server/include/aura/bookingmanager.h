// aura/bookingmanager.h — страницы записи (booking.*).
//
// Владелец заводит постоянную публичную ссылку <AURA_PUBLIC_URL>/#/b/<slug>:
// название, длительность встречи, на сколько дней вперёд открыта запись, лимит
// записей в день, минимальный запас до начала, можно ли брать еженедельную серию.
// Гость без Aura открывает ссылку (booking.get — публичный метод), видит
// свободное время владельца (AI-сервис, POST /v1/agent/slots — рабочие часы в
// поясе владельца) и записывается (booking.book). Сервер не доверяет AI: сам
// проверяет длительность, окно, запас, занятость, пересечения и лимит в день.
//
// Гость получает одноразово показанный токен ссылки управления #/m/<токен>
// (в БД — только sha256): по ней он переносит встречу (booking.reschedule,
// не больше 3 раз) или отменяет одну встречу серии либо все (booking.cancel).
// Владельцу — задача с напоминанием на каждую встречу и уведомления
// booking.created / booking.rescheduled / booking.cancelled. Встречи записей
// занимают время владельца (MemoryManager::busy) — в переговорах, приглашениях
// и на других страницах.
#pragma once

#include <chrono>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aura/json.h"

namespace aura {

class DatabaseManager;
class MailProvider;
class MemoryManager;
class NotificationsManager;
class ReferralManager;
struct BookingPageRecord;
struct BookingRecord;
struct Config;

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

    static constexpr int kMaxPages = 10;
    static constexpr int kMaxReschedules = 3;
    static constexpr int kGetPerMinute = 30;
    static constexpr int kBookPerMinute = 10;
    static constexpr int kBookPerHour = 30;
    static constexpr int kChangePerMinute = 10;
    static constexpr int kMailPerAddressHour = 10;
    static constexpr int kResendPerMinute = 5;

    BookingManager(DatabaseManager& database, MemoryManager& memory, NotificationsManager* notifications,
                   ReferralManager& referrals, const Config& config);
    ~BookingManager();

    // Владелец (нужен вход).
    Result pageCreate(long long ownerId, const Json& payload);
    Result pageList(long long ownerId);
    Result pageUpdate(long long ownerId, const Json& payload);
    Result pageDelete(long long ownerId, const Json& payload);
    Result list(long long ownerId, const Json& payload);
    Result ownerCancel(long long ownerId, const Json& payload);

    // Гость (без входа): slug страницы или токен ссылки управления в payload.
    Result get(const Json& payload, const std::string& remoteAddr);
    Result book(const Json& payload, const std::string& remoteAddr);
    Result manage(const Json& payload, const std::string& remoteAddr);
    Result reschedule(const Json& payload, const std::string& remoteAddr);
    Result cancel(const Json& payload, const std::string& remoteAddr);
    // Потерянная ссылка управления: новые ссылки на активные записи этой страницы
    // уходят письмом на email гостя (старые перестают работать). Ответ всегда
    // одинаковый — по нему нельзя узнать, записан ли кто-то с этим адресом.
    Result resendLink(const Json& payload, const std::string& remoteAddr);

    // Токен ссылки управления → ключ поиска в БД (hex sha256); "" — заведомо неверный.
    static std::string tokenHash(const std::string& token);
    // Адрес страницы: 10 символов [a-z0-9].
    static bool validSlug(const std::string& slug);

    using Busy = std::vector<std::pair<std::time_t, std::time_t>>;

private:
    struct Offer {
        std::time_t start = 0;
        std::time_t end = 0;
        std::string day;    // местная дата владельца YYYY-MM-DD
        std::string local;  // местное время владельца YYYY-MM-DDTHH:MM ("" — неизвестно)
    };
    struct Cached {
        std::chrono::steady_clock::time_point at;
        std::string version;
        std::vector<Offer> offers;
    };

    Json pageJson(const BookingPageRecord& page);
    Json bookingJson(const BookingRecord& booking, const std::string& pageTitle) const;
    Json guestBookingJson(const BookingRecord& booking, const BookingPageRecord& page, bool withSlots);
    void ownerInfo(long long ownerId, std::string& name, std::string& timezone) const;

    // Еженедельная серия от first в местном времени владельца (AI POST /v1/agent/series):
    // повтор сохраняет местное время при переходе на летнее/зимнее. false — сервис недоступен
    // или ответ не прошёл проверку.
    bool series(const BookingPageRecord& page, const Offer& first, int count, std::vector<Offer>& out);
    // Кандидаты от AI-сервиса (кэш на минуту); false — сервис недоступен.
    bool offers(const BookingPageRecord& page, std::vector<Offer>& out);
    // Свободное время страницы сейчас: запас, занятость, лимит в день. skipBooking/skipStart —
    // встреча, которую переносят: её интервал и место в лимите дня не учитываются.
    std::vector<Offer> available(const BookingPageRecord& page, const std::vector<Offer>& candidates,
                                 long long skipBooking = 0, const std::string& skipStart = "");
    // Активные встречи страницы по местным дням (для лимита max_per_day).
    std::unordered_map<std::string, int> dayCounts(const BookingPageRecord& page, long long skipBooking,
                                                   const std::string& skipStart);

    long long createTask(const BookingRecord& booking, const std::string& pageTitle, std::time_t start);
    // Отмена встреч: index < 0 — все будущие активные. Возвращает число отменённых.
    int cancelOccurrences(BookingRecord& booking, int index, std::time_t now);
    static int findOccurrence(const BookingRecord& booking, const std::string& start, std::time_t now);

    bool limited(const std::string& key, int perWindow, int windowSeconds = 60);

    // Письмо гостю о записи (если указан email, есть лимит на адрес). Текст — только
    // от сервера и владельца (название страницы, имя): имя гостя в письмо не попадает,
    // чтобы форму нельзя было использовать для рассылки произвольного текста.
    void mailGuest(const BookingRecord& booking, const BookingPageRecord& page, const std::string& kind,
                   const std::string& subject, const std::string& title, const std::string& lead,
                   const std::string& token, bool onlyActive);
    std::string manageUrl(const std::string& token) const;

    DatabaseManager& database_;
    MemoryManager& memory_;
    NotificationsManager* notifications_;
    ReferralManager& referrals_;
    const Config& config_;
    std::unique_ptr<MailProvider> mail_;

    // Запись, перенос и отмена: проверка «свободно» и сохранение — под одной
    // блокировкой, чтобы два гостя не заняли одно время одновременно.
    std::mutex changeMutex_;

    std::mutex cacheMutex_;
    std::unordered_map<long long, Cached> cache_;

    struct Window {
        std::chrono::steady_clock::time_point start;
        int count = 0;
    };
    std::mutex limitsMutex_;
    std::unordered_map<std::string, Window> limits_;
};

}  // namespace aura
