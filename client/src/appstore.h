// client/src/appstore.h — состояние приложения для QML.
//
// Единственный объект, который видит QML (контекстное свойство App):
// держит авторизацию, списки чатов/сообщений/памяти и вызывает WebSocketClient.
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

QT_BEGIN_NAMESPACE
class QAudioSource;
class QIODevice;
class QTcpServer;
class QTextToSpeech;
QT_END_NAMESPACE

namespace aura {

class WebSocketClient;

class AppStore : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY authenticatedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY profileChanged)
    Q_PROPERTY(QString userEmail READ userEmail NOTIFY profileChanged)
    Q_PROPERTY(qint64 userId READ userId NOTIFY profileChanged)
    Q_PROPERTY(QString userBio READ userBio NOTIFY profileChanged)
    Q_PROPERTY(QVariantList chats READ chats NOTIFY chatsChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(QVariantList memory READ memory NOTIFY memoryChanged)
    Q_PROPERTY(QVariantMap preferences READ preferences NOTIFY preferencesChanged)
    Q_PROPERTY(bool needOnboarding READ needOnboarding NOTIFY preferencesChanged)
    // Приватность ИИ (prefs.privacy): share_free_slots_only, hide_event_titles,
    // allow_place_choice — все три флага всегда есть, по умолчанию true.
    Q_PROPERTY(QVariantMap privacy READ privacy NOTIFY preferencesChanged)
    Q_PROPERTY(QString chatTitle READ chatTitle NOTIFY chatChanged)
    Q_PROPERTY(bool peerOnline READ peerOnline NOTIFY chatChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY connectedChanged)
    Q_PROPERTY(QString connectionLabel READ connectionLabel NOTIFY connectedChanged)
    // auth v3: подтверждение email, восстановление пароля, активные сессии.
    Q_PROPERTY(QString pendingEmail READ pendingEmail NOTIFY authFlowChanged)
    Q_PROPERTY(QString devEmailCode READ devEmailCode NOTIFY authFlowChanged)
    Q_PROPERTY(QString devResetCode READ devResetCode NOTIFY authFlowChanged)
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
    // 2FA (TOTP): состояние, настройка, доверенные устройства.
    Q_PROPERTY(bool twoFactorRequired READ twoFactorRequired NOTIFY twoFactorRequiredChanged)
    Q_PROPERTY(bool twoFactorEnabled READ twoFactorEnabled NOTIFY twoFactorStatusChanged)
    Q_PROPERTY(bool twoFactorPending READ twoFactorPending NOTIFY twoFactorStatusChanged)
    Q_PROPERTY(QString twoFactorSecret READ twoFactorSecret NOTIFY twoFactorSetupChanged)
    Q_PROPERTY(QString twoFactorUri READ twoFactorUri NOTIFY twoFactorSetupChanged)
    Q_PROPERTY(QVariantList recoveryCodes READ recoveryCodes NOTIFY twoFactorSetupChanged)
    Q_PROPERTY(int recoveryCodesLeft READ recoveryCodesLeft NOTIFY twoFactorStatusChanged)
    Q_PROPERTY(QVariantList trustedDevices READ trustedDevices NOTIFY trustedDevicesChanged)
    // Голосовой ввод (STT): idle → listening → processing → success/error.
    Q_PROPERTY(QString voiceState READ voiceState NOTIFY voiceStateChanged)
    Q_PROPERTY(QString transcript READ transcript NOTIFY transcriptChanged)
    Q_PROPERTY(bool micAvailable READ micAvailable NOTIFY micAvailableChanged)
    Q_PROPERTY(qreal voiceLevel READ voiceLevel NOTIFY voiceLevelChanged)
    Q_PROPERTY(bool autoSendVoice READ autoSendVoice NOTIFY autoSendVoiceChanged)
    Q_PROPERTY(QString micLanguage READ micLanguage NOTIFY micLanguageChanged)
    // Озвучка (TTS): платформенный синтез + «только важное».
    Q_PROPERTY(bool ttsEnabled READ ttsEnabled NOTIFY ttsChanged)
    Q_PROPERTY(QString ttsVoice READ ttsVoice NOTIFY ttsChanged)
    Q_PROPERTY(qreal ttsRate READ ttsRate NOTIFY ttsChanged)
    Q_PROPERTY(qreal ttsVolume READ ttsVolume NOTIFY ttsChanged)
    Q_PROPERTY(bool ttsImportantOnly READ ttsImportantOnly NOTIFY ttsChanged)
    Q_PROPERTY(bool speaking READ speaking NOTIFY speakingChanged)
    Q_PROPERTY(QStringList ttsVoices READ ttsVoices NOTIFY ttsChanged)
    // Этап 8: разрешения на инструменты, подтверждения опасных операций, задачи.
    Q_PROPERTY(QVariantList toolPermissions READ toolPermissions NOTIFY toolPermissionsChanged)
    Q_PROPERTY(QVariantList confirmations READ confirmations NOTIFY confirmationsChanged)
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY tasksChanged)
    // Этап 9: интеграции с внешними сервисами (Google Календарь, Gmail).
    Q_PROPERTY(QVariantList integrationProviders READ integrationProviders NOTIFY integrationsChanged)
    Q_PROPERTY(QVariantList integrations READ integrations NOTIFY integrationsChanged)
    Q_PROPERTY(QString integrationUrl READ integrationUrl NOTIFY integrationUrlChanged)
    // Этап 13: уведомления — in-app «входящая» и push-устройства.
    Q_PROPERTY(QVariantList notifications READ notifications NOTIFY notificationsChanged)
    Q_PROPERTY(int unreadNotifications READ unreadNotifications NOTIFY notificationsChanged)
    Q_PROPERTY(QVariantList pushDevices READ pushDevices NOTIFY pushDevicesChanged)
    // v3: Home-дашборд — ответ Ауры без чата; последний результат A2A-переговоров.
    Q_PROPERTY(QString homeReply READ homeReply NOTIFY homeReplyChanged)
    Q_PROPERTY(QVariantMap negotiation READ negotiation NOTIFY negotiationChanged)
    // v3: диалог с Аурой («Моя Аура»): реплики пользователя и ответы с действиями.
    Q_PROPERTY(QVariantList auraDialog READ auraDialog NOTIFY auraDialogChanged)
    // v3: раунды переговоров за сессию (свежие первыми), бронирование итога.
    Q_PROPERTY(QVariantList negotiationHistory READ negotiationHistory NOTIFY negotiationChanged)
    Q_PROPERTY(bool bookingBusy READ bookingBusy NOTIFY negotiationChanged)
    // v3: поиск людей для контактов (users.search).
    Q_PROPERTY(QVariantList userResults READ userResults NOTIFY userResultsChanged)

public:
    explicit AppStore(QObject* parent = nullptr);
    // Деструктор определён в .cpp: там WebSocketClient — полный тип,
    // а здесь только forward declaration (unique_ptr этого требует).
    ~AppStore() override;

    void setServerUrl(const QUrl& url);
    void start();  // подключается к серверу (и восстанавливает токен из QSettings)

    bool connected() const;
    bool authenticated() const { return userId_ != 0; }
    bool busy() const { return busy_; }
    QString userName() const { return userName_; }
    QString userEmail() const { return userEmail_; }
    qint64 userId() const { return userId_; }
    QString userBio() const { return userBio_; }
    QVariantList chats() const { return chats_; }
    QVariantList messages() const { return messages_; }
    QVariantList memory() const { return memory_; }
    QVariantMap preferences() const { return preferences_; }
    // Онбординг-опрос: сервер ставит prefs.onboarded при сохранении анкеты;
    // onboardingDismissed_ — локальное «Пропустить» до конца сессии.
    bool needOnboarding() const {
        return userId_ != 0 && !onboardingDismissed_ &&
               !preferences_.value(QStringLiteral("onboarded")).toBool();
    }
    void dismissOnboarding() { onboardingDismissed_ = true; emit preferencesChanged(); }
    QVariantMap privacy() const;
    // Профиль (profile.update): имя 1–80 символов, «о себе» до 300.
    Q_INVOKABLE void updateProfile(const QString& displayName, const QString& bio);
    // Приватность ИИ: prefs.set {"privacy": {key: value}}; при ошибке значение откатывается.
    Q_INVOKABLE void setPrivacy(const QString& key, bool value);
    QString chatTitle() const { return chatTitle_; }
    bool peerOnline() const { return peerOnline_; }
    QString statusMessage() const { return statusMessage_; }
    QString errorMessage() const { return errorMessage_; }
    QString serverUrl() const;
    QString connectionLabel() const;
    // Смена адреса сервера из интерфейса: ws:// или wss://, сохраняется в настройках.
    Q_INVOKABLE bool changeServerUrl(const QString& url);
    QString pendingEmail() const { return pendingEmail_; }
    QString devEmailCode() const { return devEmailCode_; }
    QString devResetCode() const { return devResetCode_; }
    QVariantList sessions() const { return sessions_; }
    // 2FA
    bool twoFactorRequired() const { return twoFactorRequired_; }
    bool twoFactorEnabled() const { return twoFactorEnabled_; }
    bool twoFactorPending() const { return twoFactorPending_; }
    QString twoFactorSecret() const { return twoFactorSecret_; }
    QString twoFactorUri() const { return twoFactorUri_; }
    QVariantList recoveryCodes() const { return recoveryCodes_; }
    int recoveryCodesLeft() const { return recoveryCodesLeft_; }
    QVariantList trustedDevices() const { return trustedDevices_; }
    // Голосовой ввод (STT)
    QString voiceState() const { return voiceState_; }
    QString transcript() const { return transcript_; }
    bool micAvailable() const { return micAvailable_; }
    qreal voiceLevel() const { return voiceLevel_; }
    bool autoSendVoice() const { return autoSendVoice_; }
    QString micLanguage() const { return micLanguage_; }
    // Озвучка (TTS)
    bool ttsEnabled() const { return ttsEnabled_; }
    QString ttsVoice() const { return ttsVoice_; }
    qreal ttsRate() const { return ttsRate_; }
    qreal ttsVolume() const { return ttsVolume_; }
    bool ttsImportantOnly() const { return ttsImportantOnly_; }
    bool speaking() const { return speaking_; }
    QStringList ttsVoices() const { return ttsVoices_; }
    // Этап 8
    QVariantList toolPermissions() const { return toolPermissions_; }
    QVariantList confirmations() const { return confirmations_; }
    QVariantList tasks() const { return tasks_; }
    // Этап 9
    QVariantList integrationProviders() const { return integrationProviders_; }
    QVariantList integrations() const { return integrations_; }
    QString integrationUrl() const { return integrationUrl_; }
    // Этап 13
    QVariantList notifications() const { return notifications_; }
    int unreadNotifications() const { return unreadNotifications_; }
    QVariantList pushDevices() const { return pushDevices_; }
    // v3
    QString homeReply() const { return homeReply_; }
    QVariantMap negotiation() const { return negotiation_; }
    QVariantList auraDialog() const { return auraDialog_; }
    QVariantList negotiationHistory() const { return negotiationHistory_; }
    bool bookingBusy() const { return bookingBusy_; }
    QVariantList userResults() const { return userResults_; }

public slots:
    void login(const QString& email, const QString& password);
    void registerUser(const QString& displayName, const QString& email, const QString& password);
    void logout();
    // auth v3
    void verifyEmail(const QString& email, const QString& code);
    void resendCode(const QString& email);
    void forgotPassword(const QString& email);
    void resetPassword(const QString& email, const QString& code, const QString& newPassword);
    void changePassword(const QString& oldPassword, const QString& newPassword);
    void loadSessions();
    void revokeSession(const QString& sessionId);
    void revokeAllSessions();
    // 2FA (TOTP)
    void login2fa(const QString& code, bool trustDevice);  // завершить вход с кодом
    void cancelTwoFactor();                                // отменить шаг 2FA (назад к логину)
    void setup2fa();                                       // начать настройку: секрет + otpauth URI
    void confirm2fa(const QString& code);                  // подтвердить и получить резервные коды
    void disable2fa(const QString& password);              // отключить (по паролю)
    void loadTwoFactorStatus();                            // статус: включена/настраивается/кодов осталось
    void loadTrustedDevices();
    void revokeTrustedDevice(const QString& id);
    void revokeAllTrustedDevices();
    void openChat(const QString& contact);
    void selectChat(qint64 chatId);
    void sendMessage(qint64 chatId, const QString& text);
    void askAgent(qint64 chatId, const QString& message);
    // v3: запрос к Ауре с Home-дашборда — без привязки к чату (chat_id=0).
    void askAgentGlobal(const QString& message);
    // v3: A2A-переговоры (agent.negotiate): цель, тема, длительность и окно в минутах/часах.
    void negotiateAgent(const QString& target, const QString& topic,
                        int durationMinutes, int windowHours);
    // v3: забронировать место из последних переговоров (book_table) и
    // поставить встречу в задачи. Режим «запретить» для book_table уважается.
    void bookNegotiation();
    void clearAuraDialog();
    // Ответ «С чем помогать?» из регистрации: сохраняется в память Ауры
    // (memory.add) при первом входе — до подтверждения email сессии ещё нет.
    void setPendingFocus(const QString& text);
    void searchUsers(const QString& query);
    void savePreferences(const QVariantMap& preferences);
    void refreshMemory();
    // Голосовой ввод (STT): запись с микрофона → /v1/speech/transcribe → текст.
    void startListening();                       // начать запись (idle → listening)
    void stopListening();                        // остановить и отправить на распознавание
    void cancelListening();                      // отменить запись без распознавания
    void setTranscript(const QString& text);     // правка текста перед отправкой
    void sendTranscript();                       // отправить распознанный текст в текущий чат
    void setAutoSendVoice(bool enabled);         // автоотправка после распознавания
    void setMicLanguage(const QString& lang);    // ru | en | auto
    // Озвучка (TTS): платформенный синтез речи.
    void speak(const QString& text);             // озвучить текст
    void speakImportant(const QString& text);    // озвучить, даже если режим «только важное»
    void stopSpeaking();
    void setTtsEnabled(bool enabled);
    void setTtsVoice(const QString& voice);
    void setTtsRate(qreal rate);                 // -1.0 … 1.0
    void setTtsVolume(qreal volume);             // 0.0 … 1.0
    void setTtsImportantOnly(bool enabled);
    // Этап 8: разрешения на инструменты (allow | ask | deny).
    void loadPermissions();
    void setToolPermission(const QString& tool, const QString& mode);
    // Этап 8: барьер подтверждения опасных операций.
    void loadConfirmations();
    void approveConfirmation(qint64 actionId);   // исполнить отложенное действие
    void denyConfirmation(qint64 actionId);      // отклонить отложенное действие
    // Этап 8: задачи и напоминания.
    void loadTasks();
    void createTask(const QString& title, const QString& notes, const QString& remindAt);
    // v3: задача со сроком (due_at) и напоминанием, ISO 8601; пустые — не задавать.
    void createTaskAt(const QString& title, const QString& dueAt, const QString& remindAt);
    void completeTask(qint64 taskId);
    void reopenTask(qint64 taskId);
    void cancelTask(qint64 taskId);
    void deleteTask(qint64 taskId);
    // Этап 9: интеграции (Google OAuth: системный браузер + loopback-редирект).
    void loadIntegrations();
    void beginIntegration(const QString& provider);   // открыть consent-экран Google
    void completeIntegration(const QString& provider, const QString& code,
                             const QString& state);   // обмен code на токены
    void revokeIntegration(qint64 connectionId);      // отозвать доступ
    void syncIntegration(qint64 connectionId);        // синхронизировать сейчас
    // Этап 13: уведомления и push-устройства.
    void loadNotifications();
    void markNotificationsRead(qint64 notificationId = 0);  // 0 — все
    void loadPushDevices();
    void revokePushDevice(qint64 deviceId);

signals:
    void connectedChanged();
    void authenticatedChanged();
    void busyChanged();
    void profileChanged();
    void chatsChanged();
    void messagesChanged();
    void memoryChanged();
    void preferencesChanged();
    void profileSaved();                        // profile.update принят сервером
    void profileSaveFailed(const QString& message);
    void privacySaveFailed(const QString& key, const QString& message);  // флаг откатан
    void chatChanged();
    void statusMessageChanged();
    void errorMessageChanged();
    // auth v3
    void authFlowChanged();        // pendingEmail/devEmailCode/devResetCode изменились
    void verificationRequested();  // нужен ввод кода подтверждения email
    void resetFlowRequested();     // код сброса отправлен — нужен новый пароль
    void sessionsChanged();
    // 2FA
    void twoFactorRequiredChanged();  // вход требует код 2FA (или шаг отменён)
    void twoFactorStatusChanged();    // enabled/pending/recoveryCodesLeft изменились
    void twoFactorSetupChanged();     // секрет/otpauth URI/резервные коды изменились
    void trustedDevicesChanged();
    // Голосовой ввод (STT)
    void voiceStateChanged();
    void transcriptChanged();
    void micAvailableChanged();
    void voiceLevelChanged();
    void autoSendVoiceChanged();
    void micLanguageChanged();
    // Озвучка (TTS)
    void ttsChanged();
    void speakingChanged();
    // Этап 8: разрешения, подтверждения, задачи
    void toolPermissionsChanged();
    void confirmationsChanged();
    void tasksChanged();
    // Этап 9: интеграции
    void integrationsChanged();
    void integrationUrlChanged();
    // Этап 13: уведомления
    void notificationsChanged();
    void pushDevicesChanged();
    // v3: Home и переговоры
    void homeReplyChanged();
    void negotiationChanged();
    void auraDialogChanged();
    void userResultsChanged();

private:
    void handleEvent(const QString& name, const QJsonObject& payload);
    void applyAuth(const QJsonObject& payload);
    void tryRefresh();  // обмен refresh-токена на новую пару при старте
    void clearSession();
    void setBusy(bool busy);
    void scheduleReconnect();
    void setStatus(const QString& message);
    void setError(const QString& message);
    void loadChats();
    void loadMessages(qint64 chatId);
    void loadPreferences();
    void loadMemory();
    // Голосовой ввод (STT) — захват PCM с микрофона, упаковка в WAV, отправка.
    void setVoiceState(const QString& state);
    bool ensureMicAvailable();          // проверяет наличие устройства ввода
    void startCapture();                // поднимает QAudioSource
    void stopCaptureAndTranscribe();    // останавливает запись и шлёт на сервер
    void onAudioReady();                // читает готовые байты PCM + уровень
    static QByteArray wrapWav(const QByteArray& pcm, int sampleRate, int channels, int bitsPerSample);
    // Интеграции (этап 9) — loopback HTTP-сервер ловит OAuth-редирект Google.
    bool ensureRedirectServer();        // поднимает 127.0.0.1:<случайный порт>
    void handleRedirectRequest();       // GET /callback?code&state → completeIntegration
    // Озвучка (TTS) — платформенный синтез.
    void initTts();                     // создаёт QTextToSpeech, читает настройки
    void applyTtsSettings();            // применяет voice/rate/volume к синтезатору
    void loadVoiceSettings();           // читает настройки голоса/TTS из QSettings
    void saveVoiceSettings();           // сохраняет настройки голоса/TTS

    std::unique_ptr<WebSocketClient> client_;
    QString serverUrlText_;
    QString token_;
    QString refreshToken_;
    QString userName_;
    QString userEmail_;
    QString userBio_;
    qint64 userId_ = 0;
    bool busy_ = false;
    int reconnectDelayMs_ = 2000;     // переподключение: 2 → 4 → 8 → 15 с
    bool reconnectScheduled_ = false;
    qint64 currentChatId_ = 0;
    QString chatTitle_;
    bool peerOnline_ = false;
    QVariantList chats_;
    QVariantList messages_;
    QVariantList memory_;
    QVariantMap preferences_;
    bool onboardingDismissed_ = false;
    int privacyPending_ = 0;          // prefs.set с privacy в полёте (не затираем оптимистичные значения)
    QString statusMessage_;
    QString errorMessage_;
    // auth v3
    QString pendingEmail_;   // email, ожидающий подтверждения/сброса
    QString devEmailCode_;   // код подтверждения (сервер в режиме AURA_MAIL_DRIVER=dev)
    QString devResetCode_;   // код сброса пароля (dev)
    QVariantList sessions_;
    // 2FA (TOTP)
    bool twoFactorRequired_ = false;  // текущий вход требует код 2FA
    bool twoFactorEnabled_ = false;
    bool twoFactorPending_ = false;   // настройка начата, но не подтверждена
    QString twoFactorSecret_;         // base32-секрет из setup2fa
    QString twoFactorUri_;            // otpauth:// URI из setup2fa
    QVariantList recoveryCodes_;      // резервные коды из confirm2fa (показ один раз)
    int recoveryCodesLeft_ = 0;
    QVariantList trustedDevices_;
    // Данные входа, ожидающие кода 2FA (для login2fa).
    QString pendingLoginEmail_;
    QString pendingLoginPassword_;
    QString deviceId_;                // стабильный идентификатор устройства (QSettings)
    // Голосовой ввод (STT)
    QString voiceState_ = QStringLiteral("idle");  // idle|listening|processing|success|error
    QString transcript_;              // распознанный текст (редактируется перед отправкой)
    bool micAvailable_ = true;        // есть ли устройство ввода (иначе — текстовый фолбэк)
    qreal voiceLevel_ = 0.0;          // 0.0…1.0 для индикатора/волны
    bool autoSendVoice_ = false;      // автоотправка после распознавания
    QString micLanguage_ = QStringLiteral("auto");  // ru | en | auto
    QAudioSource* audioSource_ = nullptr;
    QIODevice* audioInput_ = nullptr;
    QByteArray recordedPcm_;          // накопленный PCM 16-bit mono 16 kHz
    // Озвучка (TTS)
    QTextToSpeech* tts_ = nullptr;
    bool ttsEnabled_ = false;
    QString ttsVoice_;                // имя выбранного голоса (пусто = системный)
    qreal ttsRate_ = 0.0;             // -1.0…1.0
    qreal ttsVolume_ = 1.0;           // 0.0…1.0
    bool ttsImportantOnly_ = false;   // озвучивать только важные ответы
    bool speaking_ = false;
    QStringList ttsVoices_;           // доступные голоса платформы
    // Этап 8: разрешения, подтверждения, задачи
    QVariantList toolPermissions_;    // каталог инструментов с эффективным mode
    QVariantList confirmations_;      // отложенные действия (status=pending)
    QVariantList tasks_;              // задачи пользователя
    // Этап 9: интеграции с внешними сервисами
    QVariantList integrationProviders_;  // каталог провайдеров с сервера
    QVariantList integrations_;          // активные подключения пользователя
    QString integrationUrl_;             // последняя ссылка на consent-экран
    QString pendingProvider_;            // провайдер текущего OAuth-потока
    QTcpServer* redirectServer_ = nullptr;  // 127.0.0.1:<порт>/callback
    // Этап 13: уведомления
    QVariantList notifications_;         // «входящая»: свежие первыми
    int unreadNotifications_ = 0;        // счётчик непрочитанных (бейдж)
    QVariantList pushDevices_;           // зарегистрированные push-устройства
    // v3: Home-дашборд и A2A-переговоры
    QString homeReply_;                  // последний ответ Ауры с Home (chat_id=0)
    QVariantMap negotiation_;            // последний результат agent.negotiate
    QVariantList auraDialog_;            // «Моя Аура»: реплики и ответы за сессию
    QVariantList negotiationHistory_;    // раунды agent.negotiate, свежие первыми
    bool bookingBusy_ = false;
    QVariantList userResults_;           // результаты users.search
    qint64 dialogSeq_ = 0;
    void appendDialog(QVariantMap entry);
    QString toolMode(const QString& tool) const;
};

}  // namespace aura
