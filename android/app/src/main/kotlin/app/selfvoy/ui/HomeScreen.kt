package app.selfvoy.ui

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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.Send
import androidx.compose.material.icons.outlined.Description
import androidx.compose.material.icons.outlined.Handshake
import androidx.compose.material.icons.outlined.Mic
import androidx.compose.material.icons.outlined.Notifications
import androidx.compose.material.icons.outlined.Person
import androidx.compose.material.icons.outlined.Slideshow
import androidx.compose.material.icons.outlined.WarningAmber
import androidx.compose.material3.Badge
import androidx.compose.material3.BadgedBox
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
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
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.selfvoy.AppStore
import app.selfvoy.ui.theme.Accent
import app.selfvoy.ui.theme.SelfvoyAvatar
import app.selfvoy.ui.theme.GlassButton
import app.selfvoy.ui.theme.GlassCard
import app.selfvoy.ui.theme.Lilac
import app.selfvoy.ui.theme.Manrope
import app.selfvoy.ui.theme.Muted
import app.selfvoy.ui.theme.RoundIconButton
import app.selfvoy.ui.theme.Success
import app.selfvoy.ui.theme.TextMuted
import app.selfvoy.ui.theme.TextPrimary
import app.selfvoy.ui.theme.TextSecondary
import app.selfvoy.ui.theme.Warning
import java.time.LocalDate

/**
 * Home (макет Home): приветствие, строка «Что нужно сделать?» с микрофоном
 * (agent.ask → «Мой Selfvoy»), быстрые действия, карточка «Selfvoy активен»,
 * ждущие подтверждения, последние переговоры, ближайшие задачи и лента действий.
 */
@Composable
fun HomeScreen() {
    val auth by AppStore.auth.collectAsState()
    val tasks by AppStore.tasks.collectAsState()
    val notices by AppStore.notices.collectAsState()
    val unread by AppStore.unread.collectAsState()
    val confirmations by AppStore.confirmations.collectAsState()
    val negotiation by AppStore.negotiation.collectAsState()
    val name = (auth as? AppStore.AuthState.LoggedIn)?.displayName.orEmpty()
    var command by remember { mutableStateOf("") }

    val open = tasks.filter { it.status == "pending" }.sortedBy { it.whenAt.ifEmpty { "9999" } }
    val doneToday = tasks.count { it.status == "done" && parseInstant(it.completedAt)?.toLocalDate() == LocalDate.now() }

    fun ask(text: String) {
        if (text.isBlank()) return
        AppStore.ask(text)
        command = ""
        AppStore.selectTab(AppStore.Tab.SELFVOY)
    }

    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 20.dp, vertical = 16.dp),
        verticalArrangement = Arrangement.spacedBy(22.dp),
    ) {
        // Шапка: статус системы, подтверждения, входящая, профиль → настройки
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(7.dp).clip(CircleShape).background(Success))
            Text("  Все системы работают", style = MaterialTheme.typography.bodySmall, color = TextSecondary,
                modifier = Modifier.weight(1f))
            HeaderIcon(Icons.Outlined.WarningAmber, "Подтверждения", confirmations.size.toLong(), Warning) {
                AppStore.selectTab(AppStore.Tab.CONFIRMATIONS)
            }
            HeaderIcon(Icons.Outlined.Notifications, "Входящая", unread, Accent) {
                AppStore.selectTab(AppStore.Tab.NOTICES)
            }
            Box(Modifier.padding(start = 6.dp).clickable { AppStore.selectTab(AppStore.Tab.SETTINGS) }) {
                SelfvoyAvatar(name, size = 36.dp, online = true)
            }
        }

        Text("${greeting()},\n${firstName(name)}", style = MaterialTheme.typography.displayLarge, color = TextPrimary)

        // Строка команды
        val shape = RoundedCornerShape(50)
        Row(
            Modifier
                .fillMaxWidth()
                .height(64.dp)
                .clip(shape)
                .background(Brush.verticalGradient(listOf(Color.White.copy(alpha = 0.09f), Color.White.copy(alpha = 0.03f))))
                .border(1.dp, Color.White.copy(alpha = 0.18f), shape)
                .padding(start = 22.dp, end = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            BasicTextField(
                value = command, onValueChange = { command = it },
                singleLine = true,
                textStyle = TextStyle(fontFamily = Manrope, fontSize = 17.sp, color = TextPrimary, fontWeight = FontWeight.Medium),
                cursorBrush = SolidColor(Lilac),
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Send),
                keyboardActions = KeyboardActions(onSend = { ask(command) }),
                modifier = Modifier.weight(1f),
                decorationBox = { inner ->
                    Box {
                        if (command.isEmpty()) Text("Что нужно сделать?", fontFamily = Manrope, fontSize = 17.sp,
                            color = TextPrimary, fontWeight = FontWeight.Medium)
                        inner()
                    }
                },
            )
            RoundIconButton(
                icon = if (command.isBlank()) Icons.Outlined.Mic else Icons.AutoMirrored.Outlined.Send,
                contentDescription = if (command.isBlank()) "Сказать голосом" else "Отправить Selfvoy",
                accent = true, size = 48.dp,
                onClick = { if (command.isBlank()) AppStore.requestVoice() else ask(command) },
            )
        }

        // Быстрые действия 2×2
        val quick = listOf(
            "Назначить\nвстречу" to { AppStore.selectTab(AppStore.Tab.NEGOTIATIONS) },
            "Моё\nрасписание" to { ask("Что у меня сегодня в расписании?") },
            "Написать\nписьмо" to { command = "Напиши письмо " },
            "Создать\nнапоминание" to { command = "Напомни мне " },
        )
        quick.chunked(2).forEach { row ->
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                row.forEach { (label, action) ->
                    GlassCard(
                        modifier = Modifier.weight(1f).height(66.dp), radius = 16.dp,
                        contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 14.dp, vertical = 10.dp),
                        onClick = action,
                    ) {
                        Text(label, style = MaterialTheme.typography.labelMedium, color = TextPrimary,
                            modifier = Modifier.align(Alignment.CenterStart))
                    }
                }
            }
        }

        // «Ваш Selfvoy активен»
        GlassCard(Modifier.fillMaxWidth(), radius = 26.dp, glow = true, tint = Color(0xFFB4AAFF), fill = 0.2f) {
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text("Ваш Selfvoy активен", style = MaterialTheme.typography.headlineSmall, color = TextPrimary,
                    fontWeight = FontWeight.SemiBold)
                Text("$doneToday ${plural(doneToday.toLong(), "задача выполнена", "задачи выполнены", "задач выполнено")} сегодня",
                    style = MaterialTheme.typography.bodySmall, color = TextSecondary)
                Row(Modifier.padding(top = 10.dp), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    RoundIconButton(Icons.Outlined.Person, "Контакты", { AppStore.selectTab(AppStore.Tab.CONTACTS) }, size = 38.dp)
                    RoundIconButton(Icons.AutoMirrored.Outlined.Send, "Мой Selfvoy", { AppStore.selectTab(AppStore.Tab.SELFVOY) }, size = 38.dp)
                }
            }
        }

        if (confirmations.isNotEmpty()) {
            GlassButton("Ждут вашего подтверждения: ${confirmations.size}", { AppStore.selectTab(AppStore.Tab.CONFIRMATIONS) },
                Modifier.fillMaxWidth(), icon = Icons.Outlined.WarningAmber, height = 52.dp)
        }
        negotiation?.let { n ->
            val text = when {
                !n.accepted -> "Переговоры: нужен ваш выбор"
                n.booking != null -> "Забронировано: ${n.place?.name}, ${dayPhrase(n.slot?.start.orEmpty())} ${timeOf(n.slot?.start.orEmpty())}"
                else -> "Ждёт решения: ${n.place?.name}, ${dayPhrase(n.slot?.start.orEmpty())} ${timeOf(n.slot?.start.orEmpty())}"
            }
            GlassButton(text, { AppStore.selectTab(AppStore.Tab.NEGOTIATIONS) }, Modifier.fillMaxWidth(),
                icon = Icons.Outlined.Handshake, height = 52.dp)
        }

        // Ближайшие задачи
        Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Ближайшие задачи", style = MaterialTheme.typography.titleMedium, color = TextPrimary,
                    modifier = Modifier.weight(1f))
                Text("Все задачи", style = MaterialTheme.typography.bodySmall, color = TextSecondary,
                    modifier = Modifier.clickable { AppStore.selectTab(AppStore.Tab.TASKS) })
            }
            if (open.isEmpty()) Muted("Задач нет — можно попросить Selfvoy что-нибудь запланировать.")
            open.take(4).forEachIndexed { index, task ->
                Row(
                    Modifier.fillMaxWidth().clickable { AppStore.selectTab(AppStore.Tab.TASKS) }.padding(vertical = 9.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Box(Modifier.size(34.dp).clip(CircleShape).border(1.dp, Color.White.copy(alpha = 0.18f), CircleShape),
                        contentAlignment = Alignment.Center) {
                        Icon(listOf(Icons.Outlined.Person, Icons.Outlined.Description, Icons.Outlined.Slideshow)[index % 3],
                            contentDescription = null, tint = TextSecondary, modifier = Modifier.size(16.dp))
                    }
                    Text(task.title, style = MaterialTheme.typography.titleSmall, color = TextPrimary, maxLines = 1,
                        overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f).padding(horizontal = 14.dp))
                    val at = task.whenAt
                    if (at.isNotEmpty()) {
                        val today = parseInstant(at)?.toLocalDate() == LocalDate.now()
                        Muted(if (today) timeOf(at) else "${dayPhrase(at)}, ${timeOf(at)}")
                    }
                }
            }
        }

        // Недавние действия Selfvoy
        Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text("Недавние действия Selfvoy", style = MaterialTheme.typography.titleMedium, color = TextPrimary)
            if (notices.isEmpty()) Muted("Здесь появятся напоминания, переговоры и подтверждения.")
            notices.take(6).forEach { notice ->
                val detail = if (notice.kind == "login.new") "" else notice.body
                Text(
                    buildAnnotatedString {
                        withStyle(SpanStyle(color = TextMuted)) { append(timeOf(notice.createdAt) + "  ") }
                        append(notice.title)
                        if (detail.isNotEmpty()) {
                            append(": ")
                            withStyle(SpanStyle(color = TextPrimary, fontWeight = FontWeight.Bold)) { append(detail) }
                        }
                    },
                    style = MaterialTheme.typography.bodySmall, color = TextSecondary,
                )
            }
        }
        Spacer(Modifier.height(8.dp))
    }
}

@Composable
private fun HeaderIcon(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    label: String,
    count: Long,
    badgeColor: Color,
    onClick: () -> Unit,
) {
    Box(Modifier.size(40.dp).clip(CircleShape).clickable(onClick = onClick), contentAlignment = Alignment.Center) {
        BadgedBox(badge = { if (count > 0) Badge(containerColor = badgeColor) { Text("$count", color = Color.Black) } }) {
            Icon(icon, contentDescription = label, tint = TextSecondary, modifier = Modifier.size(22.dp))
        }
    }
}
