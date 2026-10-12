package ai.aura.app.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.Chat
import androidx.compose.material.icons.outlined.CheckBox
import androidx.compose.material.icons.outlined.Handshake
import androidx.compose.material.icons.outlined.Home
import androidx.compose.material.icons.outlined.People
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import ai.aura.app.AppStore
import ai.aura.app.ui.theme.Accent
import ai.aura.app.ui.theme.AuraBackground
import ai.aura.app.ui.theme.TextMuted
import ai.aura.app.ui.theme.TextPrimary

/**
 * Оболочка после входа (v3): фон с орбитами, текущий раздел и стеклянная панель
 * вкладок снизу. Пять разделов во вкладках; настройки, подтверждения и входящая
 * открываются из шапки Home и закрываются кнопкой «назад».
 */
@Composable
fun MainScaffold() {
    val tab by AppStore.tab.collectAsState()

    // Вложенные разделы и открытый чат закрываются системной кнопкой «назад».
    BackHandler(enabled = tab in setOf(AppStore.Tab.SETTINGS, AppStore.Tab.CONFIRMATIONS, AppStore.Tab.NOTICES)) {
        AppStore.selectTab(AppStore.Tab.HOME)
    }

    AuraBackground {
        Column(Modifier.fillMaxSize().statusBarsPadding()) {
            Box(Modifier.weight(1f).fillMaxWidth()) {
                when (tab) {
                    AppStore.Tab.HOME -> HomeScreen()
                    AppStore.Tab.AURA -> MyAuraScreen()
                    AppStore.Tab.NEGOTIATIONS -> NegotiationsScreen()
                    AppStore.Tab.TASKS -> TasksScreen()
                    AppStore.Tab.CONTACTS -> ContactsScreen()
                    AppStore.Tab.SETTINGS -> SettingsScreen()
                    AppStore.Tab.CONFIRMATIONS -> ConfirmationsScreen()
                    AppStore.Tab.NOTICES -> NotificationsScreen()
                }
            }
            BottomBar(tab)
        }
    }
}

private data class TabItem(val tab: AppStore.Tab, val label: String, val icon: ImageVector)

private val tabs = listOf(
    TabItem(AppStore.Tab.HOME, "Главная", Icons.Outlined.Home),
    TabItem(AppStore.Tab.AURA, "Аура", Icons.AutoMirrored.Outlined.Chat),
    TabItem(AppStore.Tab.NEGOTIATIONS, "Переговоры", Icons.Outlined.Handshake),
    TabItem(AppStore.Tab.TASKS, "Задачи", Icons.Outlined.CheckBox),
    TabItem(AppStore.Tab.CONTACTS, "Контакты", Icons.Outlined.People),
)

@Composable
private fun BottomBar(current: AppStore.Tab) {
    val shape = RoundedCornerShape(topStart = 24.dp, topEnd = 24.dp)
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(shape)
            .background(Color(0xFF100D18))
            .border(1.dp, Color.White.copy(alpha = 0.08f), shape)
            .windowInsetsPadding(WindowInsets.navigationBars)
            .padding(horizontal = 6.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.SpaceAround,
    ) {
        tabs.forEach { item ->
            val active = current == item.tab
            Column(
                modifier = Modifier
                    .weight(1f)
                    .clip(RoundedCornerShape(14.dp))
                    .background(if (active) Accent.copy(alpha = 0.18f) else Color.Transparent)
                    .clickable { AppStore.selectTab(item.tab) }
                    .padding(vertical = 7.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Icon(item.icon, contentDescription = item.label,
                    tint = if (active) TextPrimary else TextMuted, modifier = Modifier.size(22.dp))
                Text(item.label, style = MaterialTheme.typography.labelSmall,
                    color = if (active) TextPrimary else TextMuted, maxLines = 1)
            }
        }
    }
}
