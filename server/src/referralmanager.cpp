// aura/referralmanager.cpp
#include "aura/referralmanager.h"

#include <algorithm>
#include <cctype>

#include "aura/config.h"
#include "aura/crypto.h"
#include "aura/databasemanager.h"
#include "aura/log.h"
#include "aura/memorymanager.h"
#include "notificationsmanager.h"

namespace aura {

namespace {

// Без I, O, 0, 1 — код диктуют голосом и переписывают руками.
constexpr char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
constexpr std::size_t kCodeLength = 8;

std::string normalizeCode(const std::string& code) {
    std::string out;
    for (const unsigned char ch : code) {
        if (std::isspace(ch) != 0 || ch == '-') continue;
        out.push_back(static_cast<char>(std::toupper(ch)));
    }
    return out;
}

}  // namespace

ReferralManager::ReferralManager(DatabaseManager& database, NotificationsManager* notifications, const Config& config)
    : database_(database), notifications_(notifications), config_(config) {}

std::string ReferralManager::codeFor(long long userId) {
    std::string code = database_.db().getReferralCode(userId);
    if (!code.empty()) return code;
    for (int attempt = 0; attempt < 5; ++attempt) {
        const std::string bytes = crypto::randomBytes(kCodeLength);
        code.clear();
        // 32 символа алфавита: младшие 5 бит байта дают равномерный выбор.
        for (const unsigned char byte : bytes) code.push_back(kAlphabet[byte & 31]);
        if (database_.db().setReferralCode(userId, code)) return code;
    }
    AURA_LOG(log::Level::Warn, "referral") << "не удалось выдать код пользователю " << userId;
    return "";
}

bool ReferralManager::isPro(long long userId) {
    std::time_t until = 0;
    return parseIsoUtc(database_.db().getProUntil(userId), until) && until > std::time(nullptr);
}

Json ReferralManager::plan(long long userId) {
    const bool pro = isPro(userId);
    Json json = Json::object();
    json.set("name", Json(std::string(pro ? "pro" : "free")));
    json.set("pro_until", Json(pro ? database_.db().getProUntil(userId) : std::string()));
    json.set("max_pages", Json(static_cast<long long>(pro ? kProPages : kFreePages)));
    json.set("max_weeks", Json(static_cast<long long>(pro ? kProWeeks : kFreeWeeks)));
    return json;
}

Json ReferralManager::info(long long userId) {
    const std::string code = codeFor(userId);
    const std::string since = isoUtc(std::time(nullptr) - 365LL * 24 * 3600);
    const int qualifiedYear = database_.db().countQualifiedReferrals(userId, since);
    Json json = Json::object();
    json.set("code", Json(code));
    std::string url;
    if (!config_.publicUrl.empty() && !code.empty()) url = config_.publicUrl + "/#/signup?ref=" + code;
    json.set("url", Json(url));
    json.set("invited", Json(static_cast<long long>(database_.db().countReferrals(userId))));
    json.set("qualified", Json(static_cast<long long>(database_.db().countQualifiedReferrals(userId, "1970-01-01T00:00:00Z"))));
    json.set("reward_days", Json(static_cast<long long>(kRewardDays)));
    json.set("rewards_left", Json(static_cast<long long>(std::max(0, kMaxRewardsPerYear - qualifiedYear))));
    json.set("plan", plan(userId));
    return json;
}

bool ReferralManager::applyOnRegister(long long inviteeId, const std::string& code) {
    const std::string normalized = normalizeCode(code);
    if (normalized.size() != kCodeLength) return false;
    const long long referrerId = database_.db().findUserIdByReferralCode(normalized);
    if (referrerId <= 0 || referrerId == inviteeId) return false;
    const auto referrer = database_.db().findUserById(referrerId);
    if (!referrer || !referrer->active) return false;
    const DatabaseError created = database_.db().createReferral(referrerId, inviteeId);
    if (!created.ok) return false;
    Json detail = Json::object();
    detail.set("referrer_id", Json(referrerId));
    database_.db().insertAudit(inviteeId, "referral_joined", detail, "");
    return true;
}

std::string ReferralManager::extendPro(long long userId, int days) {
    std::time_t base = std::time(nullptr);
    std::time_t current = 0;
    if (parseIsoUtc(database_.db().getProUntil(userId), current) && current > base) base = current;
    const std::string until = isoUtc(base + static_cast<std::time_t>(days) * 24 * 3600);
    const DatabaseError saved = database_.db().setProUntil(userId, until);
    if (!saved.ok) AURA_LOG(log::Level::Warn, "referral") << "pro_until не сохранён: " << saved.message;
    return until;
}

void ReferralManager::onActivity(long long userId) {
    const auto referral = database_.db().findReferralByInvitee(userId);
    if (!referral || !referral->qualifiedAt.empty()) return;
    const auto invitee = database_.db().findUserById(userId);
    if (!invitee || !invitee->emailVerified) return;
    // Лимит наград пригласившему считаем до засчитывания этого приглашения.
    const std::string since = isoUtc(std::time(nullptr) - 365LL * 24 * 3600);
    const bool referrerRewarded =
        database_.db().countQualifiedReferrals(referral->referrerId, since) < kMaxRewardsPerYear;
    if (!database_.db().qualifyReferral(referral->id)) return;  // уже засчитано параллельно

    auto reward = [this](long long user, const std::string& role, const std::string& body) {
        const std::string until = extendPro(user, kRewardDays);
        Json detail = Json::object();
        detail.set("days", Json(static_cast<long long>(kRewardDays)));
        detail.set("pro_until", Json(until));
        detail.set("role", Json(role));
        database_.db().insertAudit(user, "referral_rewarded", detail, "");
        if (notifications_) notifications_->create(user, "referral.rewarded", "Бонус: 30 дней Pro", body, detail);
    };
    reward(userId, "invitee", "Вы начали пользоваться Aura по приглашению — дарим месяц Pro");
    if (referrerRewarded) {
        reward(referral->referrerId, "referrer",
               (invitee->displayName.empty() ? std::string("Приглашённый коллега") : invitee->displayName) +
                   " начал(а) пользоваться Aura — вам месяц Pro");
    }
}

}  // namespace aura
