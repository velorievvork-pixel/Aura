package ai.aura.agent.ui

import ai.aura.agent.AppViewModel
import ai.aura.agent.core.MemoryDto
import ai.aura.agent.ui.theme.AuraOrb
import ai.aura.agent.ui.theme.GlassButton
import ai.aura.agent.ui.theme.GlassCard
import ai.aura.agent.ui.theme.Muted
import ai.aura.agent.ui.theme.ScreenHeader
import ai.aura.agent.ui.theme.SectionTitle
import ai.aura.agent.ui.theme.TextPrimary
import ai.aura.agent.ui.theme.TextSecondary
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Close
import androidx.compose.material.icons.outlined.DeleteSweep
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

private val KIND_ORDER = listOf("fact", "preference", "schedule", "contact")

/** Память: что Аура узнала из разговоров, по четырём типам; каждую запись можно забыть. */
@Composable
fun MemoryScreen(vm: AppViewModel) {
    LaunchedEffect(Unit) { vm.reloadMemories() }
    var toDelete by remember { mutableStateOf<MemoryDto?>(null) }
    var wipe by remember { mutableStateOf(false) }
    val grouped = vm.memories.groupBy { it.kind }
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 20.dp, vertical = 16.dp),
        verticalArrangement = Arrangement.spacedBy(20.dp),
    ) {
        ScreenHeader("Память")
        GlassCard(Modifier.fillMaxWidth(), radius = 26.dp, glow = true, tint = Color(0xFFB4AAFF), fill = 0.2f) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                AuraOrb(48.dp)
                Column(Modifier.weight(1f).padding(start = 14.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text("${vm.memories.size} ${plural(vm.memories.size)}", style = MaterialTheme.typography.headlineSmall, color = TextPrimary)
                    Text("Аура учится после каждого разговора. Пароли и номера карт не сохраняются.",
                        style = MaterialTheme.typography.bodySmall, color = TextSecondary)
                }
            }
        }
        if (vm.memories.isEmpty()) Muted("Пока пусто. Просто общайтесь — факты, вкусы, расписание и контакты сохранятся сами.")
        KIND_ORDER.filter { grouped.containsKey(it) }.forEach { kind ->
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                SectionTitle(vm.options.memoryKinds[kind] ?: kind)
                GlassCard(Modifier.fillMaxWidth(), contentPadding = PaddingValues(horizontal = 16.dp, vertical = 6.dp)) {
                    Column {
                        grouped.getValue(kind).forEachIndexed { i, m ->
                            if (i > 0) HorizontalDivider(color = Color.White.copy(alpha = 0.08f))
                            Row(Modifier.fillMaxWidth().padding(vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                                Text(m.text, style = MaterialTheme.typography.bodyMedium, color = TextPrimary, modifier = Modifier.weight(1f))
                                Box(Modifier.size(36.dp).clip(RoundedCornerShape(50)).clickable { toDelete = m },
                                    contentAlignment = Alignment.Center) {
                                    Icon(Icons.Outlined.Close, "Забыть", tint = TextSecondary, modifier = Modifier.size(18.dp))
                                }
                            }
                        }
                    }
                }
            }
        }
        if (vm.memories.isNotEmpty()) {
            GlassButton("Стереть всю память", { wipe = true }, Modifier.fillMaxWidth(), icon = Icons.Outlined.DeleteSweep, danger = true)
        }
        Spacer(Modifier.height(8.dp))
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

private fun plural(n: Int): String {
    val m10 = n % 10
    val m100 = n % 100
    return when {
        m10 == 1 && m100 != 11 -> "запись"
        m10 in 2..4 && m100 !in 12..14 -> "записи"
        else -> "записей"
    }
}
