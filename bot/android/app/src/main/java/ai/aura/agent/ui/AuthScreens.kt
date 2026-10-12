package ai.aura.agent.ui

import ai.aura.agent.AppViewModel
import ai.aura.agent.core.Validation
import ai.aura.agent.ui.theme.AuraChip
import ai.aura.agent.ui.theme.AuraEmblem
import ai.aura.agent.ui.theme.AuraField
import ai.aura.agent.ui.theme.GlassButton
import ai.aura.agent.ui.theme.GlassCard
import ai.aura.agent.ui.theme.GradientButton
import ai.aura.agent.ui.theme.Link
import ai.aura.agent.ui.theme.Muted
import ai.aura.agent.ui.theme.SectionTitle
import ai.aura.agent.ui.theme.TextPrimary
import ai.aura.agent.ui.theme.TextSecondary
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Lock
import androidx.compose.material.icons.outlined.Mail
import androidx.compose.material.icons.outlined.Schedule
import androidx.compose.material.icons.outlined.Visibility
import androidx.compose.material.icons.outlined.VisibilityOff
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp

/** Вход и регистрация (макет Welcome из android/app): стеклянная карточка с эмблемой. */
@Composable
fun LoginScreen(vm: AppViewModel) {
    var register by remember { mutableStateOf(false) }
    var email by remember { mutableStateOf("") }
    var password by remember { mutableStateOf("") }
    var showPassword by remember { mutableStateOf(false) }
    var showServer by remember { mutableStateOf(false) }
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        GlassCard(Modifier.fillMaxWidth(), radius = 34.dp, contentPadding = PaddingValues(26.dp)) {
            Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(14.dp)) {
                AuraEmblem(76.dp, Modifier.align(Alignment.CenterHorizontally))
                Text(
                    if (register) "Создайте свою Ауру" else "Добро пожаловать в Aura",
                    style = MaterialTheme.typography.headlineMedium, color = TextPrimary,
                    textAlign = TextAlign.Center, modifier = Modifier.fillMaxWidth(),
                )
                Muted(
                    if (register) "Без телефона и SMS — только email и пароль" else "Личный ИИ-агент: помнит важное, ведёт задачи",
                    Modifier.fillMaxWidth(), align = TextAlign.Center,
                )
                AuraField(email, { email = it }, "Введите email", label = "Email", trailing = Icons.Outlined.Mail,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Email))
                AuraField(
                    password, { password = it }, if (register) "Минимум 8 символов, буквы и цифры" else "••••••••",
                    label = "Пароль", password = !showPassword,
                    trailing = if (register) (if (showPassword) Icons.Outlined.VisibilityOff else Icons.Outlined.Visibility) else Icons.Outlined.Lock,
                    onTrailingClick = if (register) ({ showPassword = !showPassword }) else null,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
                    onDone = { vm.auth(email, password, register) },
                )
                if (register) {
                    AuraField("${vm.deviceTimezone} · по системе", {}, "", label = "Часовой пояс", readOnly = true,
                        trailing = Icons.Outlined.Schedule)
                }
                GradientButton(if (register) "Создать аккаунт" else "Войти", { vm.auth(email, password, register) },
                    Modifier.fillMaxWidth().padding(top = 4.dp), busy = vm.busy)
                if (register) Muted("Пароль хранится как Argon2id-хэш", Modifier.fillMaxWidth(), align = TextAlign.Center)
                Link(if (register) "Уже есть аккаунт?" else "Создать аккаунт", Modifier.align(Alignment.End), color = TextPrimary) {
                    register = !register
                }
            }
        }
        Spacer(Modifier.height(16.dp))
        Link(if (showServer) "Скрыть адрес сервера" else "Адрес сервера", color = TextSecondary) { showServer = !showServer }
        if (showServer) {
            AuraField(vm.serverUrl, { vm.serverUrl = it }, "https://…", Modifier.fillMaxWidth().padding(top = 10.dp),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri))
        }
        Spacer(Modifier.height(12.dp))
        Muted("Уже пользуетесь ботом в Telegram? После входа привяжите его во вкладке «Ещё».",
            Modifier.fillMaxWidth(), align = TextAlign.Center)
    }
}

/** Анкета: те же поля, что в Telegram; ответы сразу попадают в контекст агента. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun OnboardingScreen(vm: AppViewModel) {
    val p = vm.user?.profile
    var birthday by remember { mutableStateOf(Validation.isoToBirthday(p?.birthday)) }
    var allergies by remember { mutableStateOf(p?.allergies?.joinToString(", ") ?: "") }
    var diets by remember { mutableStateOf(p?.diets?.toSet() ?: emptySet()) }
    var habits by remember { mutableStateOf(p?.habits?.joinToString(", ") ?: "") }
    var tz by remember { mutableStateOf(vm.user?.timezone?.takeIf { p != null } ?: vm.deviceTimezone) }
    var pickTz by remember { mutableStateOf(false) }
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 20.dp, vertical = 16.dp),
        verticalArrangement = Arrangement.spacedBy(18.dp),
    ) {
        Text("Анкета", style = MaterialTheme.typography.displayMedium, color = TextPrimary)
        Text("Пять вопросов — и я сразу учту их в советах. Всё можно оставить пустым.",
            style = MaterialTheme.typography.bodyMedium, color = TextSecondary)
        GlassCard(Modifier.fillMaxWidth()) {
            Column(verticalArrangement = Arrangement.spacedBy(14.dp)) {
                AuraField(birthday, { birthday = it }, "ДД.ММ.ГГГГ", label = "День рождения",
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number))
                AuraField(allergies, { allergies = it }, "арахис, мёд", label = "Аллергии через запятую")
                AuraField(habits, { habits = it }, "бегаю по утрам, ложусь в 23:00", label = "Привычки через запятую")
                AuraField(vm.options.timezones.firstOrNull { it.name == tz }?.title ?: tz, {}, "", label = "Часовой пояс",
                    readOnly = true, trailing = Icons.Outlined.Schedule, onTrailingClick = { pickTz = true })
                Link("Выбрать другой пояс") { pickTz = true }
            }
        }
        SectionTitle("Питание")
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            vm.options.diets.forEach { d ->
                AuraChip((if (d.code in diets) "✓ " else "") + d.title, active = d.code in diets) {
                    diets = if (d.code in diets) diets - d.code else diets + d.code
                }
            }
        }
        GradientButton("Сохранить", { vm.saveProfile(birthday, allergies, diets.toList(), habits, tz) },
            Modifier.fillMaxWidth(), busy = vm.busy)
        if (vm.user?.onboarded == true) GlassButton("Отмена", { vm.cancelOnboarding() }, Modifier.fillMaxWidth(), quiet = true)
        Spacer(Modifier.height(8.dp))
    }
    if (pickTz) TimezoneDialog(vm, current = tz, onPick = { tz = it; pickTz = false }, onDismiss = { pickTz = false })
}
