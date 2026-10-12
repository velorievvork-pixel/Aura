package ai.aura.agent.ui

import ai.aura.agent.AppViewModel
import ai.aura.agent.ui.theme.Accent
import ai.aura.agent.ui.theme.AuraAvatar
import ai.aura.agent.ui.theme.AuraField
import ai.aura.agent.ui.theme.BgElevated
import ai.aura.agent.ui.theme.GlassButton
import ai.aura.agent.ui.theme.GlassCard
import ai.aura.agent.ui.theme.GradientButton
import ai.aura.agent.ui.theme.Lilac
import ai.aura.agent.ui.theme.Muted
import ai.aura.agent.ui.theme.ScreenHeader
import ai.aura.agent.ui.theme.SectionTitle
import ai.aura.agent.ui.theme.Success
import ai.aura.agent.ui.theme.TextPrimary
import ai.aura.agent.ui.theme.TextSecondary
import android.content.ActivityNotFoundException
import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.EditNote
import androidx.compose.material.icons.automirrored.outlined.Logout
import androidx.compose.material.icons.outlined.Schedule
import androidx.compose.material.icons.automirrored.outlined.Send
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp

/** «Ещё» (макет Settings из android/app): профиль, премиум, голос, пояс, Telegram, статистика. */
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
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 20.dp, vertical = 16.dp),
        verticalArrangement = Arrangement.spacedBy(20.dp),
    ) {
        ScreenHeader("Ещё")
        Row(verticalAlignment = Alignment.CenterVertically) {
            AuraAvatar(user?.name ?: "?", size = 56.dp, ring = true, online = true)
            Column(Modifier.weight(1f).padding(start = 16.dp)) {
                Text(user?.name ?: "", style = MaterialTheme.typography.titleLarge, color = TextPrimary)
                Text(user?.email ?: "", style = MaterialTheme.typography.bodySmall, color = TextSecondary)
            }
        }

        // Премиум — светящаяся карточка
        vm.billing?.let { b ->
            GlassCard(Modifier.fillMaxWidth(), radius = 26.dp, glow = true, tint = Color(0xFFB4AAFF), fill = if (b.premium) 0.2f else 0.12f) {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text(if (b.premium) "Премиум активен" else "Aura Премиум", style = MaterialTheme.typography.headlineSmall, color = TextPrimary)
                    Text(
                        if (b.premium) "до ${b.premiumUntil}" else "${b.priceRub} ₽/мес · ${b.priceStars} ⭐" + (b.priceTon?.let { " или $it TON" } ?: ""),
                        style = MaterialTheme.typography.titleSmall, color = Lilac,
                    )
                    UsageBar(b.used, b.limit)
                    Muted("Без комиссий магазинов приложений: оплата в Telegram или TON-кошельке.")
                    if (!b.premium) {
                        b.starsLink?.let { link -> GradientButton("Оплатить Stars в Telegram", { open(link) }, Modifier.fillMaxWidth()) }
                        if (b.tonEnabled) GlassButton("Оплатить TON", { vm.createTonInvoice() }, Modifier.fillMaxWidth())
                    }
                }
            }
        }

        SectionTitle("Голос и время")
        GlassCard(Modifier.fillMaxWidth()) {
            Column(verticalArrangement = Arrangement.spacedBy(14.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text("Озвучивать ответы", style = MaterialTheme.typography.titleSmall, color = TextPrimary)
                        Muted("Голосом телефона, прямо на устройстве")
                    }
                    Switch(
                        checked = vm.speakReplies, onCheckedChange = { vm.setSpeak(it) },
                        colors = SwitchDefaults.colors(checkedTrackColor = Accent, checkedThumbColor = Color.White,
                            uncheckedTrackColor = Color.White.copy(alpha = 0.08f), uncheckedBorderColor = Color.White.copy(alpha = 0.2f)),
                    )
                }
                GlassButton("Часовой пояс: ${user?.timezone ?: ""}", { pickTz = true }, Modifier.fillMaxWidth(), icon = Icons.Outlined.Schedule)
                GlassButton("Изменить анкету", { vm.redoOnboarding() }, Modifier.fillMaxWidth(), icon = Icons.Outlined.EditNote)
            }
        }

        SectionTitle("Telegram")
        GlassCard(Modifier.fillMaxWidth()) {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                if (user?.telegramLinked == true) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("● ", color = Success)
                        Text("Привязан: память, задачи и подписка общие с ботом.", style = MaterialTheme.typography.bodyMedium, color = TextPrimary)
                    }
                } else {
                    Muted("Отправьте боту команду /link и введите код — аккаунты объединятся.")
                    AuraField(code, { code = it.uppercase().take(8) }, "XXXXXXXX", label = "Код из бота")
                    GradientButton("Привязать", { vm.linkTelegram(code); code = "" }, Modifier.fillMaxWidth(),
                        icon = Icons.AutoMirrored.Outlined.Send, enabled = code.length == 8, busy = vm.busy)
                }
            }
        }

        vm.stats?.let { s ->
            SectionTitle("Статистика")
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Stat("${s.messages}", "сообщений", Modifier.weight(1f))
                Stat("${s.memories}", "в памяти", Modifier.weight(1f))
                Stat("${s.tasksDone}", "сделано", Modifier.weight(1f))
            }
        }
        Muted(if (vm.pushConfigured) "Push-уведомления включены."
            else "Push не настроен в этой сборке — напоминания придут в Telegram и в открытое приложение.")
        GlassButton("Выйти", { logout = true }, Modifier.fillMaxWidth(), icon = Icons.AutoMirrored.Outlined.Logout, danger = true)
        Spacer(Modifier.height(8.dp))
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
            containerColor = BgElevated,
            title = { Text("Оплата TON", style = MaterialTheme.typography.titleMedium, color = TextPrimary) },
            text = {
                SelectionContainer {
                    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text("${inv.amountTon} TON", style = MaterialTheme.typography.headlineSmall, color = Lilac)
                        Muted("Адрес")
                        Text(inv.wallet, style = MaterialTheme.typography.bodySmall, color = TextPrimary)
                        Muted("Комментарий — обязательно")
                        Text(inv.comment, style = MaterialTheme.typography.titleMedium, color = TextPrimary)
                        Muted("Счёт действует 1 час, зачисление — 1–2 минуты.")
                    }
                }
            },
            confirmButton = { TextButton(onClick = { vm.checkTon() }) { Text("Проверить оплату", color = Lilac) } },
            dismissButton = { TextButton(onClick = { open(inv.link) }) { Text("Открыть кошелёк", color = TextSecondary) } },
        )
    }
}

@Composable
private fun UsageBar(used: Int, limit: Int) {
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Box(Modifier.fillMaxWidth().height(6.dp).clip(RoundedCornerShape(3.dp)).background(Color.White.copy(alpha = 0.08f))) {
            Box(Modifier.fillMaxWidth(if (limit > 0) (used.toFloat() / limit).coerceIn(0.02f, 1f) else 0f).height(6.dp)
                .clip(RoundedCornerShape(3.dp))
                .background(Brush.horizontalGradient(listOf(ai.aura.agent.ui.theme.GradStart, ai.aura.agent.ui.theme.GradEnd))))
        }
        Muted("Сегодня: $used из $limit сообщений")
    }
}

@Composable
private fun Stat(value: String, label: String, modifier: Modifier = Modifier) {
    GlassCard(modifier, radius = 18.dp, contentPadding = androidx.compose.foundation.layout.PaddingValues(14.dp)) {
        Column {
            Text(value, style = MaterialTheme.typography.headlineSmall, color = TextPrimary)
            Muted(label)
        }
    }
}
