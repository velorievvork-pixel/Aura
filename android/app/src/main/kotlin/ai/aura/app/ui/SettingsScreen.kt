package ai.aura.app.ui

import android.content.Context
import androidx.browser.customtabs.CustomTabsIntent
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ArrowBack
import androidx.compose.material.icons.automirrored.outlined.Logout
import androidx.compose.material.icons.outlined.CalendarMonth
import androidx.compose.material.icons.outlined.Mail
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import ai.aura.app.AppStore
import ai.aura.app.ui.theme.Accent
import ai.aura.app.ui.theme.AccentSoft
import ai.aura.app.ui.theme.AuraAvatar
import ai.aura.app.ui.theme.AuraField
import ai.aura.app.ui.theme.Danger
import ai.aura.app.ui.theme.GlassButton
import ai.aura.app.ui.theme.GlassCard
import ai.aura.app.ui.theme.GradientButton
import ai.aura.app.ui.theme.Muted
import ai.aura.app.ui.theme.RoundIconButton
import ai.aura.app.ui.theme.ScreenHeader
import ai.aura.app.ui.theme.SectionTitle
import ai.aura.app.ui.theme.TextPrimary
import ai.aura.app.ui.theme.TextSecondary
import ai.aura.app.ui.theme.Warning
import kotlinx.coroutines.launch

/**
 * Настройки: профиль, сервер, что разрешено Ауре (allow/ask/deny по инструментам),
 * 2FA (setup → confirm → резервные коды, отключение по паролю), интеграции Google
 * (Custom Tabs → aura://oauth), выход. Кнопки делают только то, что умеет сервер.
 */
@Composable
fun SettingsScreen() {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val auth by AppStore.auth.collectAsState()
    val permissions by AppStore.permissions.collectAsState()
    val twoFaEnabled by AppStore.twoFaEnabled.collectAsState()
    val twoFaSetup by AppStore.twoFaSetup.collectAsState()
    val recoveryCodes by AppStore.recoveryCodes.collectAsState()
    val connections by AppStore.connections.collectAsState()
    val pushStatus by AppStore.pushStatus.collectAsState()
    var serverUrl by remember { mutableStateOf(AppStore.store.serverUrl) }
    var totpCode by remember { mutableStateOf("") }
    var disablePassword by remember { mutableStateOf("") }
    val me = auth as? AppStore.AuthState.LoggedIn

    LazyColumn(
        Modifier.fillMaxSize(),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(20.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        item { RoundIconButton(Icons.AutoMirrored.Outlined.ArrowBack, "Назад", { AppStore.selectTab(AppStore.Tab.HOME) }, size = 40.dp) }
        item { ScreenHeader("Настройки") }

        item {
            GlassCard(Modifier.fillMaxWidth()) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    AuraAvatar(me?.displayName.orEmpty(), size = 52.dp, online = true)
                    Column(Modifier.weight(1f).padding(start = 14.dp)) {
                        Text(me?.displayName.orEmpty(), style = MaterialTheme.typography.titleLarge, color = TextPrimary)
                        Text(me?.email.orEmpty(), style = MaterialTheme.typography.bodySmall, color = TextSecondary)
                        Muted(pushStatus)
                    }
                }
            }
        }

        item { SectionTitle("Что может ваша Аура") }
        item { Muted("Аура лишь формирует намерение — исполняет сервер. Опасные действия по умолчанию ждут вашего подтверждения.") }
        items(permissions, key = { it.tool }) { row ->
            GlassCard(Modifier.fillMaxWidth(), contentPadding = androidx.compose.foundation.layout.PaddingValues(16.dp)) {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(toolLabel(row.tool), style = MaterialTheme.typography.titleSmall, color = TextPrimary)
                        if (row.dangerous) {
                            Spacer(Modifier.width(8.dp))
                            Box(Modifier.clip(RoundedCornerShape(50)).border(1.dp, Warning.copy(alpha = 0.5f), RoundedCornerShape(50))
                                .padding(horizontal = 8.dp, vertical = 2.dp)) {
                                Text("опасный", style = MaterialTheme.typography.labelSmall, color = Warning)
                            }
                        }
                    }
                    Muted(row.description)
                    Segmented(row.mode) { AppStore.setPermission(row.tool, it) }
                }
            }
        }

        item { SectionTitle("Безопасность") }
        item {
            GlassCard(Modifier.fillMaxWidth()) {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text("Двухфакторная аутентификация", style = MaterialTheme.typography.titleSmall, color = TextPrimary)
                    Text(if (twoFaEnabled) "Включена" else "Выключена", color = if (twoFaEnabled) AccentSoft else TextSecondary,
                        style = MaterialTheme.typography.bodySmall)
                    if (!twoFaEnabled && twoFaSetup == null) {
                        GradientButton("Включить 2FA", { AppStore.setup2fa() }, Modifier.fillMaxWidth(), height = 44.dp)
                    }
                    twoFaSetup?.let { setup ->
                        Muted("Добавьте секрет в приложение-аутентификатор:")
                        Text(setup.otpauthUri.ifEmpty { setup.secret }, color = AccentSoft, style = MaterialTheme.typography.bodySmall)
                        AuraField(totpCode, { totpCode = it }, "Код из приложения")
                        GradientButton("Подтвердить и включить", { AppStore.confirm2fa(totpCode); totpCode = "" },
                            Modifier.fillMaxWidth(), height = 44.dp)
                    }
                    if (recoveryCodes.isNotEmpty()) {
                        Muted("Резервные коды (сохраните, показываются один раз):")
                        recoveryCodes.forEach { Text(it, color = AccentSoft, style = MaterialTheme.typography.labelMedium) }
                    }
                    if (twoFaEnabled) {
                        AuraField(disablePassword, { disablePassword = it }, "Пароль", password = true)
                        GlassButton("Отключить 2FA", { AppStore.disable2fa(disablePassword); disablePassword = "" },
                            Modifier.fillMaxWidth(), danger = true)
                    }
                }
            }
        }

        item { SectionTitle("Интеграции") }
        item {
            GlassCard(Modifier.fillMaxWidth()) {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Muted("Google Календарь и Gmail через официальный OAuth. Токены хранятся только на сервере.")
                    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        GlassButton("Календарь", { openOAuth(context, scope, "google_calendar") }, Modifier.weight(1f),
                            icon = Icons.Outlined.CalendarMonth)
                        GlassButton("Gmail", { openOAuth(context, scope, "google_gmail") }, Modifier.weight(1f),
                            icon = Icons.Outlined.Mail)
                    }
                    if (connections.isEmpty()) Muted("Подключений нет")
                    connections.forEach { connection ->
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text("${connection.provider} · ${connection.account.ifEmpty { connection.status }}",
                                style = MaterialTheme.typography.bodySmall, color = TextPrimary, modifier = Modifier.weight(1f))
                            GlassButton("Синхр.", { AppStore.syncIntegration(connection.id) }, height = 34.dp, quiet = true)
                            GlassButton("Отозвать", { AppStore.revokeIntegration(connection.id) }, height = 34.dp, quiet = true)
                        }
                    }
                }
            }
        }

        item { SectionTitle("Сервер") }
        item {
            GlassCard(Modifier.fillMaxWidth()) {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    AuraField(serverUrl, { serverUrl = it }, "ws:// или wss://")
                    GlassButton("Сохранить и переподключиться", { AppStore.setServerUrl(serverUrl.trim()) }, Modifier.fillMaxWidth())
                }
            }
        }
        item {
            GlassButton("Выйти", { AppStore.logout() }, Modifier.fillMaxWidth(), icon = Icons.AutoMirrored.Outlined.Logout,
                danger = true, height = 50.dp)
        }
    }
}

/** Три режима инструмента: сама / с подтверждения / запрещено. */
@Composable
private fun Segmented(mode: String, onSelect: (String) -> Unit) {
    val shape = RoundedCornerShape(50)
    Row(Modifier.fillMaxWidth().clip(shape).background(Color(0x730A0812)).border(1.dp, Color.White.copy(alpha = 0.14f), shape)
        .padding(3.dp)) {
        listOf("allow" to "Сама", "ask" to "Спрашивать", "deny" to "Запретить").forEach { (value, label) ->
            val active = mode == value
            Box(
                Modifier.weight(1f).clip(shape)
                    .background(if (active) (if (value == "deny") Danger.copy(alpha = 0.3f) else Accent.copy(alpha = 0.35f)) else Color.Transparent)
                    .clickable { if (!active) onSelect(value) }.padding(vertical = 8.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(label, style = MaterialTheme.typography.labelMedium, color = if (active) TextPrimary else TextSecondary,
                    textAlign = TextAlign.Center)
            }
        }
    }
}

/** Google OAuth: begin → Custom Tabs → редирект aura://oauth (MainActivity). */
private fun openOAuth(context: Context, scope: kotlinx.coroutines.CoroutineScope, provider: String) {
    scope.launch {
        val url = AppStore.beginIntegration(provider)
        if (url.isNotEmpty()) CustomTabsIntent.Builder().build().launchUrl(context, android.net.Uri.parse(url))
    }
}
