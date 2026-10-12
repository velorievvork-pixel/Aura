package ai.aura.agent.ui

import ai.aura.agent.AppViewModel
import ai.aura.agent.ui.theme.AuraChip
import ai.aura.agent.ui.theme.AuraField
import ai.aura.agent.ui.theme.AuraOrb
import ai.aura.agent.ui.theme.Bubble
import ai.aura.agent.ui.theme.Danger
import ai.aura.agent.ui.theme.Muted
import ai.aura.agent.ui.theme.RoundIconButton
import ai.aura.agent.ui.theme.TextPrimary
import ai.aura.agent.ui.theme.TextSecondary
import android.Manifest
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.Send
import androidx.compose.material.icons.outlined.Mic
import androidx.compose.material.icons.outlined.Stop
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat

private val SUGGESTIONS = listOf(
    "Напомни завтра в 9 позвонить маме",
    "Что приготовить на ужин?",
    "Договорись с @username о встрече",
)

/** Чат (макет «Моя Аура»): реплики пользователя слева, ответы Ауры справа, стеклянные пузыри. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun ChatScreen(vm: AppViewModel) {
    val context = LocalContext.current
    var draft by remember { mutableStateOf("") }
    val list = rememberLazyListState()
    val micPermission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) vm.toggleRecording() else vm.toast = "Без доступа к микрофону голосовые недоступны"
    }
    LaunchedEffect(vm.messages.size) {
        if (vm.messages.isNotEmpty()) list.animateScrollToItem(vm.messages.size)
    }
    fun send() {
        if (draft.isBlank() || vm.thinking) return
        vm.send(draft)
        draft = ""
    }
    fun mic() {
        val granted = ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED
        if (granted || vm.recording) vm.toggleRecording() else micPermission.launch(Manifest.permission.RECORD_AUDIO)
    }

    Column(Modifier.fillMaxSize()) {
        // Шапка: сфера Ауры, статус, счётчик сообщений
        Row(
            Modifier
                .fillMaxWidth()
                .background(Brush.verticalGradient(listOf(Color(0x29D9B8FF), Color.Transparent)))
                .padding(horizontal = 20.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            AuraOrb(52.dp)
            Column(Modifier.weight(1f).padding(start = 14.dp)) {
                Text("Aura", style = MaterialTheme.typography.headlineSmall, color = TextPrimary)
                Text(
                    when {
                        vm.recording -> "Слушаю"
                        vm.thinking -> "Думает"
                        else -> "В сети"
                    } + " · Персональный ассистент",
                    style = MaterialTheme.typography.bodySmall, color = if (vm.recording) Danger else TextSecondary,
                )
            }
            vm.usage?.let { Muted("${it.used}/${it.limit}") }
        }

        LazyColumn(
            Modifier.weight(1f).fillMaxWidth(),
            state = list,
            contentPadding = PaddingValues(horizontal = 16.dp, vertical = 18.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp),
        ) {
            item {
                if (vm.messages.isEmpty()) {
                    Column(horizontalAlignment = Alignment.End, verticalArrangement = Arrangement.spacedBy(10.dp),
                        modifier = Modifier.fillMaxWidth()) {
                        Bubble(fromAura = true, modifier = Modifier.widthIn(max = 320.dp),
                            text = "Привет, ${vm.user?.name ?: ""}! Я ваша Аура. Расскажите о себе, попросите напомнить " +
                                "о деле или договориться о встрече — я запомню важное.")
                        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.End),
                            verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            SUGGESTIONS.forEach { s -> AuraChip(s) { draft = s.replace("@username", "@") } }
                        }
                    }
                }
            }
            itemsIndexed(vm.messages) { _, m ->
                Box(Modifier.fillMaxWidth(), contentAlignment = if (m.mine) Alignment.CenterStart else Alignment.CenterEnd) {
                    Bubble(fromAura = !m.mine, text = m.text, modifier = Modifier.widthIn(max = 320.dp))
                }
            }
            if (vm.thinking) item {
                Muted("Аура думает…", Modifier.fillMaxWidth().padding(end = 4.dp), align = TextAlign.End)
            }
        }

        // Ввод: микрофон, поле, отправка (как в android/app)
        Row(
            Modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, bottom = 8.dp, top = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            RoundIconButton(
                if (vm.recording) Icons.Outlined.Stop else Icons.Outlined.Mic,
                if (vm.recording) "Остановить и отправить" else "Сказать голосом",
                { mic() }, accent = vm.recording, size = 48.dp, enabled = !vm.thinking,
            )
            AuraField(draft, { draft = it }, if (vm.recording) "Говорите… ■ — отправить" else "Напишите что-нибудь…",
                Modifier.weight(1f), readOnly = vm.recording,
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Send), onDone = { send() })
            RoundIconButton(Icons.AutoMirrored.Outlined.Send, "Отправить", { send() }, accent = true, size = 50.dp,
                enabled = draft.isNotBlank() && !vm.thinking && !vm.recording)
        }
    }
}
