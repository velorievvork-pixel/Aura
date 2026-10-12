package ai.aura.agent.ui

import ai.aura.agent.AppViewModel
import ai.aura.agent.Screen
import ai.aura.agent.Tab
import ai.aura.agent.ui.theme.Accent
import ai.aura.agent.ui.theme.AuraBackground
import ai.aura.agent.ui.theme.AuraEmblem
import ai.aura.agent.ui.theme.BgElevated
import ai.aura.agent.ui.theme.GlassButton
import ai.aura.agent.ui.theme.GradientButton
import ai.aura.agent.ui.theme.Muted
import ai.aura.agent.ui.theme.TextMuted
import ai.aura.agent.ui.theme.TextPrimary
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
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.Chat
import androidx.compose.material.icons.outlined.CheckBox
import androidx.compose.material.icons.outlined.Psychology
import androidx.compose.material.icons.outlined.Tune
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Snackbar
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp

private data class TabItem(val tab: Tab, val label: String, val icon: ImageVector)

private val TABS = listOf(
    TabItem(Tab.Chat, "Аура", Icons.AutoMirrored.Outlined.Chat),
    TabItem(Tab.Tasks, "Задачи", Icons.Outlined.CheckBox),
    TabItem(Tab.Memory, "Память", Icons.Outlined.Psychology),
    TabItem(Tab.Settings, "Ещё", Icons.Outlined.Tune),
)

/** Оболочка (как MainScaffold в android/app): фон с орбитами, раздел и парящая стеклянная панель вкладок. */
@Composable
fun AuraApp(vm: AppViewModel) {
    val snackbar = remember { SnackbarHostState() }
    LaunchedEffect(vm.toast) {
        vm.toast?.let {
            snackbar.showSnackbar(it)
            vm.toast = null
        }
    }
    AuraBackground {
        Column(Modifier.fillMaxSize().statusBarsPadding().imePadding()) {
            Box(Modifier.weight(1f).fillMaxWidth()) {
                when (vm.screen) {
                    Screen.Loading -> Centered { AuraEmblem(76.dp) }
                    Screen.Offline -> Centered {
                        Column(
                            Modifier.padding(32.dp),
                            horizontalAlignment = Alignment.CenterHorizontally,
                            verticalArrangement = Arrangement.spacedBy(14.dp),
                        ) {
                            AuraEmblem(64.dp)
                            Text("Нет связи с сервером Aura", style = MaterialTheme.typography.titleLarge, color = TextPrimary)
                            Muted("Проверьте интернет или адрес сервера.", align = TextAlign.Center)
                            GradientButton("Повторить", { vm.start() }, Modifier.fillMaxWidth())
                            GlassButton("Сменить сервер", { vm.logout() }, Modifier.fillMaxWidth(), quiet = true)
                        }
                    }
                    Screen.Login -> LoginScreen(vm)
                    Screen.Onboarding -> OnboardingScreen(vm)
                    Screen.Main -> when (vm.tab) {
                        Tab.Chat -> ChatScreen(vm)
                        Tab.Tasks -> TasksScreen(vm)
                        Tab.Memory -> MemoryScreen(vm)
                        Tab.Settings -> SettingsScreen(vm)
                    }
                }
                SnackbarHost(snackbar, Modifier.align(Alignment.BottomCenter).padding(12.dp)) { data ->
                    Snackbar(data, containerColor = BgElevated, contentColor = TextPrimary, shape = RoundedCornerShape(16.dp))
                }
            }
            if (vm.screen == Screen.Main) BottomBar(vm)
        }
    }
}

/* Панель вкладок — парящая стеклянная капсула с «линзой» активной вкладки (как в android/app и iOS). */
@Composable
private fun BottomBar(vm: AppViewModel) {
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
                .border(1.dp, Brush.verticalGradient(listOf(Color.White.copy(alpha = 0.42f), Color.White.copy(alpha = 0.06f))), shape)
                .padding(6.dp),
            horizontalArrangement = Arrangement.SpaceAround,
        ) {
            TABS.forEach { item ->
                val active = vm.tab == item.tab
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
                        .clickable { vm.tab = item.tab }
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

@Composable
fun Centered(content: @Composable () -> Unit) {
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) { content() }
}
