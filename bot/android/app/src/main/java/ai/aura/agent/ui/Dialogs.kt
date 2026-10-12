package ai.aura.agent.ui

import ai.aura.agent.AppViewModel
import ai.aura.agent.ui.theme.Accent
import ai.aura.agent.ui.theme.AuraField
import ai.aura.agent.ui.theme.BgElevated
import ai.aura.agent.ui.theme.Danger
import ai.aura.agent.ui.theme.Lilac
import ai.aura.agent.ui.theme.TextPrimary
import ai.aura.agent.ui.theme.TextSecondary
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp

/** Выбор пояса: пояс телефона, частые варианты и ручной ввод IANA (сервер проверит). */
@Composable
fun TimezoneDialog(vm: AppViewModel, current: String, onPick: (String) -> Unit, onDismiss: () -> Unit) {
    var custom by remember { mutableStateOf(current) }
    val names = (listOf(vm.deviceTimezone to "Как на телефоне: ${vm.deviceTimezone}") +
        vm.options.timezones.map { it.name to it.title }).distinctBy { it.first }
    AlertDialog(
        onDismissRequest = onDismiss,
        containerColor = BgElevated,
        title = { Text("Часовой пояс", style = MaterialTheme.typography.titleMedium, color = TextPrimary) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                LazyColumn(Modifier.heightIn(max = 300.dp)) {
                    items(names) { (name, title) ->
                        val selected = name == current
                        Text(
                            title,
                            style = MaterialTheme.typography.bodyMedium,
                            color = if (selected) TextPrimary else TextSecondary,
                            modifier = Modifier
                                .fillMaxWidth()
                                .clip(RoundedCornerShape(12.dp))
                                .then(if (selected) Modifier.background(Accent.copy(alpha = 0.22f)) else Modifier)
                                .clickable { onPick(name) }
                                .padding(horizontal = 12.dp, vertical = 11.dp),
                        )
                    }
                }
                AuraField(custom, { custom = it }, "Asia/Almaty", label = "Другой (IANA)")
            }
        },
        confirmButton = { TextButton(onClick = { onPick(custom.trim()) }) { Text("Выбрать", color = Lilac) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Отмена", color = TextSecondary) } },
    )
}

/** Подтверждение опасного действия — без него запрос не уходит. */
@Composable
fun ConfirmDialog(title: String, text: String, confirm: String, onConfirm: () -> Unit, onDismiss: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        containerColor = BgElevated,
        title = { Text(title, style = MaterialTheme.typography.titleMedium, color = TextPrimary) },
        text = { Text(text, style = MaterialTheme.typography.bodyMedium, color = TextSecondary) },
        confirmButton = { TextButton(onClick = { onConfirm(); onDismiss() }) { Text(confirm, color = Danger) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Отмена", color = TextSecondary) } },
    )
}
