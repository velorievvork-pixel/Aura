// aura/server.h — сборка всех модулей и маршрутизация сообщений.
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "aura/agentmanager.h"
#include "aura/authmanager.h"
#include "aura/chatmanager.h"
#include "aura/config.h"
#include "aura/connectionmanager.h"
#include "aura/databasemanager.h"
#include "aura/listener.h"
#include "aura/memorymanager.h"
#include "aura/protocol.h"
#include "aura/taskmanager.h"
#include "aura/toolmanager.h"

namespace aura {

class IntegrationManager;   // server/src/integrationmanager.h (этап 9)
class NotificationsManager;  // server/src/notificationsmanager.h (этап 13)

class Server {
public:
    using Handler = std::function<Json(std::shared_ptr<Session>, const protocol::Request&)>;

    explicit Server(Config config = Config::fromEnv());
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool start(std::string& error);
    void stop();
    void wait();               // блокирует поток до stop()
    void requestStop();        // потокобезопасный сигнал остановки

    int port() const { return listener_ ? listener_->port() : config_.port; }
    const Config& config() const { return config_; }

    Json health() const;

    // Доступ к менеджерам (нужен тестам и расширяющим модулям).
    DatabaseManager& database() { return *database_; }
    AuthManager& auth() { return *auth_; }
    ChatManager& chats() { return *chats_; }
    MemoryManager& memory() { return *memory_; }
    ToolManager& tools() { return *tools_; }
    AgentManager& agent() { return *agent_; }
    TaskManager& taskManager() { return *taskManager_; }
    IntegrationManager& integrations() { return *integrations_; }
    NotificationsManager& notifications() { return *notifications_; }
    ConnectionManager& connections() { return connections_; }

    // Один проход планировщика напоминаний (публично для тестов).
    // userId = 0 — все пользователи; иначе только напоминания этого пользователя.
    std::size_t runSchedulerTick(int limit = 100, long long userId = 0);

    // «Выбить» открытые соединения пользователя (событие session.revoked +
    // закрытие сокета). keepJwtId — не трогать соединения этой сессии;
    // onlyJwtId — только соединения этой сессии; exceptSessionId — не
    // трогать данное соединение (тот, кто сделал запрос). Пригодится и для
    // блокировки пользователя администратором.
    std::size_t kickUser(long long userId, const std::string& reason,
                         const std::string& keepJwtId = "", const std::string& onlyJwtId = "",
                         const std::string& exceptSessionId = "");

    // Обработка одного сообщения (публично: тестируется без сокетов).
    Json handleMessage(std::shared_ptr<Session> session, const Json& message);

    void registerHandler(const std::string& type, Handler handler);
    bool hasHandler(const std::string& type) const;
    std::vector<std::string> handlerTypes() const;

private:
    void handleConnection(std::shared_ptr<Session> session);
    void registerHandlers();
    bool authorize(std::shared_ptr<Session> session, const std::string& token, Json& payload);
    // Периодическая сверка авторизованного соединения с БД (отзыв/срок/блокировка).
    bool revalidate(const std::shared_ptr<Session>& session);

    Config config_;
    std::unique_ptr<DatabaseManager> database_;
    std::unique_ptr<AuthManager> auth_;
    std::unique_ptr<ChatManager> chats_;
    std::unique_ptr<MemoryManager> memory_;
    std::unique_ptr<ToolManager> tools_;
    std::unique_ptr<AgentManager> agent_;
    std::unique_ptr<TaskManager> taskManager_;
    std::unique_ptr<IntegrationManager> integrations_;
    std::unique_ptr<NotificationsManager> notifications_;
    std::unique_ptr<Listener> listener_;
    ConnectionManager connections_;

    mutable std::mutex handlersMutex_;
    std::map<std::string, Handler> handlers_;

    std::mutex waitMutex_;
    std::condition_variable waitCondition_;
    std::atomic<bool> stopping_{false};

    // Фоновый планировщик напоминаний.
    void schedulerLoop();
    std::unique_ptr<std::thread> schedulerThread_;
    std::mutex schedulerMutex_;
    std::condition_variable schedulerCv_;
};

}  // namespace aura
