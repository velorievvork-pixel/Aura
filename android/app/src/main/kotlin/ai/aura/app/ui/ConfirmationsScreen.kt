package ai.aura.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ArrowBack
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import ai.aura.app.AppStore
import ai.aura.app.ui.theme.Dot
import ai.aura.app.ui.theme.GlassButton
import ai.aura.app.ui.theme.GlassCard
import ai.aura.app.ui.theme.GradientButton
import ai.aura.app.ui.theme.Muted
import ai.aura.app.ui.theme.RoundIconButton
import ai.aura.app.ui.theme.ScreenHeader
import ai.aura.app.ui.theme.TextPrimary
import ai.aura.app.ui.theme.TextSecondary
import ai.aura.app.ui.theme.Warning

/**
 * Подтверждения: опасные действия Ауры (письма, сообщения, бронь) ждут решения
 * человека (барьер подтверждения). Ничего не исполняется без «Подтвердить».
 */
@Composable
fun ConfirmationsScreen() {
    val confirmations by AppStore.confirmations.collectAsState()
    LazyColumn(
        Modifier.fillMaxSize(),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        item { RoundIconButton(Icons.AutoMirrored.Outlined.ArrowBack, "Назад", { AppStore.selectTab(AppStore.Tab.HOME) }, size = 40.dp) }
        item { ScreenHeader("Подтверждения") }
        item {
            Text("Письма, сообщения и брони Аура не выполняет сама: каждое такое действие ждёт здесь вашего решения.",
                style = MaterialTheme.typography.bodyMedium, color = TextSecondary)
        }
        if (confirmations.isEmpty()) item { Muted("Очередь пуста — Аура ничего не ждёт.") }
        items(confirmations, key = { it.id }) { action ->
            GlassCard(Modifier.fillMaxWidth()) {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Dot(Warning, 8.dp)
                        Spacer(Modifier.width(8.dp))
                        Text("Требуется подтверждение", style = MaterialTheme.typography.labelMedium, color = Warning)
                    }
                    Text(action.summary.ifEmpty { action.tool }, style = MaterialTheme.typography.titleSmall, color = TextPrimary)
                    Muted("инструмент: ${toolLabel(action.tool)}")
                    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        GradientButton("Подтвердить", { AppStore.decide(action.id, approve = true) }, Modifier.weight(1f), height = 44.dp)
                        GlassButton("Отклонить", { AppStore.decide(action.id, approve = false) }, Modifier.weight(1f),
                            danger = true, height = 44.dp)
                    }
                }
            }
        }
    }
}

/** Понятные названия инструментов вместо технических имён. */
fun toolLabel(tool: String): String = when (tool) {
    "send_message" -> "Сообщения"
    "create_note" -> "Заметки"
    "create_reminder" -> "Напоминания"
    "send_email" -> "Письма"
    "find_cafe" -> "Подбор мест"
    "book_table" -> "Бронирование"
    "check_calendar" -> "Календарь"
    "suggest_time" -> "Подбор времени"
    else -> tool
}
