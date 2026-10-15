pragma Singleton
import QtQuick

/*
  SelfvoyTheme — дизайн-система «Dark Glassmorphism» v3 (макеты 30.09.2026).

  Глубокий фиолетово-чёрный фон с размытыми орбитами, стеклянные панели с
  сиреневым бликом по краю, CTA — градиент сирень → фиолет со свечением.
  Значения один в один с preview/styles.css, client/styles/selfvoy_theme.qss,
  android/.../ui/theme/Theme.kt, ios/Selfvoy/Support/Theme.swift и docs/DESIGN.md.
*/
QtObject {
    // --- фон ------------------------------------------------------------
    readonly property color bgDeep:      "#09080E"   // фон окна
    readonly property color bgBase:      "#0F0D17"   // нижний слой градиента
    readonly property color bgRaised:    "#16131F"   // карточки, секции
    readonly property color bgElevated:  "#1E1A2B"   // модалки, дропдауны
    readonly property color bgPanel:     "#14111D"   // подложка стеклянных панелей

    // --- стекло ---------------------------------------------------------
    readonly property real  glassFill:        0.055
    readonly property real  glassFillHover:   0.09
    readonly property real  glassFillPressed: 0.13
    readonly property real  glassBorder:      0.10
    readonly property real  glassBorderHover: 0.18
    readonly property real  glassHighlight:   0.14   // верхний блик
    readonly property color glassTint:        "#FFFFFF"
    readonly property color glassEdge:        "#D9B8FF" // сиреневый блик по краю панели

    // --- акцент ---------------------------------------------------------
    readonly property color accent:       "#8B5CF6"
    readonly property color accentSoft:   "#A78BFA"  // сообщения Selfvoy, подсветка
    readonly property color accentDeep:   "#6D28D9"
    readonly property real  accentFill:   0.18
    readonly property color accentGlow:   "#738B5CF6" // свечение акцентных кнопок (.45)
    readonly property color gradStart:    "#E6B3FF"   // CTA: сирень →
    readonly property color gradMid:      "#A77BFF"   //      фиолет →
    readonly property color gradEnd:      "#7346F5"   //      индиго
    readonly property color glowViolet:   "#6D28D9"   // фоновые орбиты
    readonly property color glowMagenta:  "#4C1D95"
    readonly property color glowIndigo:   "#3B3CC4"
    readonly property color lilac:        "#D9B8FF"   // индикатор активного пункта, ссылки
    readonly property color pink:         "#E8A8FF"

    // --- текст ----------------------------------------------------------
    readonly property color textPrimary:   "#F4F1FB"
    readonly property color textSecondary: "#B9B3C9"
    readonly property color textMuted:     "#7D768F"
    readonly property color textOnAccent:  "#FFFFFF"

    // --- состояния ------------------------------------------------------
    readonly property color success: "#4ADE80"
    readonly property color warning: "#FBBF24"
    readonly property color danger:  "#F87171"
    readonly property color online:  "#4ADE80"

    // --- геометрия ------------------------------------------------------
    readonly property int radiusPill:   999
    readonly property int radiusShell:  32   // главные панели окна
    readonly property int radiusCard:   24
    readonly property int radiusBubble: 20
    readonly property int radiusField:  14
    readonly property int radiusSmall:  12

    readonly property int spaceXs: 4
    readonly property int spaceSm: 8
    readonly property int spaceMd: 16
    readonly property int spaceLg: 24
    readonly property int spaceXl: 32
    readonly property int spaceXxl: 48

    // --- адаптивность: узкое окно — стек, широкое — сайдбар + контент ----
    readonly property int breakpointWide: 1000   // ≥ — сайдбар с подписями
    readonly property int sidebarWidth: 258
    readonly property int railWidth: 76
    readonly property int masterWidth: 340

    // --- тень -----------------------------------------------------------
    readonly property color shadowColor: "#99000000"
    readonly property int   shadowBlur:  40
    readonly property int   shadowY:     24

    // --- типографика: Manrope встроен в клиент (client/fonts, OFL) -------
    readonly property string fontFamily: "Manrope"
    readonly property int fontMicro:   11
    readonly property int fontSmall:   13
    readonly property int fontBody:    15
    readonly property int fontLead:    18
    readonly property int fontTitle:   22
    readonly property int fontHero:    34
    readonly property int fontDisplay: 54

    // --- анимация: fade + slide-up, 200–300 мс ---------------------------
    readonly property int  pressMs:  180
    readonly property int  moveMs:   250
    readonly property int  slowMs:   350
    readonly property real pressScale: 0.98
    readonly property real hoverLift:  -2
    readonly property int  ease: Easing.OutCubic

    // Вспомогательные цвета с прозрачностью
    function glass(alpha)      { return Qt.rgba(1, 1, 1, alpha) }
    function accentA(alpha)    { return Qt.rgba(accent.r, accent.g, accent.b, alpha) }
    function lilacA(alpha)     { return Qt.rgba(lilac.r, lilac.g, lilac.b, alpha) }
    function shade(alpha)      { return Qt.rgba(0, 0, 0, alpha) }

    // Приветствие по времени суток (Home)
    function greeting(date) {
        const h = date.getHours()
        if (h < 5) return "Доброй ночи"
        if (h < 12) return "Доброе утро"
        if (h < 18) return "Добрый день"
        return "Добрый вечер"
    }
}
