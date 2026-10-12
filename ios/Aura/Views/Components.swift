// Components.swift — общие элементы v3, аналоги client/qml/components и
// android/.../ui/theme/Components.kt: фон с орбитами, стеклянные карточки,
// кнопки, чипы, поля, аватары, эмблема и сфера Ауры.
import SwiftUI

// MARK: - Фон

/// Фон приложения: почти чёрный фиолет и три размытые орбиты (макеты).
struct AuraBackground: View {
    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width
            let h = geo.size.height
            ZStack {
                LinearGradient(colors: [AuraColor.bgDeep, AuraColor.bgBase], startPoint: .top, endPoint: .bottom)
                orb(AuraColor.glowViolet, alpha: 0.6, radius: w * 0.9)
                    .position(x: 0, y: h * 0.92)
                orb(AuraColor.glowMagenta, alpha: 0.7, radius: w * 0.8)
                    .position(x: w, y: 0)
                orb(AuraColor.glowIndigo, alpha: 0.35, radius: w * 0.6)
                    .position(x: w * 0.85, y: h)
            }
        }
        .ignoresSafeArea()
    }

    private func orb(_ color: Color, alpha: Double, radius: CGFloat) -> some View {
        Circle()
            .fill(RadialGradient(colors: [color.opacity(alpha), color.opacity(alpha * 0.45), .clear],
                                 center: .center, startRadius: 0, endRadius: radius))
            .frame(width: radius * 2, height: radius * 2)
    }
}

// MARK: - Стекло

/// Стеклянная карточка (GlassPanel.qml): плёнка на тёмной подложке, сиреневый блик по краю.
struct GlassCardModifier: ViewModifier {
    var radius: CGFloat = AuraRadius.card
    var glow = false
    var tint: Color = .white
    var fill: Double = 0.055
    var padding: CGFloat = 20

    func body(content: Content) -> some View {
        let shape = RoundedRectangle(cornerRadius: radius, style: .continuous)
        content
            .padding(padding)
            .background(
                ZStack {
                    shape.fill(AuraColor.bgPanel.opacity(0.82))
                    shape.fill(LinearGradient(colors: [tint.opacity(fill + 0.05), tint.opacity(fill)],
                                              startPoint: .top, endPoint: .bottom))
                }
            )
            .overlay(shape.strokeBorder(AuraColor.glassEdge, lineWidth: 1))
            .clipShape(shape)
            .shadow(color: glow ? AuraColor.accent.opacity(0.45) : .clear, radius: glow ? 24 : 0)
    }
}

extension View {
    func glassCard(radius: CGFloat = AuraRadius.card, glow: Bool = false, tint: Color = .white,
                   fill: Double = 0.055, padding: CGFloat = 20) -> some View {
        modifier(GlassCardModifier(radius: radius, glow: glow, tint: tint, fill: fill, padding: padding))
    }
}

/// Нажатие: лёгкое уменьшение, как у кнопок десктопа (pressScale 0.98).
struct PressStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .scaleEffect(configuration.isPressed ? 0.97 : 1)
            .animation(.easeOut(duration: 0.18), value: configuration.isPressed)
    }
}

// MARK: - Кнопки

/// CTA: градиент сирень → фиолет → индиго со свечением.
struct GradientButton: View {
    let title: String
    var systemImage: String?
    var enabled = true
    var busy = false
    var height: CGFloat = 50
    var fullWidth = true
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 8) {
                if busy {
                    ProgressView().tint(.white).controlSize(.small)
                } else if let systemImage {
                    Image(systemName: systemImage).font(.system(size: 15, weight: .semibold))
                }
                if !title.isEmpty {
                    Text(title).font(AuraFont.label).lineLimit(1)
                }
            }
            .foregroundStyle(.white)
            .padding(.horizontal, 22)
            .frame(maxWidth: fullWidth ? .infinity : nil, minHeight: height, maxHeight: height)
            .background(Capsule().fill(AuraColor.cta))
            .overlay(Capsule().strokeBorder(.white.opacity(0.28), lineWidth: 1))
            .shadow(color: enabled ? AuraColor.accent.opacity(0.5) : .clear, radius: 14, y: 4)
        }
        .buttonStyle(PressStyle())
        .disabled(!enabled || busy)
        .opacity(enabled ? 1 : 0.45)
    }
}

/// Стеклянная кнопка-пилюля: вторичные действия.
struct GlassButton: View {
    let title: String
    var systemImage: String?
    var enabled = true
    var danger = false
    var quiet = false
    var height: CGFloat = 46
    var fullWidth = true
    let action: () -> Void

    var body: some View {
        let fg = danger ? AuraColor.danger : AuraColor.textPrimary
        Button(action: action) {
            HStack(spacing: 8) {
                if let systemImage {
                    Image(systemName: systemImage).font(.system(size: 15, weight: .medium))
                }
                if !title.isEmpty {
                    Text(title).font(AuraFont.labelMedium).lineLimit(1)
                }
            }
            .foregroundStyle(fg)
            .padding(.horizontal, title.isEmpty ? 0 : 18)
            .frame(maxWidth: fullWidth ? .infinity : nil, minHeight: height, maxHeight: height)
            .background(Capsule().fill(danger ? AuraColor.danger.opacity(0.14)
                                       : quiet ? Color.clear : Color.white.opacity(0.06)))
            .overlay(Capsule().strokeBorder(.white.opacity(quiet ? 0 : 0.14), lineWidth: 1))
            .contentShape(Capsule())
        }
        .buttonStyle(PressStyle())
        .disabled(!enabled)
        .opacity(enabled ? 1 : 0.45)
    }
}

/// Круглая кнопка-иконка: микрофон, отправка, «назад».
struct RoundIconButton: View {
    let systemImage: String
    let label: String
    var accent = false
    var size: CGFloat = 44
    var enabled = true
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Image(systemName: systemImage)
                .font(.system(size: size * 0.4, weight: .medium))
                .foregroundStyle(accent ? Color.white : AuraColor.textSecondary)
                .frame(width: size, height: size)
                .background(
                    Circle().fill(accent ? AnyShapeStyle(LinearGradient(colors: [AuraColor.gradStart, AuraColor.gradEnd],
                                                                       startPoint: .topLeading, endPoint: .bottomTrailing))
                                         : AnyShapeStyle(Color.white.opacity(0.06)))
                )
                .overlay(Circle().strokeBorder(.white.opacity(accent ? 0.28 : 0.14), lineWidth: 1))
                .shadow(color: accent && enabled ? AuraColor.accent.opacity(0.5) : .clear, radius: 10)
        }
        .buttonStyle(PressStyle())
        .disabled(!enabled)
        .opacity(enabled ? 1 : 0.45)
        .accessibilityLabel(label)
    }
}

/// Пилюля-чип: быстрые ответы, фильтры, варианты окна переговоров.
struct AuraChip: View {
    let title: String
    var active = false
    var enabled = true
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(AuraFont.labelMedium)
                .foregroundStyle(AuraColor.textPrimary)
                .lineLimit(1)
                .padding(.horizontal, 15)
                .frame(height: 34)
                .background(Capsule().fill(active ? Color.white.opacity(0.14) : AuraColor.well.opacity(0.55)))
                .overlay(Capsule().strokeBorder(.white.opacity(active ? 0.38 : 0.2), lineWidth: 1))
        }
        .buttonStyle(PressStyle())
        .disabled(!enabled)
        .opacity(enabled ? 1 : 0.55)
    }
}

/// Ссылка-текст (входа и регистрации).
struct LinkButton: View {
    let title: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title).font(AuraFont.labelMedium).foregroundStyle(AuraColor.textPrimary)
        }
        .buttonStyle(.plain)
        .padding(.vertical, 4)
    }
}

// MARK: - Поле ввода

/// Поле v3: тёмное стекло, тонкая рамка, фиолетовая рамка в фокусе, иконка справа,
/// подпись над полем (макеты входа и регистрации).
struct AuraField: View {
    @Binding var text: String
    let placeholder: String
    var label = ""
    var systemImage: String?
    var onTrailing: (() -> Void)?
    var secure = false
    var filled = false
    var keyboard: UIKeyboardType = .default
    var submitLabel: SubmitLabel = .done
    var onSubmit: (() -> Void)?

    @FocusState private var focused: Bool

    init(text: Binding<String>, placeholder: String, label: String = "", systemImage: String? = nil,
         onTrailing: (() -> Void)? = nil, secure: Bool = false, filled: Bool = false,
         keyboard: UIKeyboardType = .default, submitLabel: SubmitLabel = .done,
         onSubmit: (() -> Void)? = nil) {
        _text = text
        self.placeholder = placeholder
        self.label = label
        self.systemImage = systemImage
        self.onTrailing = onTrailing
        self.secure = secure
        self.filled = filled
        self.keyboard = keyboard
        self.submitLabel = submitLabel
        self.onSubmit = onSubmit
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 7) {
            if !label.isEmpty {
                Text(label).font(AuraFont.labelMedium).foregroundStyle(AuraColor.textPrimary)
            }
            HStack(spacing: 10) {
                ZStack(alignment: .leading) {
                    if text.isEmpty {
                        Text(placeholder).font(AuraFont.body).foregroundStyle(AuraColor.textMuted).lineLimit(1)
                    }
                    Group {
                        if secure {
                            SecureField("", text: $text)
                        } else {
                            TextField("", text: $text)
                        }
                    }
                    .font(AuraFont.body)
                    .foregroundStyle(AuraColor.textPrimary)
                    .tint(AuraColor.lilac)
                    .keyboardType(keyboard)
                    .textInputAutocapitalization(keyboard == .emailAddress || secure ? .never : .sentences)
                    .autocorrectionDisabled(keyboard == .emailAddress || secure)
                    .submitLabel(submitLabel)
                    .focused($focused)
                    .onSubmit { onSubmit?() }
                }
                if let systemImage {
                    Image(systemName: systemImage)
                        .font(.system(size: 16))
                        .foregroundStyle(AuraColor.textSecondary)
                        .onTapGesture { onTrailing?() }
                }
            }
            .padding(.leading, 16)
            .padding(.trailing, 12)
            .frame(height: 48)
            .background(RoundedRectangle(cornerRadius: AuraRadius.field, style: .continuous)
                .fill(filled ? Color.white.opacity(0.09) : AuraColor.well.opacity(0.45)))
            .overlay(RoundedRectangle(cornerRadius: AuraRadius.field, style: .continuous)
                .strokeBorder(focused ? AuraColor.accent.opacity(0.8)
                              : filled ? Color.clear : Color.white.opacity(0.16), lineWidth: 1))
        }
    }
}

// MARK: - Аватары и эмблемы

/// Аватар-шар: цвет из имени, инициалы, опциональные кольцо и «в сети».
struct AuraAvatar: View {
    let name: String
    var size: CGFloat = 44
    var ring = false
    var online = false

    private var hue: Double {
        let value = name.unicodeScalars.reduce(0) { ($0 * 31 + Int($1.value)) % 360 }
        return Double(abs(value)) / 360
    }

    private var initials: String {
        let parts = name.split(whereSeparator: { $0.isWhitespace })
        switch parts.count {
        case 0: return "?"
        case 1: return String(parts[0].prefix(1)).uppercased()
        default: return (String(parts[0].prefix(1)) + String(parts[1].prefix(1))).uppercased()
        }
    }

    var body: some View {
        ZStack(alignment: .bottomTrailing) {
            ZStack {
                if ring {
                    Circle().strokeBorder(AuraColor.lilac.opacity(0.25), lineWidth: 3)
                        .frame(width: size + 10, height: size + 10)
                }
                Circle()
                    .fill(RadialGradient(colors: [Color(hue: hue, saturation: 0.45, brightness: 0.98),
                                                  Color(hue: hue, saturation: 0.62, brightness: 0.82),
                                                  Color(hue: hue + 0.08, saturation: 0.75, brightness: 0.5)],
                                         center: .topLeading, startRadius: 0, endRadius: size))
                    .overlay(Circle().strokeBorder(.white.opacity(0.2), lineWidth: 1))
                    .frame(width: size, height: size)
                Text(initials)
                    .font(AuraFont.manrope(size * 0.36, .bold))
                    .foregroundStyle(.white)
            }
            if online {
                Circle()
                    .fill(AuraColor.success)
                    .overlay(Circle().strokeBorder(AuraColor.bgPanel, lineWidth: 2))
                    .frame(width: size * 0.26, height: size * 0.26)
            }
        }
        .accessibilityHidden(true)
    }
}

/// Логотип Aura: кольцо из 12 изогнутых штрихов (геометрия иконки приложения).
struct AuraEmblem: View {
    var size: CGFloat = 72

    var body: some View {
        Canvas { context, canvas in
            let s = canvas.width / 100
            let center = CGPoint(x: canvas.width / 2, y: canvas.height / 2)
            context.fill(Path(ellipseIn: CGRect(x: center.x - 60 * s, y: center.y - 60 * s,
                                                width: 120 * s, height: 120 * s)),
                         with: .radialGradient(Gradient(colors: [AuraColor.lilac.opacity(0.35), .clear]),
                                               center: center, startRadius: 0, endRadius: 60 * s))
            let shading = GraphicsContext.Shading.linearGradient(
                Gradient(colors: [Color(hex: 0xF6E6FF), Color(hex: 0xB18CFF)]),
                startPoint: .zero, endPoint: CGPoint(x: canvas.width, y: canvas.height))
            for i in 0..<12 {
                var stroke = Path()
                stroke.move(to: CGPoint(x: 50 * s, y: 9 * s))
                stroke.addQuadCurve(to: CGPoint(x: 66 * s, y: 19 * s), control: CGPoint(x: 60 * s, y: 9.5 * s))
                let turn = CGAffineTransform(translationX: center.x, y: center.y)
                    .rotated(by: Double(i) * .pi / 6)
                    .translatedBy(x: -center.x, y: -center.y)
                context.stroke(stroke.applying(turn), with: shading,
                               style: StrokeStyle(lineWidth: 5 * s, lineCap: .round))
            }
        }
        .frame(width: size, height: size)
        .accessibilityHidden(true)
    }
}

/// Светящаяся сфера Ауры (шапка «Моей Ауры»).
struct AuraOrb: View {
    var size: CGFloat = 56

    var body: some View {
        ZStack {
            Circle()
                .fill(RadialGradient(colors: [AuraColor.accentSoft.opacity(0.45), .clear],
                                     center: .center, startRadius: 0, endRadius: size * 0.7))
                .frame(width: size * 1.4, height: size * 1.4)
            Circle()
                .fill(RadialGradient(stops: [.init(color: .white, location: 0),
                                             .init(color: Color(hex: 0xF3D9FF), location: 0.1),
                                             .init(color: Color(hex: 0xB98CFF), location: 0.34),
                                             .init(color: Color(hex: 0x5B3BD6), location: 0.62),
                                             .init(color: Color(hex: 0x1A1040), location: 1)],
                                     center: UnitPoint(x: 0.32, y: 0.28), startRadius: 0, endRadius: size * 0.55))
                .frame(width: size, height: size)
        }
        .frame(width: size, height: size)
        .accessibilityHidden(true)
    }
}

// MARK: - Текст

/// Заголовок экрана (38 bold) с действиями справа.
struct ScreenHeader<Actions: View>: View {
    let title: String
    @ViewBuilder var actions: () -> Actions

    init(_ title: String, @ViewBuilder actions: @escaping () -> Actions = { EmptyView() }) {
        self.title = title
        self.actions = actions
    }

    var body: some View {
        HStack(alignment: .center) {
            Text(title).font(AuraFont.screenTitle).foregroundStyle(AuraColor.textPrimary)
            Spacer(minLength: 8)
            HStack(spacing: 8) { actions() }
        }
    }
}

/// Заголовок секции с сиреневым индикатором слева (макет Settings).
struct SectionTitle: View {
    let text: String

    var body: some View {
        HStack(spacing: 10) {
            RoundedRectangle(cornerRadius: 2).fill(AuraColor.lilac).frame(width: 3, height: 18)
            Text(text).font(AuraFont.titleMedium).foregroundStyle(AuraColor.textPrimary)
        }
    }
}

/// Приглушённый пояснительный текст.
struct Muted: View {
    let text: String
    var alignment: TextAlignment = .leading

    init(_ text: String, alignment: TextAlignment = .leading) {
        self.text = text
        self.alignment = alignment
    }

    var body: some View {
        Text(text).font(AuraFont.small).foregroundStyle(AuraColor.textMuted).multilineTextAlignment(alignment)
    }
}

/// Пилюля-статус с цветной рамкой.
struct Pill: View {
    let text: String
    let color: Color

    var body: some View {
        Text(text)
            .font(AuraFont.labelMedium)
            .foregroundStyle(color)
            .padding(.horizontal, 12)
            .padding(.vertical, 5)
            .overlay(Capsule().strokeBorder(color.opacity(0.5), lineWidth: 1))
    }
}

/// Простая раскладка «в строку с переносом» для чипов (аналог FlowRow).
struct FlowLayout: Layout {
    var spacing: CGFloat = 8
    var alignment: HorizontalAlignment = .leading

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let rows = arrange(width: proposal.width ?? .infinity, subviews: subviews)
        let height = rows.reduce(0) { $0 + $1.height } + spacing * CGFloat(max(rows.count - 1, 0))
        let width = rows.map(\.width).max() ?? 0
        return CGSize(width: proposal.width ?? width, height: height)
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        var y = bounds.minY
        for row in arrange(width: bounds.width, subviews: subviews) {
            var x = alignment == .trailing ? bounds.maxX - row.width : bounds.minX
            for index in row.items {
                let size = subviews[index].sizeThatFits(.unspecified)
                subviews[index].place(at: CGPoint(x: x, y: y), proposal: ProposedViewSize(size))
                x += size.width + spacing
            }
            y += row.height + spacing
        }
    }

    private struct Row {
        var items: [Int] = []
        var width: CGFloat = 0
        var height: CGFloat = 0
    }

    private func arrange(width: CGFloat, subviews: Subviews) -> [Row] {
        var rows: [Row] = [Row()]
        for index in subviews.indices {
            let size = subviews[index].sizeThatFits(.unspecified)
            let extra = rows[rows.count - 1].items.isEmpty ? size.width : size.width + spacing
            if rows[rows.count - 1].width + extra > width, !rows[rows.count - 1].items.isEmpty {
                rows.append(Row())
            }
            var row = rows[rows.count - 1]
            row.width += row.items.isEmpty ? size.width : size.width + spacing
            row.height = max(row.height, size.height)
            row.items.append(index)
            rows[rows.count - 1] = row
        }
        return rows.filter { !$0.items.isEmpty }
    }
}
