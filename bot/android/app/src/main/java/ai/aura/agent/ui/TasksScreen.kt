package ai.aura.agent.ui

import ai.aura.agent.AppViewModel
import ai.aura.agent.core.NegotiationDto
import ai.aura.agent.core.TaskDto
import ai.aura.agent.ui.theme.Accent
import ai.aura.agent.ui.theme.AccentSoft
import ai.aura.agent.ui.theme.AuraAvatar
import ai.aura.agent.ui.theme.AuraChip
import ai.aura.agent.ui.theme.AuraField
import ai.aura.agent.ui.theme.BgElevated
import ai.aura.agent.ui.theme.Danger
import ai.aura.agent.ui.theme.Dot
import ai.aura.agent.ui.theme.GlassButton
import ai.aura.agent.ui.theme.GlassCard
import ai.aura.agent.ui.theme.GradientButton
import ai.aura.agent.ui.theme.Lilac
import ai.aura.agent.ui.theme.Muted
import ai.aura.agent.ui.theme.RoundIconButton
import ai.aura.agent.ui.theme.ScreenHeader
import ai.aura.agent.ui.theme.Success
import ai.aura.agent.ui.theme.TextMuted
import ai.aura.agent.ui.theme.TextPrimary
import ai.aura.agent.ui.theme.TextSecondary
import ai.aura.agent.ui.theme.Warning
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
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Close
import androidx.compose.material.icons.outlined.Handshake
import androidx.compose.material.icons.outlined.Refresh
import androidx.compose.material.icons.outlined.Snooze
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import java.time.Duration
import java.time.OffsetDateTime

private val STATUS = mapOf(
    "negotiating" to ("Агенты договариваются…" to AccentSoft),
    "agreed" to ("Договорились — нужно ваше «да»" to Warning),
    "confirmed" to ("Встреча подтверждена" to Success),
    "escalated" to ("Договоритесь напрямую" to Danger),
    "declined" to ("Отклонена" to TextMuted),
)

/** Задачи (макет Tasks из android/app): строки-стекло с цветом срочности и блок «Встречи». */
@Composable
fun TasksScreen(vm: AppViewModel) {
    LaunchedEffect(Unit) { vm.reloadTasks() }
    var cancel by remember { mutableStateOf<TaskDto?>(null) }
    var meeting by remember { mutableStateOf(false) }
    val now = OffsetDateTime.now()
    val sent = vm.tasks.filter { it.status == "sent" }
    val upcoming = vm.tasks.filter { it.status == "pending" }
    val next = upcoming.firstOrNull()

    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 20.dp, vertical = 16.dp),
        verticalArrangement = Arrangement.spacedBy(20.dp),
    ) {
        ScreenHeader("Задачи") { RoundIconButton(Icons.Outlined.Refresh, "Обновить", { vm.reloadTasks() }, size = 40.dp) }

        // Подсказка Ауры: до ближайшего напоминания
        GlassCard(Modifier.fillMaxWidth()) {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text("Подсказки Ауры", style = MaterialTheme.typography.titleLarge, color = TextPrimary)
                Text(next?.let { "«${it.text}» — ${it.dueHuman}" } ?: "Впереди ничего не запланировано. " +
                    "Скажите в чате: «напомни в пятницу в 18:00 купить подарок».",
                    style = MaterialTheme.typography.titleSmall, color = TextPrimary)
                next?.let {
                    val minutes = minutesUntil(it, now)
                    Box(Modifier.fillMaxWidth().height(6.dp).clip(RoundedCornerShape(3.dp)).background(Color.White.copy(alpha = 0.08f))) {
                        Box(Modifier.fillMaxWidth((1f - minutes / 240f).coerceIn(0.05f, 1f)).height(6.dp).clip(RoundedCornerShape(3.dp))
                            .background(Brush.horizontalGradient(listOf(ai.aura.agent.ui.theme.GradStart, ai.aura.agent.ui.theme.GradEnd))))
                    }
                    Muted("Время до следующего напоминания")
                }
            }
        }

        if (sent.isNotEmpty()) Section("Напомнено") { sent.forEach { TaskRow(it, now, vm) { cancel = it } } }
        Section("Дальше") {
            if (upcoming.isEmpty()) Muted("Пока ничего")
            upcoming.forEach { TaskRow(it, now, vm) { cancel = it } }
        }

        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Встречи", style = MaterialTheme.typography.headlineSmall, color = TextPrimary, modifier = Modifier.weight(1f))
            GradientButton("Договориться", { meeting = true }, icon = Icons.Outlined.Handshake, height = 40.dp)
        }
        if (vm.negotiations.isEmpty()) {
            Muted("Аура сама согласует время с Аурой собеседника — до 5 раундов, а итог подтверждаете вы.")
        }
        vm.negotiations.forEach { MeetingCard(it, vm) }
        Spacer(Modifier.height(8.dp))
    }

    cancel?.let { t ->
        ConfirmDialog("Удалить задачу?", "«${t.text}» — напоминание не придёт.", "Удалить",
            onConfirm = { vm.taskCancel(t.id) }, onDismiss = { cancel = null })
    }
    if (meeting) MeetingDialog(vm) { meeting = false }
}

private fun minutesUntil(t: TaskDto, now: OffsetDateTime): Long =
    runCatching { Duration.between(now, OffsetDateTime.parse(t.dueAt)).toMinutes() }.getOrDefault(Long.MAX_VALUE)

@Composable
private fun Section(title: String, content: @Composable () -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(title, style = MaterialTheme.typography.headlineSmall, color = TextPrimary)
        content()
    }
}

/** Строка задачи: чекбокс «готово», цвет срочности, «+1 час» и удаление. */
@Composable
private fun TaskRow(task: TaskDto, now: OffsetDateTime, vm: AppViewModel, onCancel: () -> Unit) {
    val minutes = minutesUntil(task, now)
    val tone = when {
        task.status == "sent" || minutes < 0 -> Danger
        minutes < 120 -> Warning
        else -> AccentSoft
    }
    val shape = RoundedCornerShape(16.dp)
    Row(
        Modifier.fillMaxWidth().clip(shape)
            .background(Brush.horizontalGradient(listOf(Color.White.copy(alpha = 0.09f), Accent.copy(alpha = 0.08f), Color.White.copy(alpha = 0.03f))))
            .border(1.dp, Color.White.copy(alpha = 0.16f), shape).padding(start = 16.dp, end = 6.dp, top = 10.dp, bottom = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (task.repeat == "none") {
            Box(
                Modifier.size(22.dp).clip(RoundedCornerShape(7.dp))
                    .border(1.5.dp, Color.White.copy(alpha = 0.3f), RoundedCornerShape(7.dp))
                    .clickable { vm.taskDone(task.id) },
            )
        } else {
            Dot(AccentSoft, 10.dp)
        }
        Column(Modifier.weight(1f).padding(horizontal = 12.dp)) {
            Text(task.text, style = MaterialTheme.typography.titleSmall, color = TextPrimary, maxLines = 2, overflow = TextOverflow.Ellipsis)
            Row(verticalAlignment = Alignment.CenterVertically) {
                Dot(tone, 6.dp)
                Text("  ${task.dueHuman}", style = MaterialTheme.typography.bodySmall, color = TextSecondary)
            }
        }
        if (task.repeat == "none") {
            IconAction(Icons.Outlined.Snooze, "Через час") { vm.taskSnooze(task.id) }
        }
        IconAction(Icons.Outlined.Close, "Удалить", onCancel)
    }
}

@Composable
private fun IconAction(icon: androidx.compose.ui.graphics.vector.ImageVector, label: String, onClick: () -> Unit) {
    Box(Modifier.size(38.dp).clip(RoundedCornerShape(50)).clickable(onClick = onClick), contentAlignment = Alignment.Center) {
        Icon(icon, contentDescription = label, tint = TextSecondary, modifier = Modifier.size(20.dp))
    }
}

@Composable
private fun MeetingCard(n: NegotiationDto, vm: AppViewModel) {
    val (label, color) = STATUS[n.status] ?: (n.status to AccentSoft)
    GlassCard(Modifier.fillMaxWidth(), glow = n.needsMyConfirmation, contentPadding = androidx.compose.foundation.layout.PaddingValues(18.dp)) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                AuraAvatar(n.withName, size = 40.dp)
                Column(Modifier.weight(1f).padding(start = 12.dp)) {
                    Text(n.topic, style = MaterialTheme.typography.titleSmall, color = TextPrimary)
                    Text("с ${n.withName} · ${n.durationMin} мин", style = MaterialTheme.typography.bodySmall, color = TextSecondary)
                }
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Dot(color, 8.dp)
                Text("  $label", style = MaterialTheme.typography.labelMedium, color = color)
            }
            n.slotHuman?.let { Text(it, style = MaterialTheme.typography.titleMedium, color = Lilac) }
            if (n.needsMyConfirmation) {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    AuraChip("Подтвердить", active = true) { vm.confirmMeeting(n.id, true) }
                    AuraChip("Отклонить") { vm.confirmMeeting(n.id, false) }
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
        containerColor = BgElevated,
        title = { Text("Договориться о встрече", style = MaterialTheme.typography.titleMedium, color = TextPrimary) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                AuraField(who, { who = it }, "@username или email", label = "С кем")
                AuraField(topic, { topic = it }, "обсудить проект", label = "Тема")
                AuraField(minutes, { minutes = it.filter(Char::isDigit).take(3) }, "60", label = "Минут",
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number))
                Muted("Другая Аура увидит только, свободны ли вы в предложенное время.")
            }
        },
        confirmButton = {
            GlassButton("Начать", {
                vm.meet(who, topic.ifBlank { "встреча" }, (minutes.toIntOrNull() ?: 60).coerceIn(15, 240))
                onDismiss()
            }, enabled = who.isNotBlank())
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Отмена", color = TextSecondary) } },
    )
}
