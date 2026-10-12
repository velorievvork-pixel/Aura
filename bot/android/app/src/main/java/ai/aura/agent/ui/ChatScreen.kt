package ai.aura.agent.ui

import ai.aura.agent.AppViewModel
import ai.aura.agent.ChatMsg
import android.Manifest
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material3.FilledIconButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
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
import androidx.core.content.ContextCompat

@Composable
fun ChatScreen(vm: AppViewModel) {
    val context = LocalContext.current
    var input by remember { mutableStateOf("") }
    val list = rememberLazyListState()
    val micPermission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) vm.toggleRecording() else vm.toast = "Без доступа к микрофону голосовые недоступны"
    }
    LaunchedEffect(vm.messages.size) {
        if (vm.messages.isNotEmpty()) list.animateScrollToItem(vm.messages.size - 1)
    }
    Column(Modifier.fillMaxSize()) {
        vm.usage?.let { u ->
            Text(
                "Сообщений сегодня: ${u.used} из ${u.limit}",
                style = MaterialTheme.typography.labelSmall,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
            )
        }
        LazyColumn(
            state = list,
            modifier = Modifier.weight(1f).fillMaxWidth().padding(horizontal = 12.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            if (vm.messages.isEmpty()) {
                item {
                    Text(
                        "Напишите или скажите что-нибудь. Например: «напомни завтра в 9 позвонить маме» " +
                            "или «что приготовить на ужин?»",
                        Modifier.padding(16.dp), style = MaterialTheme.typography.bodyMedium,
                    )
                }
            }
            items(vm.messages) { Bubble(it) }
        }
        if (vm.thinking) LinearProgressIndicator(Modifier.fillMaxWidth())
        Row(Modifier.fillMaxWidth().padding(8.dp), verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                value = input, onValueChange = { input = it },
                placeholder = { Text(if (vm.recording) "Говорите… нажмите ■ для отправки" else "Сообщение") },
                modifier = Modifier.weight(1f), maxLines = 5, enabled = !vm.recording,
            )
            IconButton(onClick = {
                val granted = ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) ==
                    PackageManager.PERMISSION_GRANTED
                if (granted || vm.recording) vm.toggleRecording() else micPermission.launch(Manifest.permission.RECORD_AUDIO)
            }, enabled = !vm.thinking) {
                Icon(
                    if (vm.recording) Icons.Filled.Stop else Icons.Filled.Mic,
                    contentDescription = if (vm.recording) "Остановить и отправить" else "Голосовое",
                    tint = if (vm.recording) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary,
                )
            }
            FilledIconButton(
                onClick = { vm.send(input); input = "" },
                enabled = input.isNotBlank() && !vm.thinking && !vm.recording,
                colors = IconButtonDefaults.filledIconButtonColors(),
            ) { Icon(Icons.AutoMirrored.Filled.Send, contentDescription = "Отправить") }
        }
    }
}

@Composable
private fun Bubble(msg: ChatMsg) {
    Box(Modifier.fillMaxWidth(), contentAlignment = if (msg.mine) Alignment.CenterEnd else Alignment.CenterStart) {
        Text(
            msg.text,
            modifier = Modifier
                .widthIn(max = 300.dp)
                .background(
                    if (msg.mine) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.secondaryContainer,
                    RoundedCornerShape(16.dp),
                )
                .padding(horizontal = 14.dp, vertical = 10.dp),
            style = MaterialTheme.typography.bodyLarge,
        )
    }
}
