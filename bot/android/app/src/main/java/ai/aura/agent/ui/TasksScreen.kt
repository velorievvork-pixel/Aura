package ai.aura.agent.ui

import ai.aura.agent.AppViewModel
import ai.aura.agent.core.NegotiationDto
import ai.aura.agent.core.TaskDto
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Groups
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Snooze
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

private val STATUS = mapOf(
    "negotiating" to "агенты договариваются…", "agreed" to "договорились — ждёт подтверждения",
    "confirmed" to "подтверждена", "escalated" to "нужно договориться самим", "declined" to "отклонена",
)

@Composable
fun TasksScreen(vm: AppViewModel) {
    LaunchedEffect(Unit) { vm.reloadTasks() }
    var cancelId by remember { mutableStateOf<Int?>(null) }
    var meeting by remember { mutableStateOf(false) }
    LazyColumn(
        Modifier.fillMaxSize().padding(horizontal = 12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        item {
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                Text("Задачи", style = MaterialTheme.typography.headlineSmall, modifier = Modifier.weight(1f).padding(top = 12.dp))
                IconButton(onClick = { vm.reloadTasks() }) { Icon(Icons.Filled.Refresh, "Обновить") }
            }
        }
        if (vm.tasks.isEmpty()) {
            item { Text("Задач нет. Скажите в чате: «напомни в пятницу в 18:00 купить подарок».") }
        }
        items(vm.tasks, key = { it.id }) { t -> TaskCard(t, vm, onCancel = { cancelId = t.id }) }
        item {
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(top = 16.dp)) {
                Text("Встречи", style = MaterialTheme.typography.headlineSmall, modifier = Modifier.weight(1f))
                OutlinedButton(onClick = { meeting = true }) {
                    Icon(Icons.Filled.Groups, null)
                    Text(" Договориться")
                }
            }
        }
        if (vm.negotiations.isEmpty()) {
            item { Text("Агент сам согласует время с агентом собеседника (до 5 раундов), а итог подтвердите вы.") }
        }
        items(vm.negotiations, key = { "n${it.id}" }) { n -> MeetingCard(n, vm) }
    }
    cancelId?.let { id ->
        ConfirmDialog("Удалить задачу?", "Напоминание не придёт.", "Удалить",
            onConfirm = { vm.taskCancel(id) }, onDismiss = { cancelId = null })
    }
    if (meeting) MeetingDialog(vm) { meeting = false }
}

@Composable
private fun TaskCard(t: TaskDto, vm: AppViewModel, onCancel: () -> Unit) {
    Card(Modifier.fillMaxWidth()) {
        Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(t.text, style = MaterialTheme.typography.titleMedium)
                Text((if (t.status == "sent") "🔔 " else "⏳ ") + t.dueHuman, style = MaterialTheme.typography.bodySmall)
            }
            if (t.repeat == "none") {
                IconButton(onClick = { vm.taskDone(t.id) }) { Icon(Icons.Filled.Check, "Выполнено") }
                IconButton(onClick = { vm.taskSnooze(t.id) }) { Icon(Icons.Filled.Snooze, "Через час") }
            }
            IconButton(onClick = onCancel) { Icon(Icons.Filled.Delete, "Удалить") }
        }
    }
}

@Composable
private fun MeetingCard(n: NegotiationDto, vm: AppViewModel) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text("«${n.topic}» с ${n.withName}", style = MaterialTheme.typography.titleMedium)
            Text(STATUS[n.status] ?: n.status, style = MaterialTheme.typography.bodySmall)
            n.slotHuman?.let { Text("🗓 $it, ${n.durationMin} мин") }
            if (n.needsMyConfirmation) {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = { vm.confirmMeeting(n.id, true) }) { Text("Подтвердить") }
                    OutlinedButton(onClick = { vm.confirmMeeting(n.id, false) }) { Text("Отклонить") }
                }
            }
        }
    }
}

@Composable
private fun MeetingDialog(vm: AppViewModel, onDismiss: () -> Unit) {
    var who by remember { mutableStateOf("") }
    var topic by remember { mutableStateOf("") }
    var minutes by remember { mutableStateOf("60") }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Договориться о встрече") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(who, { who = it }, label = { Text("@username в Telegram или email") }, singleLine = true)
                OutlinedTextField(topic, { topic = it }, label = { Text("Тема") }, singleLine = true)
                OutlinedTextField(minutes, { minutes = it.filter(Char::isDigit).take(3) }, label = { Text("Минут") }, singleLine = true)
            }
        },
        confirmButton = {
            TextButton(onClick = {
                vm.meet(who, topic.ifBlank { "встреча" }, (minutes.toIntOrNull() ?: 60).coerceIn(15, 240))
                onDismiss()
            }, enabled = who.isNotBlank()) { Text("Начать") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Отмена") } },
    )
}
