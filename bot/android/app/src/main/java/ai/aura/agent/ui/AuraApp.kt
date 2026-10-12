package ai.aura.agent.ui

import ai.aura.agent.AppViewModel
import ai.aura.agent.Screen
import ai.aura.agent.Tab
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Chat
import androidx.compose.material.icons.filled.Checklist
import androidx.compose.material.icons.filled.Psychology
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp

private data class TabItem(val tab: Tab, val title: String, val icon: ImageVector)

private val TABS = listOf(
    TabItem(Tab.Chat, "Чат", Icons.AutoMirrored.Filled.Chat),
    TabItem(Tab.Tasks, "Задачи", Icons.Filled.Checklist),
    TabItem(Tab.Memory, "Память", Icons.Filled.Psychology),
    TabItem(Tab.Settings, "Ещё", Icons.Filled.Settings),
)

@Composable
fun AuraApp(vm: AppViewModel) {
    val snackbar = remember { SnackbarHostState() }
    LaunchedEffect(vm.toast) {
        vm.toast?.let {
            snackbar.showSnackbar(it)
            vm.toast = null
        }
    }
    Scaffold(
        snackbarHost = { SnackbarHost(snackbar) },
        bottomBar = {
            if (vm.screen == Screen.Main) {
                NavigationBar {
                    TABS.forEach { item ->
                        NavigationBarItem(
                            selected = vm.tab == item.tab,
                            onClick = { vm.tab = item.tab },
                            icon = { Icon(item.icon, contentDescription = item.title) },
                            label = { Text(item.title) },
                        )
                    }
                }
            }
        },
    ) { padding ->
        Box(Modifier.padding(padding).fillMaxSize()) {
            when (vm.screen) {
                Screen.Loading -> Centered { CircularProgressIndicator() }
                Screen.Offline -> Centered {
                    Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(12.dp)) {
                        Text("Нет связи с сервером Aura")
                        Button(onClick = { vm.start() }) { Text("Повторить") }
                        Button(onClick = { vm.logout() }) { Text("Сменить сервер / выйти") }
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
        }
    }
}

@Composable
fun Centered(content: @Composable () -> Unit) {
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) { content() }
}
