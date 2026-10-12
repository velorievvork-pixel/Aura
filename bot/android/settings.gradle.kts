// Aura Agent — Android-клиент Python-бэкенда bot/ (тот же агент, что и Telegram-бот).
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "aura-agent-android"
include(":app")
