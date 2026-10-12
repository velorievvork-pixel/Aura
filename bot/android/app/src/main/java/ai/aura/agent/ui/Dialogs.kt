package ai.aura.agent.ui

import ai.aura.agent.AppViewModel
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

/** Выбор пояса: частые варианты + пояс устройства + ручной ввод IANA (сервер проверит). */
@Composable
fun TimezoneDialog(vm: AppViewModel, current: String, onPick: (String) -> Unit, onDismiss: () -> Unit) {
    var custom by remember { mutableStateOf(current) }
    val names = (listOf(vm.deviceTimezone to "Как на телефоне: ${vm.deviceTimezone}") +
        vm.options.timezones.map { it.name to it.title }).distinctBy { it.first }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Часовой пояс") },
        text = {
            Column {
                LazyColumn(Modifier.heightIn(max = 320.dp)) {
                    items(names) { (name, title) ->
                        Text(
                            (if (name == current) "● " else "") + title,
                            Modifier.fillMaxWidth().clickable { onPick(name) }.padding(vertical = 10.dp),
                        )
                    }
                }
                OutlinedTextField(value = custom, onValueChange = { custom = it }, label = { Text("Другой (IANA)") }, singleLine = true)
            }
        },
        confirmButton = { TextButton(onClick = { onPick(custom.trim()) }) { Text("Выбрать") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Отмена") } },
    )
}

/** Подтверждение опасного действия — без него запрос не уходит. */
@Composable
fun ConfirmDialog(title: String, text: String, confirm: String, onConfirm: () -> Unit, onDismiss: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = { Text(text) },
        confirmButton = {
            TextButton(onClick = { onConfirm(); onDismiss() }) { Text(confirm, color = MaterialTheme.colorScheme.error) }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Отмена") } },
    )
}
