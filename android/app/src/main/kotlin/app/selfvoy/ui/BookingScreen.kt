package app.selfvoy.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ArrowBack
import androidx.compose.material.icons.outlined.Add
import androidx.compose.material.icons.outlined.ContentCopy
import androidx.compose.material.icons.outlined.Delete
import androidx.compose.material.icons.outlined.Edit
import androidx.compose.material.icons.outlined.Pause
import androidx.compose.material.icons.outlined.PlayArrow
import androidx.compose.material.icons.outlined.Share
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import app.selfvoy.AppStore
import app.selfvoy.ui.theme.SelfvoyChip
import app.selfvoy.ui.theme.SelfvoyField
import app.selfvoy.ui.theme.Danger
import app.selfvoy.ui.theme.GlassButton
import app.selfvoy.ui.theme.GlassCard
import app.selfvoy.ui.theme.GradientButton
import app.selfvoy.ui.theme.Muted
import app.selfvoy.ui.theme.RoundIconButton
import app.selfvoy.ui.theme.ScreenHeader
import app.selfvoy.ui.theme.SectionTitle
import app.selfvoy.ui.theme.TextPrimary
import app.selfvoy.ui.theme.TextSecondary
import app.selfvoy.kit.Booking
import app.selfvoy.kit.BookingPage
import app.selfvoy.kit.BookingPageFields
import kotlinx.coroutines.launch

/**
 * «Запись» — страницы записи владельца (booking.*): постоянная ссылка #/b/<slug>,
 * по которой люди без Selfvoy сами выбирают свободное время (страница гостя — в
 * веб-клиенте). Здесь — создать/изменить страницу, поставить на паузу, удалить,
 * поделиться ссылкой и увидеть записи гостей с отменой.
 */
@Composable
fun BookingScreen() {
    val pages by AppStore.bookingPages.collectAsState()
    val bookings by AppStore.bookings.collectAsState()
    var editing by remember { mutableStateOf<BookingPage?>(null) }
    var creating by remember { mutableStateOf(false) }

    LazyColumn(
        Modifier.fillMaxSize(),
        contentPadding = PaddingValues(20.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        item { RoundIconButton(Icons.AutoMirrored.Outlined.ArrowBack, "Назад", { AppStore.selectTab(AppStore.Tab.HOME) }, size = 40.dp) }
        item {
            ScreenHeader("Запись") {
                if (!creating && editing == null) {
                    GlassButton("Новая", { creating = true }, icon = Icons.Outlined.Add, height = 40.dp)
                }
            }
        }
        item { Muted("Постоянная ссылка: люди без Selfvoy сами выбирают свободное время в вашем расписании — можно сразу серией раз в неделю.") }

        if (creating || editing != null) {
            item(key = "form-${editing?.id ?: 0}") {
                PageForm(editing) { creating = false; editing = null }
            }
        }

        item { SectionTitle("Страницы") }
        val list = pages
        when {
            list == null -> item { Muted("Загружаем…") }
            list.isEmpty() && !creating -> item { Muted("Создайте первую страницу — например, «Консультация, 30 минут» — и отправьте ссылку клиентам.") }
            else -> items(list, key = { "page-${it.id}" }) { page -> PageCard(page, onEdit = { editing = page; creating = false }) }
        }

        item { SectionTitle("Записи гостей") }
        if (bookings.isEmpty()) item { Muted("Здесь появятся люди, которые записались по вашим ссылкам.") }
        items(bookings, key = { "booking-${it.id}" }) { BookingRow(it) }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun PageForm(page: BookingPage?, onClose: () -> Unit) {
    val scope = rememberCoroutineScope()
    val start = page?.fields ?: BookingPageFields("")
    var title by remember { mutableStateOf(start.title) }
    var description by remember { mutableStateOf(start.description) }
    var duration by remember { mutableStateOf(start.durationMinutes) }
    var days by remember { mutableStateOf(start.daysAhead) }
    var perDay by remember { mutableStateOf(start.maxPerDay.toString()) }
    var notice by remember { mutableStateOf(start.noticeHours.toString()) }
    var series by remember { mutableStateOf(start.maxSeries.toString()) }
    var weekends by remember { mutableStateOf(start.weekends) }
    var error by remember { mutableStateOf<String?>(null) }
    var busy by remember { mutableStateOf(false) }

    GlassCard(Modifier.fillMaxWidth(), contentPadding = PaddingValues(16.dp)) {
        Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(if (page == null) "Новая страница записи" else "Изменить страницу", style = MaterialTheme.typography.titleMedium, color = TextPrimary)
            SelfvoyField(title, { title = it; error = null }, "Например, консультация", label = "Название")
            SelfvoyField(description, { description = it; error = null }, "Что будет на встрече, где и как", label = "Описание для гостей", maxLines = 4)
            Text("Длительность", style = MaterialTheme.typography.labelMedium, color = TextSecondary)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                listOf(15 to "15 мин", 30 to "30 мин", 45 to "45 мин", 60 to "1 час", 90 to "1,5 часа").forEach { (value, label) ->
                    SelfvoyChip(label, active = duration == value) { duration = value }
                }
            }
            Text("Запись открыта на", style = MaterialTheme.typography.labelMedium, color = TextSecondary)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                listOf(7 to "Неделя", 14 to "2 недели", 30 to "Месяц", 60 to "2 месяца").forEach { (value, label) ->
                    SelfvoyChip(label, active = days == value) { days = value }
                }
            }
            val number = KeyboardOptions(keyboardType = KeyboardType.Number)
            SelfvoyField(perDay, { perDay = it.filter(Char::isDigit).take(2); error = null }, "4", label = "Встреч в день, не больше", keyboardOptions = number)
            SelfvoyField(notice, { notice = it.filter(Char::isDigit).take(3); error = null }, "2", label = "Запас до встречи, часов", keyboardOptions = number)
            SelfvoyField(series, { series = it.filter(Char::isDigit).take(2); error = null }, "1", label = "Серия: до скольких встреч раз в неделю", keyboardOptions = number)
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text("Выходные", style = MaterialTheme.typography.titleSmall, color = TextPrimary)
                    Muted(if (weekends) "Будни и выходные" else "Только будни")
                }
                Switch(checked = weekends, onCheckedChange = { weekends = it })
            }
            error?.let { Text(it, style = MaterialTheme.typography.bodySmall, color = Danger) }
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                GradientButton(
                    if (page == null) "Создать" else "Сохранить",
                    {
                        val fields = BookingPageFields(
                            title = title, description = description, durationMinutes = duration, daysAhead = days,
                            maxPerDay = perDay.toIntOrNull() ?: 0, noticeHours = notice.toIntOrNull() ?: -1,
                            maxSeries = series.toIntOrNull() ?: 0, weekends = weekends,
                        )
                        error = fields.validate()
                        if (error == null) {
                            busy = true
                            scope.launch {
                                val saved = AppStore.saveBookingPage(page?.id, fields)
                                busy = false
                                if (saved) onClose()
                            }
                        }
                    },
                    Modifier.weight(1f), height = 44.dp, busy = busy, enabled = !busy,
                )
                GlassButton("Отмена", onClose, Modifier.weight(1f), quiet = true, height = 44.dp)
            }
        }
    }
}

@Composable
private fun PageCard(page: BookingPage, onEdit: () -> Unit) {
    val context = LocalContext.current
    var confirmDelete by remember { mutableStateOf(false) }
    val facts = listOfNotNull(
        "${page.durationMinutes} мин",
        "на ${page.daysAhead} ${plural(page.daysAhead.toLong(), "день", "дня", "дней")} вперёд",
        "до ${page.maxPerDay} в день",
        if (page.maxSeries > 1) "серия до ${page.maxSeries}" else null,
        if (page.weekends) "и в выходные" else "только будни",
    )
    GlassCard(Modifier.fillMaxWidth(), contentPadding = PaddingValues(16.dp)) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(page.title, style = MaterialTheme.typography.titleMedium, color = TextPrimary, modifier = Modifier.weight(1f))
                Text(if (page.status == "active") "Открыта" else "На паузе", style = MaterialTheme.typography.labelMedium,
                    color = if (page.status == "active") TextSecondary else Danger)
            }
            Muted(facts.joinToString(" · "))
            if (page.description.isNotBlank()) Muted(page.description)
            Text(page.link, style = MaterialTheme.typography.bodySmall, color = TextPrimary)
            if (page.url.isEmpty()) Muted("Сервер не знает свой публичный адрес (SELFVOY_PUBLIC_URL) — полную ссылку соберите из адреса веб-клиента.")
            Muted(if (page.upcoming > 0) "Предстоит: ${page.upcoming} ${plural(page.upcoming.toLong(), "встреча", "встречи", "встреч")}" else "Пока никто не записался")
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                GlassButton("Копировать", { copyText(context, page.link) }, Modifier.weight(1f), icon = Icons.Outlined.ContentCopy, height = 40.dp)
                GlassButton("Поделиться", { shareText(context, page.title, page.link) }, Modifier.weight(1f), icon = Icons.Outlined.Share, height = 40.dp)
            }
            if (confirmDelete) {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                    Muted("Удалить страницу?", Modifier.weight(1f))
                    GlassButton("Удалить", { AppStore.deleteBookingPage(page); confirmDelete = false }, danger = true, height = 40.dp)
                    GlassButton("Нет", { confirmDelete = false }, quiet = true, height = 40.dp)
                }
            } else {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    GlassButton("Изменить", onEdit, Modifier.weight(1f), icon = Icons.Outlined.Edit, quiet = true, height = 40.dp)
                    if (page.status == "active") {
                        GlassButton("Пауза", { AppStore.setBookingPagePaused(page, true) }, Modifier.weight(1f),
                            icon = Icons.Outlined.Pause, quiet = true, height = 40.dp)
                    } else {
                        GlassButton("Открыть", { AppStore.setBookingPagePaused(page, false) }, Modifier.weight(1f),
                            icon = Icons.Outlined.PlayArrow, quiet = true, height = 40.dp)
                    }
                    GlassButton("", { confirmDelete = true }, icon = Icons.Outlined.Delete, quiet = true, height = 40.dp)
                }
            }
        }
    }
}

@Composable
private fun BookingRow(booking: Booking) {
    var confirming by remember { mutableStateOf(false) }
    val upcoming = booking.occurrences.filter { it.active && (parseInstant(it.start)?.isAfter(java.time.LocalDateTime.now()) ?: false) }
    val shown = (upcoming.ifEmpty { booking.occurrences }).take(3)
    val status = when {
        booking.status == "cancelled" -> if (booking.cancelledBy == "owner") "Отменили вы" else "Отменил гость"
        upcoming.isNotEmpty() -> "Предстоит"
        else -> "Прошла"
    }
    GlassCard(Modifier.fillMaxWidth(), contentPadding = PaddingValues(16.dp)) {
        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("${booking.guestName} · ${booking.pageTitle}", style = MaterialTheme.typography.titleSmall,
                    color = TextPrimary, modifier = Modifier.weight(1f))
                Text(status, style = MaterialTheme.typography.labelMedium, color = if (booking.status == "cancelled") Danger else TextSecondary)
            }
            Muted(shown.joinToString(" · ") { "${dayPhrase(it.start)}, ${timeOf(it.start)}" } +
                if (booking.reschedules > 0) " · переносов: ${booking.reschedules}" else "")
            if (booking.guestEmail.isNotEmpty()) Muted(booking.guestEmail)
            if (booking.guestComment.isNotEmpty()) Muted("«${booking.guestComment}»")
            if (status == "Предстоит") {
                if (confirming) {
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                        Muted("Отменить все встречи?", Modifier.weight(1f))
                        GlassButton("Да", { AppStore.cancelBooking(booking); confirming = false }, danger = true, height = 38.dp)
                        GlassButton("Нет", { confirming = false }, quiet = true, height = 38.dp)
                    }
                } else {
                    GlassButton("Отменить запись", { confirming = true }, Modifier.padding(top = 4.dp), quiet = true, height = 38.dp)
                }
            }
        }
    }
}

internal fun copyText(context: Context, text: String) {
    val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
    clipboard.setPrimaryClip(ClipData.newPlainText("Selfvoy", text))
}

internal fun shareText(context: Context, title: String, text: String) {
    val intent = Intent(Intent.ACTION_SEND).apply {
        type = "text/plain"
        putExtra(Intent.EXTRA_SUBJECT, title)
        putExtra(Intent.EXTRA_TEXT, text)
    }
    context.startActivity(Intent.createChooser(intent, title).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
}
