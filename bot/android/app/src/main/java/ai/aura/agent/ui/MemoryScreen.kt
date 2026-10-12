package ai.aura.agent.ui

import ai.aura.agent.AppViewModel
import ai.aura.agent.core.MemoryDto
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
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
import androidx.compose.ui.unit.dp

private val KIND_ORDER = listOf("fact", "preference", "schedule", "contact")

@Composable
fun MemoryScreen(vm: AppViewModel) {
    LaunchedEffect(Unit) { vm.reloadMemories() }
    var toDelete by remember { mutableStateOf<MemoryDto?>(null) }
    var wipe by remember { mutableStateOf(false) }
    val grouped = vm.memories.groupBy { it.kind }
    LazyColumn(Modifier.fillMaxSize().padding(horizontal = 16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        item {
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth().padding(top = 12.dp)) {
                Text("Что я о вас знаю", style = MaterialTheme.typography.headlineSmall, modifier = Modifier.weight(1f))
                if (vm.memories.isNotEmpty()) {
                    TextButton(onClick = { wipe = true }) { Text("Стереть всё", color = MaterialTheme.colorScheme.error) }
                }
            }
        }
        if (vm.memories.isEmpty()) {
            item { Text("Пока ничего. Просто общайтесь — важное сохранится само (факты, вкусы, расписание, контакты).") }
        }
        KIND_ORDER.filter { grouped.containsKey(it) }.forEach { kind ->
            item {
                Text(vm.options.memoryKinds[kind] ?: kind, style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.primary, modifier = Modifier.padding(top = 12.dp))
            }
            items(grouped.getValue(kind), key = { it.id }) { m ->
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("• ${m.text}", modifier = Modifier.weight(1f))
                    IconButton(onClick = { toDelete = m }) { Icon(Icons.Filled.Delete, "Забыть") }
                }
            }
        }
    }
    toDelete?.let { m ->
        ConfirmDialog("Забыть?", "«${m.text}»", "Забыть", onConfirm = { vm.deleteMemory(m.id) }, onDismiss = { toDelete = null })
    }
    if (wipe) {
        ConfirmDialog(
            "Стереть всю память?", "Удалятся все записи памяти и история переписки. Анкета останется. Отменить нельзя.",
            "Стереть", onConfirm = { vm.wipeMemories() }, onDismiss = { wipe = false },
        )
    }
}
