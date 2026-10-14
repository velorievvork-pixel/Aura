// aura/referralmanager.h — реферальная программа (referral.*) и тариф Free/Pro.
//
// Каждый пользователь получает код приглашения (8 символов, выдаётся при первом
// запросе). Новый пользователь может указать код в auth.register. Бонус — +30
// дней Pro обоим — начисляется не за регистрацию, а когда приглашённый впервые
// реально воспользовался Aura (получил запись на свою страницу или ответ гостя
// по приглашению). Пригласившему — не больше 12 наград за 365 дней.
// Оплаты пока нет: Pro даёт больше страниц записи и длиннее серии занятий.
#pragma once

#include <ctime>
#include <string>

#include "aura/json.h"

namespace aura {

class DatabaseManager;
class NotificationsManager;
struct Config;

class ReferralManager {
public:
    static constexpr int kRewardDays = 30;
    static constexpr int kMaxRewardsPerYear = 12;
    static constexpr int kFreePages = 1;
    static constexpr int kProPages = 5;
    static constexpr int kFreeWeeks = 8;
    static constexpr int kProWeeks = 26;

    ReferralManager(DatabaseManager& database, NotificationsManager* notifications, const Config& config);

    // Код пользователя; при отсутствии выдаёт новый. "" — не удалось сохранить.
    std::string codeFor(long long userId);
    // referral.info: {code, url, invited, qualified, reward_days, rewards_left, plan}.
    Json info(long long userId);
    // auth.register с referral_code: true — приглашение учтено.
    bool applyOnRegister(long long inviteeId, const std::string& code);
    // Приглашённый реально воспользовался Aura — засчитать приглашение и начислить бонус.
    void onActivity(long long userId);

    bool isPro(long long userId);
    // {name, pro_until, max_pages, max_weeks}
    Json plan(long long userId);
    int maxPages(long long userId) { return isPro(userId) ? kProPages : kFreePages; }
    int maxWeeks(long long userId) { return isPro(userId) ? kProWeeks : kFreeWeeks; }

private:
    std::string extendPro(long long userId, int days);

    DatabaseManager& database_;
    NotificationsManager* notifications_;
    const Config& config_;
};

}  // namespace aura
