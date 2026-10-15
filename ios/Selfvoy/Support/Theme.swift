// Theme.swift — дизайн-система «Dark Glassmorphism» v3 (макеты 30.09.2026).
//
// Те же токены, что в client/qml/theme/SelfvoyTheme.qml,
// android/.../ui/theme/Theme.kt, preview/styles.css и docs/DESIGN.md.
// Шрифт Manrope (SIL OFL 1.1) встроен в приложение: Resources/Fonts, UIAppFonts.
import SwiftUI

extension Color {
    /// Цвет из 0xRRGGBB.
    init(hex: UInt32, alpha: Double = 1) {
        self.init(.sRGB,
                  red: Double((hex >> 16) & 0xFF) / 255,
                  green: Double((hex >> 8) & 0xFF) / 255,
                  blue: Double(hex & 0xFF) / 255,
                  opacity: alpha)
    }
}

enum SelfvoyColor {
    static let bgDeep = Color(hex: 0x09080E)
    static let bgBase = Color(hex: 0x0F0D17)
    static let bgRaised = Color(hex: 0x16131F)
    static let bgElevated = Color(hex: 0x1E1A2B)
    static let bgPanel = Color(hex: 0x14111D)

    static let accent = Color(hex: 0x8B5CF6)
    static let accentSoft = Color(hex: 0xA78BFA)
    static let accentDeep = Color(hex: 0x6D28D9)
    static let gradStart = Color(hex: 0xE6B3FF)
    static let gradMid = Color(hex: 0xA77BFF)
    static let gradEnd = Color(hex: 0x7346F5)
    static let glowViolet = Color(hex: 0x6D28D9)
    static let glowMagenta = Color(hex: 0x4C1D95)
    static let glowIndigo = Color(hex: 0x3B3CC4)
    static let lilac = Color(hex: 0xD9B8FF)

    static let textPrimary = Color(hex: 0xF4F1FB)
    static let textSecondary = Color(hex: 0xB9B3C9)
    static let textMuted = Color(hex: 0x7D768F)

    static let success = Color(hex: 0x4ADE80)
    static let warning = Color(hex: 0xFBBF24)
    static let danger = Color(hex: 0xF87171)

    /// Тёмная подложка полей и чипов (#0A0812).
    static let well = Color(hex: 0x0A0812)

    /// CTA: сирень → фиолет → индиго.
    static let cta = LinearGradient(colors: [gradStart, gradMid, gradEnd],
                                    startPoint: .leading, endPoint: .trailing)

    /// Сиреневый блик по краю стекла: ярче сверху и снизу, гаснет в середине.
    static let glassEdge = LinearGradient(
        stops: [.init(color: lilac.opacity(0.7), location: 0),
                .init(color: .white.opacity(0.06), location: 0.35),
                .init(color: .white.opacity(0.03), location: 0.7),
                .init(color: lilac.opacity(0.42), location: 1)],
        startPoint: .top, endPoint: .bottom)

    /// Статусный цвет по виду уведомления.
    static func status(_ kind: String) -> Color {
        if kind.hasPrefix("twofactor") || kind == "login.new" { return warning }
        if kind == "confirmation.requested" { return danger }
        if kind == "task.due" { return success }
        return accentSoft
    }
}

enum SelfvoyRadius {
    static let shell: CGFloat = 32
    static let card: CGFloat = 24
    static let bubble: CGFloat = 20
    static let field: CGFloat = 14
}

/// Типографика макетов: Manrope, дисплей приветствия 46, заголовки 22–38, текст 15.
enum SelfvoyFont {
    static func manrope(_ size: CGFloat, _ weight: Font.Weight = .medium) -> Font {
        let name: String
        switch weight {
        case .regular, .light, .thin, .ultraLight: name = "Manrope-Regular"
        case .semibold: name = "Manrope-SemiBold"
        case .bold: name = "Manrope-Bold"
        case .heavy, .black: name = "Manrope-ExtraBold"
        default: name = "Manrope-Medium"
        }
        return .custom(name, size: size)
    }

    static let display = manrope(46, .semibold)
    static let screenTitle = manrope(38, .bold)
    static let headline = manrope(26, .bold)
    static let title = manrope(22, .bold)
    static let titleMedium = manrope(17, .bold)
    static let titleSmall = manrope(15, .semibold)
    static let body = manrope(15, .medium)
    static let small = manrope(13, .medium)
    static let label = manrope(15, .bold)
    static let labelMedium = manrope(13, .semibold)
    static let micro = manrope(11, .semibold)
}
