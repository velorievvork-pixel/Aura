package ai.aura.agent.ui

import ai.aura.agent.AppViewModel
import android.content.ActivityNotFoundException
import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp

@Composable
fun SettingsScreen(vm: AppViewModel) {
    val context = LocalContext.current
    LaunchedEffect(Unit) { vm.reloadAccount() }
    var code by remember { mutableStateOf("") }
    var pickTz by remember { mutableStateOf(false) }
    var logout by remember { mutableStateOf(false) }
    val user = vm.user
    fun open(url: String) {
        try {
            context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)))
        } catch (e: ActivityNotFoundException) {
            vm.toast = "Нет приложения, чтобы открыть ссылку"
        }
    }
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text(user?.name ?: "", style = MaterialTheme.typography.headlineSmall)
        Text(user?.email ?: "", style = MaterialTheme.typography.bodyMedium)

        // --- подписка ---
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                val b = vm.billing
                Text("💎 Премиум", style = MaterialTheme.typography.titleMedium)
                if (b != null) {
                    Text(if (b.premium) "Активен до ${b.premiumUntil}" else "Бесплатный план: ${b.limit} сообщений в день")
                    Text("Сегодня: ${b.used} из ${b.limit}", style = MaterialTheme.typography.bodySmall)
                    Text("${b.priceRub} ₽/мес · ${b.priceStars} ⭐" + (b.priceTon?.let { " или $it TON" } ?: "") +
                        " — без комиссий магазинов приложений", style = MaterialTheme.typography.bodySmall)
                    b.starsLink?.let { link ->
                        Button(onClick = { open(link) }, modifier = Modifier.fillMaxWidth()) { Text("Оплатить Stars в Telegram") }
                    }
                    if (b.tonEnabled) {
                        OutlinedButton(onClick = { vm.createTonInvoice() }, modifier = Modifier.fillMaxWidth()) { Text("Оплатить TON") }
                    }
                }
            }
        }

        // --- голос и пояс ---
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("Озвучивать ответы")
                Text("Голосом телефона, на устройстве", style = MaterialTheme.typography.bodySmall)
            }
            Switch(checked = vm.speakReplies, onCheckedChange = { vm.setSpeak(it) })
        }
        OutlinedButton(onClick = { pickTz = true }, modifier = Modifier.fillMaxWidth()) {
            Text("🕒 Часовой пояс: ${user?.timezone ?: ""}")
        }
        OutlinedButton(onClick = { vm.redoOnboarding() }, modifier = Modifier.fillMaxWidth()) { Text("📝 Изменить анкету") }

        HorizontalDivider()
        // --- привязка Telegram ---
        Text("Telegram", style = MaterialTheme.typography.titleMedium)
        if (user?.telegramLinked == true) {
            Text("✅ Привязан: память, задачи и подписка общие с ботом.")
        } else {
            Text("Отправьте боту /link и введите код — аккаунты объединятся.", style = MaterialTheme.typography.bodySmall)
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(code, { code = it.uppercase().take(8) }, label = { Text("Код") }, singleLine = true,
                    modifier = Modifier.weight(1f))
                Button(onClick = { vm.linkTelegram(code); code = "" }, enabled = code.length == 8 && !vm.busy) { Text("Привязать") }
            }
        }

        HorizontalDivider()
        vm.stats?.let { s ->
            Text("📊 Статистика", style = MaterialTheme.typography.titleMedium)
            Text("Сообщений: ${s.messages} · память: ${s.memories} · задач: ${s.tasksActive} активных, ${s.tasksDone} выполнено")
        }
        Text(
            if (vm.pushConfigured) "🔔 Push-уведомления включены" else "🔔 Push не настроен в этой сборке — напоминания придут в Telegram и в открытое приложение",
            style = MaterialTheme.typography.bodySmall,
        )
        TextButton(onClick = { logout = true }) { Text("Выйти", color = MaterialTheme.colorScheme.error) }
    }
    if (pickTz) {
        TimezoneDialog(vm, current = user?.timezone ?: vm.deviceTimezone,
            onPick = { vm.setTimezone(it); pickTz = false }, onDismiss = { pickTz = false })
    }
    if (logout) {
        ConfirmDialog("Выйти?", "Данные останутся на сервере, войти можно снова.", "Выйти",
            onConfirm = { vm.logout() }, onDismiss = { logout = false })
    }
    vm.tonInvoice?.let { inv ->
        AlertDialog(
            onDismissRequest = { vm.tonInvoice = null },
            title = { Text("Оплата TON") },
            text = {
                SelectionContainer {
                    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        Text("Сумма: ${inv.amountTon} TON")
                        Text("Адрес: ${inv.wallet}")
                        Text("Комментарий (обязательно!): ${inv.comment}")
                        Text("Счёт действует 1 час.", style = MaterialTheme.typography.bodySmall)
                    }
                }
            },
            confirmButton = { TextButton(onClick = { vm.checkTon() }) { Text("Проверить оплату") } },
            dismissButton = { TextButton(onClick = { open(inv.link) }) { Text("Открыть кошелёк") } },
        )
    }
}
