package ai.aura.app.ui.theme

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.abs

// Общие элементы v3 — аналоги client/qml/components (GlassPanel, GlassButton, Chip, …).

/** Фон приложения: почти чёрный фиолет и три размытые орбиты (макеты). */
@Composable
fun AuraBackground(modifier: Modifier = Modifier, content: @Composable BoxScope.() -> Unit) {
    Box(
        modifier = modifier
            .fillMaxSize()
            .background(Brush.verticalGradient(listOf(BgDeep, BgBase)))
            .drawBehind {
                fun orb(center: Offset, radius: Float, color: Color, alpha: Float) = drawCircle(
                    brush = Brush.radialGradient(
                        listOf(color.copy(alpha = alpha), color.copy(alpha = alpha * 0.45f), Color.Transparent),
                        center = center, radius = radius,
                    ),
                    radius = radius, center = center,
                )
                orb(Offset(0f, size.height * 0.92f), size.width * 0.9f, GlowViolet, 0.6f)
                orb(Offset(size.width, 0f), size.width * 0.8f, GlowMagenta, 0.7f)
                orb(Offset(size.width * 0.85f, size.height), size.width * 0.6f, GlowIndigo, 0.35f)
            },
        content = content,
    )
}

/** Сиреневый блик по краю стекла: ярче сверху и снизу, гаснет в середине. */
val GlassEdge = Brush.verticalGradient(
    listOf(Lilac.copy(alpha = 0.7f), Color.White.copy(alpha = 0.06f), Color.White.copy(alpha = 0.03f),
        Lilac.copy(alpha = 0.42f)),
)

/**
 * Стеклянная карточка (GlassPanel.qml): полупрозрачная плёнка на тёмной подложке,
 * блик по верхнему краю, сиреневая рамка; glow — фиолетовое свечение вокруг.
 */
@Composable
fun GlassCard(
    modifier: Modifier = Modifier,
    radius: Dp = RadiusCard,
    glow: Boolean = false,
    tint: Color = Color.White,
    fill: Float = 0.055f,
    contentPadding: PaddingValues = PaddingValues(20.dp),
    onClick: (() -> Unit)? = null,
    content: @Composable BoxScope.() -> Unit,
) {
    val shape = RoundedCornerShape(radius)
    Box(
        modifier = modifier
            .then(if (glow) Modifier.outerGlow(radius) else Modifier)
            .clip(shape)
            .background(BgPanel.copy(alpha = 0.82f))
            .background(Brush.verticalGradient(listOf(tint.copy(alpha = fill + 0.05f), tint.copy(alpha = fill))))
            .border(BorderStroke(1.dp, GlassEdge), shape)
            .then(if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier)
            .padding(contentPadding),
        content = content,
    )
}

/**
 * Фиолетовое свечение снаружи карточки. Обычная тень (shadow) просвечивает сквозь
 * полупрозрачное стекло тёмным прямоугольником, поэтому рисуем только кольца снаружи.
 */
fun Modifier.outerGlow(radius: Dp, color: Color = Accent, spread: Dp = 22.dp): Modifier = drawBehind {
    val steps = 11
    val step = spread.toPx() / steps
    for (i in 1..steps) {
        val d = step * i
        drawRoundRect(
            color = color.copy(alpha = 0.16f * (1f - i / (steps + 1f)) * (1f - i / (steps + 1f))),
            topLeft = Offset(-d + step / 2, -d + step / 2),
            size = androidx.compose.ui.geometry.Size(size.width + 2 * d - step, size.height + 2 * d - step),
            cornerRadius = androidx.compose.ui.geometry.CornerRadius(radius.toPx() + d),
            style = Stroke(width = step),
        )
    }
}

/** Совместимость со старыми экранами. */
@Composable
fun GlassPanel(modifier: Modifier = Modifier, content: @Composable BoxScope.() -> Unit) =
    GlassCard(modifier = modifier, contentPadding = PaddingValues(16.dp), content = content)

/** CTA: градиент сирень → фиолет → индиго со свечением (кнопка «Sign in» макета). */
@Composable
fun GradientButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    icon: ImageVector? = null,
    enabled: Boolean = true,
    busy: Boolean = false,
    height: Dp = 50.dp,
) {
    val shape = RoundedCornerShape(50)
    Row(
        modifier = modifier
            .height(height)
            .alpha(if (enabled) 1f else 0.45f)
            .then(if (enabled) Modifier.shadow(18.dp, shape, ambientColor = Accent, spotColor = Accent) else Modifier)
            .clip(shape)
            .background(Brush.horizontalGradient(listOf(GradStart, GradMid, GradEnd)))
            .border(1.dp, Color.White.copy(alpha = 0.28f), shape)
            .clickable(enabled = enabled && !busy, onClick = onClick)
            .padding(horizontal = 22.dp),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (busy) {
            CircularProgressIndicator(Modifier.size(18.dp), color = Color.White, strokeWidth = 2.dp)
            Spacer(Modifier.width(10.dp))
        } else if (icon != null) {
            Icon(icon, contentDescription = null, tint = Color.White, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(8.dp))
        }
        if (text.isNotEmpty()) {
            Text(text, color = Color.White, style = MaterialTheme.typography.labelLarge, maxLines = 1)
        }
    }
}

/** Стеклянная кнопка-пилюля (вторичные действия, «Continue with Google»). */
@Composable
fun GlassButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    icon: ImageVector? = null,
    enabled: Boolean = true,
    danger: Boolean = false,
    quiet: Boolean = false,
    height: Dp = 46.dp,
) {
    val shape = RoundedCornerShape(50)
    val fg = if (danger) Danger else TextPrimary
    Row(
        modifier = modifier
            .height(height)
            .alpha(if (enabled) 1f else 0.45f)
            .clip(shape)
            .background(
                when {
                    danger -> Danger.copy(alpha = 0.14f)
                    quiet -> Color.Transparent
                    else -> Color.White.copy(alpha = 0.06f)
                },
            )
            .then(if (quiet) Modifier else Modifier.border(1.dp, Color.White.copy(alpha = 0.14f), shape))
            .clickable(enabled = enabled, onClick = onClick)
            .padding(horizontal = if (text.isEmpty()) 0.dp else 18.dp),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (icon != null) {
            Icon(icon, contentDescription = text.ifEmpty { null }, tint = fg, modifier = Modifier.size(18.dp))
            if (text.isNotEmpty()) Spacer(Modifier.width(8.dp))
        }
        if (text.isNotEmpty()) Text(text, color = fg, style = MaterialTheme.typography.labelMedium, maxLines = 1)
    }
}

/** Круглая иконка-кнопка (микрофон, отправка, «назад»). */
@Composable
fun RoundIconButton(
    icon: ImageVector,
    contentDescription: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    accent: Boolean = false,
    size: Dp = 44.dp,
    enabled: Boolean = true,
) {
    Box(
        modifier = modifier
            .size(size)
            .alpha(if (enabled) 1f else 0.45f)
            .then(if (accent && enabled) Modifier.shadow(14.dp, CircleShape, ambientColor = Accent, spotColor = Accent) else Modifier)
            .clip(CircleShape)
            .background(
                if (accent) Brush.linearGradient(listOf(GradStart, GradEnd))
                else SolidColor(Color.White.copy(alpha = 0.06f)),
            )
            .border(1.dp, Color.White.copy(alpha = if (accent) 0.28f else 0.14f), CircleShape)
            .clickable(enabled = enabled, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Icon(icon, contentDescription = contentDescription,
            tint = if (accent) Color.White else TextSecondary, modifier = Modifier.size(size * 0.44f))
    }
}

/** Пилюля-чип: быстрые ответы, фильтры, варианты окна переговоров. */
@Composable
fun AuraChip(text: String, active: Boolean = false, enabled: Boolean = true, onClick: () -> Unit) {
    val shape = RoundedCornerShape(50)
    Box(
        modifier = Modifier
            .height(34.dp)
            .alpha(if (enabled) 1f else 0.55f)
            .clip(shape)
            .background(if (active) Color.White.copy(alpha = 0.14f) else Color(0x8C0A0812))
            .border(1.dp, Color.White.copy(alpha = if (active) 0.38f else 0.2f), shape)
            .clickable(enabled = enabled, onClick = onClick)
            .padding(horizontal = 15.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(text, style = MaterialTheme.typography.labelMedium, color = TextPrimary, maxLines = 1,
            overflow = TextOverflow.Ellipsis)
    }
}

/**
 * Поле ввода v3: тёмное стекло, тонкая рамка, сиреневая рамка в фокусе, иконка
 * справа (конверт/замок) или действие (глаз). Подпись — над полем (макеты входа).
 */
@Composable
fun AuraField(
    value: String,
    onValueChange: (String) -> Unit,
    placeholder: String,
    modifier: Modifier = Modifier,
    label: String = "",
    trailing: ImageVector? = null,
    onTrailingClick: (() -> Unit)? = null,
    password: Boolean = false,
    filled: Boolean = false,
    readOnly: Boolean = false,
    keyboardOptions: KeyboardOptions = KeyboardOptions.Default,
    onDone: (() -> Unit)? = null,
    /** >1 — многострочное поле (например, «О себе»): растёт до maxLines строк. */
    maxLines: Int = 1,
) {
    val interaction = remember { MutableInteractionSource() }
    val focused by interaction.collectIsFocusedAsState()
    val shape = RoundedCornerShape(RadiusField)
    Column(modifier = modifier, verticalArrangement = Arrangement.spacedBy(7.dp)) {
        if (label.isNotEmpty()) Text(label, style = MaterialTheme.typography.labelMedium, color = TextPrimary)
        BasicTextField(
            value = value,
            onValueChange = onValueChange,
            readOnly = readOnly,
            singleLine = maxLines <= 1,
            maxLines = maxLines.coerceAtLeast(1),
            interactionSource = interaction,
            textStyle = TextStyle(fontFamily = Manrope, fontSize = 15.sp, color = TextPrimary, fontWeight = FontWeight.Medium),
            cursorBrush = SolidColor(Lilac),
            visualTransformation = if (password) PasswordVisualTransformation() else VisualTransformation.None,
            keyboardOptions = keyboardOptions,
            keyboardActions = KeyboardActions(onDone = { onDone?.invoke() }, onGo = { onDone?.invoke() },
                onSend = { onDone?.invoke() }),
            decorationBox = { inner ->
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .then(if (maxLines <= 1) Modifier.height(48.dp) else Modifier.heightIn(min = 48.dp))
                        .clip(shape)
                        .background(if (filled) Color.White.copy(alpha = 0.09f) else Color(0x730A0812))
                        .border(
                            1.dp,
                            when {
                                focused -> Accent.copy(alpha = 0.8f)
                                filled -> Color.Transparent
                                else -> Color.White.copy(alpha = 0.16f)
                            },
                            shape,
                        )
                        .padding(start = 16.dp, end = 12.dp)
                        .then(if (maxLines <= 1) Modifier else Modifier.padding(vertical = 13.dp)),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Box(Modifier.weight(1f)) {
                        if (value.isEmpty()) Text(placeholder, color = TextMuted, style = MaterialTheme.typography.bodyMedium, maxLines = 1)
                        inner()
                    }
                    if (trailing != null) {
                        Icon(
                            trailing, contentDescription = null, tint = TextSecondary,
                            modifier = Modifier
                                .size(20.dp)
                                .then(if (onTrailingClick != null) Modifier.clickable(onClick = onTrailingClick) else Modifier),
                        )
                    }
                }
            },
        )
    }
}

/** Аватар-шар: цвет из имени, инициалы, опциональное кольцо. */
@Composable
fun AuraAvatar(name: String, size: Dp = 44.dp, ring: Boolean = false, online: Boolean = false) {
    val hue = remember(name) { name.fold(0) { acc, c -> (acc * 31 + c.code) % 360 }.let { abs(it).toFloat() } }
    val initials = remember(name) {
        val parts = name.trim().split(Regex("\\s+")).filter { it.isNotEmpty() }
        when {
            parts.isEmpty() -> "?"
            parts.size == 1 -> parts[0].take(1).uppercase()
            else -> (parts[0].take(1) + parts[1].take(1)).uppercase()
        }
    }
    Box(contentAlignment = Alignment.Center) {
        if (ring) Box(Modifier.size(size + 10.dp).border(3.dp, Lilac.copy(alpha = 0.25f), CircleShape))
        Box(
            modifier = Modifier
                .size(size)
                .clip(CircleShape)
                .background(
                    Brush.radialGradient(
                        listOf(Color.hsl(hue, 0.85f, 0.74f), Color.hsl(hue, 0.68f, 0.52f), Color.hsl((hue + 30) % 360, 0.7f, 0.3f)),
                    ),
                )
                .border(1.dp, Color.White.copy(alpha = 0.2f), CircleShape),
            contentAlignment = Alignment.Center,
        ) {
            Text(initials, color = Color.White, fontFamily = Manrope, fontWeight = FontWeight.Bold,
                fontSize = (size.value * 0.36f).sp)
        }
        if (online) {
            Box(
                Modifier
                    .align(Alignment.BottomEnd)
                    .size(size * 0.26f)
                    .clip(CircleShape)
                    .background(Success)
                    .border(2.dp, BgPanel, CircleShape),
            )
        }
    }
}

/** Логотип Aura: кольцо из 12 изогнутых штрихов (геометрия иконки приложения). */
@Composable
fun AuraEmblem(size: Dp = 72.dp, modifier: Modifier = Modifier) {
    Canvas(modifier.size(size)) {
        val s = this.size.minDimension / 100f
        drawCircle(
            Brush.radialGradient(listOf(Lilac.copy(alpha = 0.35f), Color.Transparent), radius = 60 * s),
            radius = 60 * s,
        )
        val brush = Brush.linearGradient(listOf(Color(0xFFF6E6FF), Color(0xFFB18CFF)))
        repeat(12) { i ->
            rotate(i * 30f) {
                val path = Path().apply {
                    moveTo(50 * s, 9 * s)
                    quadraticTo(60 * s, 9.5f * s, 66 * s, 19 * s)
                }
                drawPath(path, brush, style = Stroke(width = 5 * s, cap = StrokeCap.Round))
            }
        }
    }
}

/** Светящаяся сфера-аватар Ауры (шапка «Моей Ауры»). */
@Composable
fun AuraOrb(size: Dp = 56.dp) {
    Canvas(Modifier.size(size)) {
        val r = this.size.minDimension / 2
        drawCircle(Brush.radialGradient(listOf(AccentSoft.copy(alpha = 0.45f), Color.Transparent), radius = r * 1.4f),
            radius = r * 1.4f)
        drawCircle(
            Brush.radialGradient(
                0f to Color.White, 0.1f to Color(0xFFF3D9FF), 0.34f to Color(0xFFB98CFF),
                0.62f to Color(0xFF5B3BD6), 1f to Color(0xFF1A1040),
                center = Offset(r * 0.64f, r * 0.56f), radius = r * 1.1f,
            ),
            radius = r,
        )
    }
}

/** Заголовок экрана (38 bold) с действиями справа. */
@Composable
fun ScreenHeader(title: String, modifier: Modifier = Modifier, actions: @Composable RowScope.() -> Unit = {}) {
    Row(modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(title, style = MaterialTheme.typography.displayMedium, color = TextPrimary, modifier = Modifier.weight(1f))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically, content = actions)
    }
}

/** Заголовок секции с сиреневым индикатором слева (макет Settings). */
@Composable
fun SectionTitle(text: String, modifier: Modifier = Modifier) {
    Row(modifier, verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(width = 3.dp, height = 18.dp).clip(RoundedCornerShape(2.dp)).background(Lilac))
        Spacer(Modifier.width(10.dp))
        Text(text, style = MaterialTheme.typography.titleMedium, color = TextPrimary)
    }
}

@Composable
fun Dot(color: Color, size: Dp = 10.dp) {
    Box(Modifier.size(size).clip(CircleShape).background(color))
}

@Composable
fun Muted(text: String, modifier: Modifier = Modifier, align: TextAlign? = null) {
    Text(text, modifier = modifier, style = MaterialTheme.typography.bodySmall, color = TextMuted, textAlign = align)
}
