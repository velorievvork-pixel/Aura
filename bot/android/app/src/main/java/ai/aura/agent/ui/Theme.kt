package ai.aura.agent.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val Indigo = Color(0xFF4F46E5)
private val Violet = Color(0xFF7C3AED)

private val Light = lightColorScheme(
    primary = Indigo, secondary = Violet, tertiary = Color(0xFF0EA5E9),
    background = Color(0xFFF8F7FF), surface = Color(0xFFFFFFFF),
    primaryContainer = Color(0xFFE0E7FF), secondaryContainer = Color(0xFFEDE9FE),
)

private val Dark = darkColorScheme(
    primary = Color(0xFFA5B4FC), secondary = Color(0xFFC4B5FD), tertiary = Color(0xFF7DD3FC),
    primaryContainer = Color(0xFF3730A3), secondaryContainer = Color(0xFF4C1D95),
)

@Composable
fun AuraTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = if (isSystemInDarkTheme()) Dark else Light, content = content)
}
