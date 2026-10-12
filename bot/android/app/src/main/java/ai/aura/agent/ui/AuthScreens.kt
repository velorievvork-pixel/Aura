package ai.aura.agent.ui

import ai.aura.agent.AppViewModel
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp

@Composable
fun LoginScreen(vm: AppViewModel) {
    var email by remember { mutableStateOf("") }
    var password by remember { mutableStateOf("") }
    var showServer by remember { mutableStateOf(false) }
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Spacer(Modifier.height(32.dp))
        Text("Aura", style = MaterialTheme.typography.displayMedium, color = MaterialTheme.colorScheme.primary)
        Text("Личный ИИ-агент: помнит важное, ведёт задачи и напоминания.", style = MaterialTheme.typography.bodyLarge)
        Spacer(Modifier.height(8.dp))
        OutlinedTextField(
            value = email, onValueChange = { email = it }, label = { Text("Email") }, singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Email), modifier = Modifier.fillMaxWidth(),
        )
        OutlinedTextField(
            value = password, onValueChange = { password = it }, label = { Text("Пароль") }, singleLine = true,
            visualTransformation = PasswordVisualTransformation(),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password), modifier = Modifier.fillMaxWidth(),
            supportingText = { Text("Минимум 8 символов, буквы и цифры") },
        )
        Button(onClick = { vm.auth(email, password, register = false) }, enabled = !vm.busy, modifier = Modifier.fillMaxWidth()) {
            Text("Войти")
        }
        OutlinedButton(onClick = { vm.auth(email, password, register = true) }, enabled = !vm.busy, modifier = Modifier.fillMaxWidth()) {
            Text("Создать аккаунт")
        }
        TextButton(onClick = { showServer = !showServer }) { Text("Сервер: ${vm.serverUrl}") }
        if (showServer) {
            OutlinedTextField(
                value = vm.serverUrl, onValueChange = { vm.serverUrl = it }, label = { Text("Адрес сервера Aura") },
                singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
                modifier = Modifier.fillMaxWidth(),
            )
        }
        Text(
            "Без телефона и SMS. Уже пользуетесь ботом в Telegram? После входа привяжите его в «Ещё» — память станет общей.",
            style = MaterialTheme.typography.bodySmall,
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun OnboardingScreen(vm: AppViewModel) {
    val p = vm.user?.profile
    var birthday by remember { mutableStateOf(ai.aura.agent.core.Validation.isoToBirthday(p?.birthday)) }
    var allergies by remember { mutableStateOf(p?.allergies?.joinToString(", ") ?: "") }
    var diets by remember { mutableStateOf(p?.diets?.toSet() ?: emptySet()) }
    var habits by remember { mutableStateOf(p?.habits?.joinToString(", ") ?: "") }
    var tz by remember { mutableStateOf(vm.user?.timezone?.takeIf { p != null } ?: vm.deviceTimezone) }
    var pickTz by remember { mutableStateOf(false) }
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Анкета", style = MaterialTheme.typography.headlineMedium)
        Text("Пять вопросов — и я сразу учту их в советах. Всё можно оставить пустым.")
        OutlinedTextField(
            value = birthday, onValueChange = { birthday = it }, label = { Text("🎂 День рождения (ДД.ММ.ГГГГ)") },
            singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
            modifier = Modifier.fillMaxWidth(),
        )
        OutlinedTextField(
            value = allergies, onValueChange = { allergies = it }, label = { Text("⚠️ Аллергии через запятую") },
            placeholder = { Text("арахис, мёд") }, modifier = Modifier.fillMaxWidth(),
        )
        Text("🥗 Питание", style = MaterialTheme.typography.titleMedium)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            vm.options.diets.forEach { d ->
                FilterChip(
                    selected = d.code in diets,
                    onClick = { diets = if (d.code in diets) diets - d.code else diets + d.code },
                    label = { Text(d.title) },
                )
            }
        }
        OutlinedTextField(
            value = habits, onValueChange = { habits = it }, label = { Text("🔁 Привычки через запятую") },
            placeholder = { Text("бегаю по утрам, ложусь в 23:00") }, modifier = Modifier.fillMaxWidth(),
        )
        OutlinedButton(onClick = { pickTz = true }, modifier = Modifier.fillMaxWidth()) {
            Text("🕒 Часовой пояс: ${vm.options.timezones.firstOrNull { it.name == tz }?.title ?: tz}")
        }
        Button(
            onClick = { vm.saveProfile(birthday, allergies, diets.toList(), habits, tz) },
            enabled = !vm.busy, modifier = Modifier.fillMaxWidth(),
        ) { Text("Сохранить") }
    }
    if (pickTz) {
        TimezoneDialog(vm, current = tz, onPick = { tz = it; pickTz = false }, onDismiss = { pickTz = false })
    }
}
