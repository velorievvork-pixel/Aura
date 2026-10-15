# Selfvoy для iOS (Swift/SwiftUI)

Нативный клиент Selfvoy: тот же WS-протокол, что у Qt-клиента и e2e
(`{id, type, payload}` ↔ `ok`/`error`, события `type=event`). Общий слой —
Swift-пакет **SelfvoyKit** (транспорт, протокол, модели, Keychain, deep links).

## Сборка

Нужны macOS 14+ и Xcode 15+.

```bash
brew install xcodegen
cd ios
xcodegen generate
open Selfvoy.xcodeproj     # схема Selfvoy, iOS 17+
```

Тесты общего слоя (без симулятора, прямо на Mac):

```bash
cd ios/SelfvoyKit && swift test
```

В Xcode: схема **SelfvoyTests** (смоук UI-слоя) и тесты SelfvoyKit.

## Первый запуск

1. В поле «Сервер» укажите адрес C++-сервера: `ws://IP:9000` (внутри одной
   сети) или `wss://ваш-домен` (прод — TLS терминирует обратный прокси).
2. Зарегистрируйтесь или войдите; при включённой 2FA — подтвердите TOTP.
3. Quick Actions → «Back Tap и Команды» — пошаговая инструкция.

## Возможности

- Чаты, сообщения, «Спросить Selfvoy» (agent.ask), события chat.message.
- Задачи и напоминания (task.due озвучивается), экспорт в напоминания iOS
  (EventKit, разрешение — только по кнопке).
- Барьер подтверждения опасных операций, разрешения инструментов.
- Интеграции Google (OAuth в Safari → возврат по `selfvoy://oauth`).
- Голос: SFSpeechRecognizer on-device, fallback — серверный STT
  (`speech.transcribe`); озвучка — AVSpeechSynthesizer.
- Siri/Команды: «Спросить Selfvoy», «Создать задачу» (App Intents).

## Deep links (схема `selfvoy://`)

| ссылка | действие |
| --- | --- |
| `selfvoy://chats/{id}` | открыть чат |
| `selfvoy://voice` | голосовой запрос (удобно для Back Tap) |
| `selfvoy://ask?text=...` | сразу спросить Selfvoy |
| `selfvoy://tasks` | список задач |
| `selfvoy://settings` | настройки |
| `selfvoy://oauth?provider=&code=&state=` | возврат из Google OAuth |

## Back Tap — только официально

Back Tap — системный жест iOS, сторонним приложениям недоступен. Приложение
его **не имитирует**: жест назначается пользователем в «Универсальный доступ
→ Касание → Касание сзади» на команду «Спросить Selfvoy» (App Intent).
Инструкция — в приложении (Настройки → Quick Actions).
