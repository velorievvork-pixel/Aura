// aura/schedulelock.cpp
#include "aura/schedulelock.h"

#include <chrono>
#include <thread>

#include "aura/databasemanager.h"
#include "aura/log.h"

namespace aura {

ScheduleLock::Guard::Guard(DatabaseManager* database, std::unique_lock<std::mutex> lock, long long ownerId, bool ok)
    : database_(database), lock_(std::move(lock)), ownerId_(ownerId), ok_(ok) {}

ScheduleLock::Guard::Guard(Guard&& other) noexcept
    : database_(other.database_), lock_(std::move(other.lock_)), ownerId_(other.ownerId_), ok_(other.ok_) {
    other.ok_ = false;
    other.database_ = nullptr;
}

ScheduleLock::Guard::~Guard() {
    if (ok_ && database_) database_->db().unlockSchedule(ownerId_);
}

ScheduleLock::Guard ScheduleLock::acquire(long long ownerId) {
    std::unique_lock<std::mutex> lock(mutex_);
    // Короткие попытки вместо блокирующего ожидания: соединение с БД общее,
    // нельзя держать его, пока ждём другой экземпляр.
    for (int attempt = 0; attempt < 120; ++attempt) {
        if (database_.db().tryLockSchedule(ownerId)) return Guard(&database_, std::move(lock), ownerId, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    AURA_LOG(log::Level::Warn, "schedule") << "расписание владельца " << ownerId << " занято другим экземпляром";
    return Guard(&database_, std::move(lock), ownerId, false);
}

}  // namespace aura
