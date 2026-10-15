package app.selfvoy.ui

import android.view.ViewTreeObserver
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
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.unit.dp
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import app.selfvoy.AppStore
import app.selfvoy.ui.theme.Accent
import app.selfvoy.ui.theme.SelfvoyBackground
import app.selfvoy.ui.theme.TextMuted
import app.selfvoy.ui.theme.TextPrimary

/**
 * Оболочка после входа (v3): фон с орбитами, текущий раздел и стеклянная панель
 * вкладок снизу. Пять разделов во вкладках; настройки, подтверждения и входящая
 * открываются из шапки Home и закрываются кнопкой «назад».
 */
@Composable
fun MainScaffold() {
    val tab by AppStore.tab.collectAsState()

    // Вложенные разделы и открытый чат закрываются системной кнопкой «назад».
    BackHandler(enabled = tab in setOf(AppStore.Tab.SETTINGS, AppStore.Tab.CONFIRMATIONS, AppStore.Tab.NOTICES, AppStore.Tab.BOOKING)) {
        AppStore.selectTab(AppStore.Tab.HOME)
    }

    SelfvoyBackground {
        Column(Modifier.fillMaxSize().statusBarsPadding()) {
            Box(Modifier.weight(1f).fillMaxWidth()) {
                when (tab) {
                    AppStore.Tab.HOME -> HomeScreen()
                    AppStore.Tab.SELFVOY -> MySelfvoyScreen()
                    AppStore.Tab.NEGOTIATIONS -> NegotiationsScreen()
                    AppStore.Tab.TASKS -> TasksScreen()
                    AppStore.Tab.CONTACTS -> ContactsScreen()
                    AppStore.Tab.SETTINGS -> SettingsScreen()
                    AppStore.Tab.CONFIRMATIONS -> ConfirmationsScreen()
                    AppStore.Tab.NOTICES -> NotificationsScreen()
                    AppStore.Tab.BOOKING -> BookingScreen()
                }
            }
            // Пока открыта клавиатура, панель вкладок не нужна и не должна висеть над ней.
            if (!rememberImeVisible()) BottomBar(tab)
        }
    }
}

private data class TabItem(val tab: AppStore.Tab, val label: String, val icon: ImageVector)

private val tabs = listOf(
    TabItem(AppStore.Tab.HOME, "Главная", Icons.Outlined.Home),
    TabItem(AppStore.Tab.SELFVOY, "Selfvoy", Icons.AutoMirrored.Outlined.Chat),
    TabItem(AppStore.Tab.NEGOTIATIONS, "Переговоры", Icons.Outlined.Handshake),
    TabItem(AppStore.Tab.TASKS, "Задачи", Icons.Outlined.CheckBox),
    TabItem(AppStore.Tab.CONTACTS, "Контакты", Icons.Outlined.People),
)

/*
  Панель вкладок — парящая стеклянная капсула (как в iOS): отступ от краёв,
  под ней фон приложения, а не тёмная полоса во всю ширину. Стекло — заливка
  с бликом сверху и светлый кант; активная вкладка — своя «линза».
*/
@Composable
private fun BottomBar(current: AppStore.Tab) {
    val shape = RoundedCornerShape(percent = 50)
    Box(
        Modifier
            .fillMaxWidth()
            .windowInsetsPadding(WindowInsets.navigationBars)
            .padding(start = 12.dp, end = 12.dp, bottom = 10.dp, top = 4.dp),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clip(shape)
                .background(Color(0xCC161124))
                .background(
                    Brush.verticalGradient(
                        0f to Color.White.copy(alpha = 0.16f),
                        0.5f to Color.White.copy(alpha = 0.04f),
                        1f to Color.White.copy(alpha = 0.08f),
                    ),
                )
                .border(
                    1.dp,
                    Brush.verticalGradient(listOf(Color.White.copy(alpha = 0.42f), Color.White.copy(alpha = 0.06f))),
                    shape,
                )
                .padding(6.dp),
            horizontalArrangement = Arrangement.SpaceAround,
        ) {
            tabs.forEach { item ->
                val active = current == item.tab
                val lens = RoundedCornerShape(percent = 50)
                Column(
                    modifier = Modifier
                        .weight(1f)
                        .clip(lens)
                        .then(
                            if (active) Modifier
                                .background(Accent.copy(alpha = 0.22f))
                                .background(Brush.verticalGradient(listOf(Color.White.copy(alpha = 0.22f), Color.White.copy(alpha = 0.05f))))
                                .border(1.dp, Brush.verticalGradient(listOf(Color.White.copy(alpha = 0.4f), Color.Transparent)), lens)
                            else Modifier,
                        )
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
}

/*
  Видна ли клавиатура — по insets корневого окна. WindowInsets.isImeVisible из
  Compose без edge-to-edge (decorFitsSystemWindows) не получает настоящих insets
  и считал клавиатуру открытой постоянно: панель вкладок пропадала после входа.
*/
@Composable
private fun rememberImeVisible(): Boolean {
    val view = LocalView.current
    var visible by remember { mutableStateOf(false) }
    DisposableEffect(view) {
        val listener = ViewTreeObserver.OnGlobalLayoutListener {
            visible = ViewCompat.getRootWindowInsets(view)?.isVisible(WindowInsetsCompat.Type.ime()) == true
        }
        view.viewTreeObserver.addOnGlobalLayoutListener(listener)
        listener.onGlobalLayout()
        onDispose { view.viewTreeObserver.removeOnGlobalLayoutListener(listener) }
    }
    return visible
}
