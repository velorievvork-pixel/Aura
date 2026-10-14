// aura/schedulelock.h — блокировка расписания владельца на время «проверить, что
// время свободно → записать встречу» (invite.respond, booking.*).
//
// Внутри процесса — один mutex (приглашения и записи одного владельца не займут
// одно время одновременно). Между экземплярами сервера на общем PostgreSQL —
// advisory lock по id владельца (IDatabase::tryLockSchedule); встроенное
// хранилище работает в одном процессе, ему хватает mutex.
#pragma once

#include <mutex>

namespace aura {

class DatabaseManager;

class ScheduleLock {
public:
    explicit ScheduleLock(DatabaseManager& database) : database_(database) {}

    class Guard {
    public:
        Guard(Guard&& other) noexcept;
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        Guard& operator=(Guard&&) = delete;
        ~Guard();
        // false — другой экземпляр сервера не отпустил расписание за отведённое время.
        bool ok() const { return ok_; }

    private:
        friend class ScheduleLock;
        Guard(DatabaseManager* database, std::unique_lock<std::mutex> lock, long long ownerId, bool ok);
        DatabaseManager* database_;
        std::unique_lock<std::mutex> lock_;
        long long ownerId_;
        bool ok_;
    };

    // Ждёт блокировку не дольше ~3 с.
    Guard acquire(long long ownerId);

private:
    DatabaseManager& database_;
    std::mutex mutex_;
};

}  // namespace aura
