package app.selfvoy.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

// Дизайн-система «Dark Glassmorphism» v3 (макеты 30.09.2026) — те же токены,
// что в client/qml/theme/SelfvoyTheme.qml, ios/Selfvoy/Support/Theme.swift и docs/DESIGN.md.

val BgDeep = Color(0xFF09080E)
val BgBase = Color(0xFF0F0D17)
val BgRaised = Color(0xFF16131F)
val BgElevated = Color(0xFF1E1A2B)
val BgPanel = Color(0xFF14111D)

val Accent = Color(0xFF8B5CF6)
val AccentSoft = Color(0xFFA78BFA)
val AccentDeep = Color(0xFF6D28D9)
val GradStart = Color(0xFFE6B3FF)
val GradMid = Color(0xFFA77BFF)
val GradEnd = Color(0xFF7346F5)
val GlowViolet = Color(0xFF6D28D9)
val GlowMagenta = Color(0xFF4C1D95)
val GlowIndigo = Color(0xFF3B3CC4)
val Lilac = Color(0xFFD9B8FF)

val TextPrimary = Color(0xFFF4F1FB)
val TextSecondary = Color(0xFFB9B3C9)
val TextMuted = Color(0xFF7D768F)

val Success = Color(0xFF4ADE80)
val Warning = Color(0xFFFBBF24)
val Danger = Color(0xFFF87171)

// Стекло: плёнка белого поверх подложки, рамка — сиреневый блик.
val GlassFill = Color(0x0EFFFFFF)
val GlassBorder = Color(0x1AFFFFFF)
val GlassHighlight = Color(0x14FFFFFF)

val RadiusShell = 32.dp
val RadiusCard = 24.dp
val RadiusBubble = 20.dp
val RadiusField = 14.dp

private val SelfvoyColorScheme = darkColorScheme(
    primary = Accent,
    onPrimary = Color.White,
    primaryContainer = AccentDeep,
    onPrimaryContainer = Color.White,
    secondary = AccentSoft,
    onSecondary = Color.Black,
    background = BgDeep,
    onBackground = TextPrimary,
    surface = BgPanel,
    onSurface = TextPrimary,
    surfaceVariant = BgRaised,
    onSurfaceVariant = TextSecondary,
    error = Danger,
    onError = Color.White,
    outline = GlassBorder,
)

private fun style(size: Int, weight: FontWeight, line: Int = (size * 1.35).toInt(), tracking: Double = 0.0) =
    TextStyle(fontFamily = Manrope, fontSize = size.sp, fontWeight = weight, lineHeight = line.sp,
        letterSpacing = tracking.sp)

/** Типографика макетов: крупный дисплей приветствия, заголовки 22–34, текст 15. */
val SelfvoyTypography = Typography(
    displayLarge = style(46, FontWeight.SemiBold, 50, -1.0),
    displayMedium = style(38, FontWeight.Bold, 44, -0.6),
    headlineLarge = style(32, FontWeight.Bold, 38, -0.4),
    headlineMedium = style(26, FontWeight.Bold, 32),
    headlineSmall = style(22, FontWeight.Bold, 28),
    titleLarge = style(20, FontWeight.Bold),
    titleMedium = style(17, FontWeight.Bold),
    titleSmall = style(15, FontWeight.SemiBold),
    bodyLarge = style(16, FontWeight.Medium),
    bodyMedium = style(15, FontWeight.Medium),
    bodySmall = style(13, FontWeight.Medium),
    labelLarge = style(15, FontWeight.Bold),
    labelMedium = style(13, FontWeight.SemiBold),
    labelSmall = style(11, FontWeight.SemiBold),
)

@Composable
fun SelfvoyTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = SelfvoyColorScheme, typography = SelfvoyTypography, content = content)
}

/** Статусный цвет по смыслу (токены success/warning/danger). */
fun statusColor(kind: String): Color = when {
    kind.startsWith("twofactor") || kind == "login.new" -> Warning
    kind == "confirmation.requested" -> Danger
    kind == "task.due" -> Success
    else -> AccentSoft
}
