// InkComponents.swift — общие элементы дизайна 2026: статус (значок + слово),
// бейдж тарифа, аватар с инициалами, заголовки секций, ячейки сгруппированных
// списков, кнопки и шапка экрана с крупным заголовком.
import SelfvoyKit
import SwiftUI

/// Статус — всегда значок и слово, не только цвет.
struct StatusBadge: View {
    let status: StatusView
    var compact = false

    var body: some View {
        HStack(spacing: 4) {
            Image(systemName: Ink.symbol(status.icon)).font(.system(size: compact ? 11 : 12, weight: .semibold))
            Text(status.label).font(.system(size: compact ? 12 : 13, weight: .semibold)).lineLimit(1)
        }
        .foregroundStyle(Ink.tone(status.tone))
        .padding(.horizontal, 8)
        .padding(.vertical, 4)
        .background(Capsule().fill(status.tone == .attention ? Ink.amber.opacity(0.18) : Ink.fill.opacity(0.6)))
        .accessibilityElement(children: .combine)
    }
}

/// Бейдж тарифа: «Pro до 12 ноября» / «Free».
struct PlanBadge: View {
    let plan: SelfvoyPlan?

    var body: some View {
        if let plan {
            let pro = plan.isPro()
            HStack(spacing: 4) {
                Image(systemName: pro ? "star.fill" : "star").font(.system(size: 11, weight: .bold))
                Text(plan.badge()).font(.system(size: 12, weight: .bold))
            }
            .foregroundStyle(pro ? Color.white : Ink.secondary)
            .padding(.horizontal, 9)
            .padding(.vertical, 4)
            .background(Capsule().fill(pro ? Ink.accent : Ink.fill))
            .accessibilityLabel("Тариф: \(plan.badge())")
        }
    }
}

/// Аватар с инициалами на мягком фоне (как в списке клиентов макета).
struct InkAvatar: View {
    let name: String
    var size: CGFloat = 40

    private var initials: String {
        let parts = name.split(whereSeparator: { $0.isWhitespace })
        switch parts.count {
        case 0: return "?"
        case 1: return String(parts[0].prefix(1)).uppercased()
        default: return (String(parts[0].prefix(1)) + String(parts[1].prefix(1))).uppercased()
        }
    }

    private var tint: Color {
        let value = name.unicodeScalars.reduce(0) { ($0 &* 31 &+ Int($1.value)) % 5 }
        return Ink.capsule(abs(value) + 1)
    }

    var body: some View {
        Text(initials)
            .font(.system(size: size * 0.36, weight: .bold))
            .foregroundStyle(tint)
            .frame(width: size, height: size)
            .background(Circle().fill(tint.opacity(0.16)))
            .accessibilityHidden(true)
    }
}

/// Шапка экрана: строка-эпиграф («ЧЕТВЕРГ, 22 ОКТЯБРЯ»), крупный заголовок, действия справа.
struct InkHeader<Trailing: View>: View {
    var eyebrow = ""
    let title: String
    @ViewBuilder var trailing: () -> Trailing

    init(eyebrow: String = "", title: String, @ViewBuilder trailing: @escaping () -> Trailing = { EmptyView() }) {
        self.eyebrow = eyebrow
        self.title = title
        self.trailing = trailing
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack(alignment: .center) {
                Text(eyebrow.uppercased())
                    .font(InkFont.eyebrow)
                    .foregroundStyle(Ink.secondary)
                Spacer(minLength: 8)
                HStack(spacing: 8) { trailing() }
            }
            .frame(minHeight: 40)
            Text(title)
                .font(InkFont.largeTitle)
                .foregroundStyle(Ink.label)
                .accessibilityAddTraits(.isHeader)
        }
    }
}

/// Круглая стеклянная кнопка шапки (поиск, «+», колокольчик).
struct InkRoundButton: View {
    let systemImage: String
    let label: String
    var badge = 0
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            ZStack(alignment: .topTrailing) {
                Image(systemName: systemImage)
                    .font(.system(size: 16, weight: .semibold))
                    .foregroundStyle(Ink.label)
                    .frame(width: 38, height: 38)
                    .background(Circle().fill(Ink.card))
                if badge > 0 {
                    Text(badge > 99 ? "99+" : "\(badge)")
                        .font(.system(size: 10, weight: .bold))
                        .foregroundStyle(.black)
                        .padding(.horizontal, 4)
                        .frame(minWidth: 16, minHeight: 16)
                        .background(Capsule().fill(Ink.amber))
                        .offset(x: 4, y: -2)
                }
            }
        }
        .buttonStyle(.plain)
        .accessibilityLabel(badge > 0 ? "\(label): \(badge)" : label)
    }
}

/// Заголовок секции сгруппированного списка («ОПЛАТА И ОТМЕНА»).
struct InkSectionHeader: View {
    let title: String
    var trailing = ""

    var body: some View {
        HStack {
            Text(title).font(InkFont.title).foregroundStyle(Ink.label)
            Spacer()
            if !trailing.isEmpty {
                Text(trailing).font(InkFont.footnote).foregroundStyle(Ink.secondary)
            }
        }
        .padding(.top, 8)
    }
}

/// Ячейка-ссылка: значок в цветном квадрате, подпись, значение, шеврон.
struct InkCell: View {
    let symbol: String
    let tint: Color
    let title: String
    var value = ""
    var badge = 0
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 12) {
                Image(systemName: symbol)
                    .font(.system(size: 15, weight: .semibold))
                    .foregroundStyle(.white)
                    .frame(width: 30, height: 30)
                    .background(RoundedRectangle(cornerRadius: 8, style: .continuous).fill(tint))
                Text(title).font(InkFont.body).foregroundStyle(Ink.label)
                Spacer(minLength: 8)
                if badge > 0 {
                    Text("\(badge)").font(.system(size: 13, weight: .bold)).foregroundStyle(.black)
                        .padding(.horizontal, 7).padding(.vertical, 2)
                        .background(Capsule().fill(Ink.amber))
                }
                if !value.isEmpty {
                    Text(value).font(InkFont.callout).foregroundStyle(Ink.secondary).lineLimit(1)
                }
                Image(systemName: "chevron.right").font(.system(size: 13, weight: .semibold)).foregroundStyle(Ink.tertiary)
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 11)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

/// Группа ячеек на одной карточке с разделителями.
struct InkGroup<Content: View>: View {
    @ViewBuilder var content: () -> Content

    var body: some View {
        VStack(spacing: 0) { content() }
            .background(RoundedRectangle(cornerRadius: 18, style: .continuous).fill(Ink.card))
            .clipShape(RoundedRectangle(cornerRadius: 18, style: .continuous))
    }
}

struct InkDivider: View {
    var inset: CGFloat = 58

    var body: some View {
        Rectangle().fill(Ink.separator).frame(height: 0.5).padding(.leading, inset)
    }
}

/// Основная кнопка: акцентная капсула во всю ширину.
struct InkPrimaryButtonStyle: ButtonStyle {
    var tint: Color = Ink.accent

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 17, weight: .semibold))
            .foregroundStyle(.white)
            .frame(maxWidth: .infinity, minHeight: 50)
            .background(Capsule().fill(tint))
            .opacity(configuration.isPressed ? 0.8 : 1)
    }
}

/// Вторичная кнопка: мягкая заливка.
struct InkSecondaryButtonStyle: ButtonStyle {
    var tint: Color = Ink.accent

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 15, weight: .semibold))
            .foregroundStyle(tint)
            .padding(.horizontal, 14)
            .frame(minHeight: 40)
            .background(Capsule().fill(tint.opacity(0.12)))
            .opacity(configuration.isPressed ? 0.7 : 1)
    }
}

/// Плашка ошибки внутри экрана/окна (pro_required — с подсказкой про приглашение).
struct InkNotice: View {
    let text: String
    var tone: StatusTone = .red

    var body: some View {
        HStack(alignment: .top, spacing: 8) {
            Image(systemName: tone == .red ? "exclamationmark.triangle.fill" : "info.circle.fill")
            Text(text).font(InkFont.footnote).frame(maxWidth: .infinity, alignment: .leading)
        }
        .foregroundStyle(Ink.tone(tone))
        .padding(12)
        .background(RoundedRectangle(cornerRadius: 14, style: .continuous).fill(Ink.tone(tone).opacity(0.12)))
    }
}

/// Пустое состояние.
struct InkEmpty: View {
    let text: String

    var body: some View {
        Text(text)
            .font(InkFont.callout)
            .foregroundStyle(Ink.secondary)
            .frame(maxWidth: .infinity, alignment: .leading)
            .inkCard()
    }
}
