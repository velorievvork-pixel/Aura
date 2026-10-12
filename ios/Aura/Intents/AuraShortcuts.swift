// AuraShortcuts.swift — фразы Siri (App Shortcuts).
//
// После сборки приложения команды автоматически появляются в «Команды» —
// их можно назначить на Back Tap, кнопку действия или автоматизации.
// В фразах допустимы только параметры AppEntity/AppEnum, поэтому текст
// запроса и название задачи Siri спрашивает отдельно (requestValueDialog).
import AppIntents

struct AuraShortcuts: AppShortcutsProvider {

    static var appShortcuts: [AppShortcut] {
        AppShortcut(
            intent: AskAuraIntent(),
            phrases: [
                "Спросить Ауру в \(.applicationName)",
                "В \(.applicationName) спроси Ауру"
            ],
            shortTitle: "Спросить Ауру",
            systemImageName: "sparkles"
        )
        AppShortcut(
            intent: CreateTaskIntent(),
            phrases: [
                "Создать задачу в \(.applicationName)",
                "В \(.applicationName) новая задача"
            ],
            shortTitle: "Создать задачу",
            systemImageName: "checklist"
        )
    }
}
