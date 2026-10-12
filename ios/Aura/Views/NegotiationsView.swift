// NegotiationsView.swift — переговоры Аур (макет Meeting arrangement).
//
// Один agent.negotiate — один раунд; шаги строятся из ответа сервера и
// появляются по очереди. Бронь — только по кнопке пользователя. После пяти
// раундов без согласия решает человек (docs/A2A_PROTOCOL.md).
import SwiftUI

private struct Step: Hashable {
    let mine: Bool
    let text: String
    let detail: String
    var done = false
    var fail = false
}

struct NegotiationsView: View {

    @EnvironmentObject private var store: AppStore
    @State private var composing = false
    @State private var revealed = 0

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                HStack(alignment: .center) {
                    Text("Организация\nвстречи").font(AuraFont.headline).foregroundStyle(AuraColor.textPrimary)
                    Spacer()
                    if store.negotiation != nil && !composing {
                        GlassButton(title: "Новые", systemImage: "plus", height: 40, fullWidth: false) {
                            composing = true
                        }
                    }
                }
                if let n = store.negotiation, !composing {
                    progress(n)
                } else {
                    NegotiationForm(onCancel: store.negotiation == nil ? nil : { composing = false }) {
                        composing = false
                    }
                }
            }
            .padding(.horizontal, 20)
            .padding(.vertical, 16)
        }
        .scrollDismissesKeyboard(.interactively)
    }

    @ViewBuilder
    private func progress(_ n: Negotiation) -> some View {
        let me = Format.firstName(store.userName)
        let contact = store.contacts.first { $0.email == n.target || $0.name == n.target }
        let them = Format.firstName(contact?.name ?? n.target)
        let steps = buildSteps(n, me: me, them: them)
        let shown = store.revealedNegotiations.contains(n.id) ? steps.count : revealed

        HStack(spacing: 10) {
            Pill(text: statusText(n), color: n.booking != nil ? AuraColor.success
                 : !n.accepted ? AuraColor.warning : AuraColor.lilac)
            Pill(text: "Раунд \(n.round) из \(AppStore.maxRounds)", color: AuraColor.textPrimary)
        }
        HStack(spacing: 12) {
            AgentCard(name: me, aura: "Аура \(Format.genitive(me))",
                      talking: shown < steps.count && steps[shown].mine, them: false)
            AgentCard(name: them, aura: "Аура \(Format.genitive(them))",
                      talking: shown < steps.count && !steps[shown].mine, them: true)
        }
        if !n.topic.isEmpty {
            Muted("Тема: \(n.topic)", alignment: .center).frame(maxWidth: .infinity)
        }
        ForEach(Array(steps.prefix(shown).enumerated()), id: \.offset) { index, step in
            StepRow(number: index + 1, step: step)
                .transition(.opacity.combined(with: .move(edge: .bottom)))
        }
        .task(id: n.id) {
            guard !store.revealedNegotiations.contains(n.id) else { return }
            revealed = 0
            while revealed < steps.count {
                try? await Task.sleep(nanoseconds: 800_000_000)
                if Task.isCancelled { return }
                withAnimation(.easeOut(duration: 0.3)) { revealed += 1 }
            }
            store.revealedNegotiations.insert(n.id)
        }
        if shown >= steps.count {
            if n.accepted, n.slot != nil {
                ResultCard(negotiation: n)
            } else {
                counterOffers(n)
            }
        }
        if store.negotiationHistory.count > 1 {
            Text("Раунды за сессию").font(AuraFont.titleSmall).foregroundStyle(AuraColor.textPrimary)
            ForEach(store.negotiationHistory.dropFirst()) { h in
                Muted("\(h.target) · раунд \(h.round): "
                      + (h.accepted ? "согласовано, \(h.place?.name ?? "")" : "без согласия"))
            }
        }
    }

    private func statusText(_ n: Negotiation) -> String {
        if store.busy { return "Идут" }
        if n.booking != nil { return "Забронировано" }
        return n.accepted ? "Ждёт решения" : "Нужен ваш выбор"
    }

    private func counterOffers(_ n: Negotiation) -> some View {
        let exhausted = n.round >= AppStore.maxRounds
        return VStack(alignment: .leading, spacing: 12) {
            Text(exhausted ? "Лимит раундов исчерпан" : "Встречные варианты собеседника")
                .font(AuraFont.titleMedium)
                .foregroundStyle(AuraColor.textPrimary)
            Text(exhausted
                 ? "Ауры провели \(AppStore.maxRounds) раундов без согласия. Дальше решаете вы: напишите собеседнику."
                 : "Без вас Аура дальше не договаривается: можно начать следующий раунд с другим окном или написать в чат.")
                .font(AuraFont.small)
                .foregroundStyle(AuraColor.textSecondary)
            if !n.counterSlots.isEmpty {
                FlowLayout(spacing: 8) {
                    ForEach(n.counterSlots, id: \.self) { slot in
                        AuraChip(title: "\(Format.dayPhrase(slot.start)), \(Format.timeOf(slot.start))",
                                 enabled: false) {}
                    }
                }
            }
            if !exhausted {
                GradientButton(title: "Следующий раунд: две недели", busy: store.busy) {
                    store.negotiate(target: n.target, topic: n.topic, durationMinutes: n.durationMinutes,
                                    windowHours: Format.windowHours("week"))
                }
            }
            GlassButton(title: "Написать собеседнику", systemImage: "bubble.left") {
                store.openChat(contact: n.target)
            }
        }
        .glassCard()
    }

    private func buildSteps(_ n: Negotiation, me: String, them: String) -> [Step] {
        let auraMe = "Аура \(Format.genitive(me))"
        let auraThem = "Аура \(Format.genitive(them))"
        var steps = [Step(mine: true, text: "\(auraMe) предложила встречу: \(n.durationMinutes) мин",
                          detail: "окно: \(Format.windowPhrase(n.windowHours))")]
        if n.accepted, let slot = n.slot {
            steps.append(Step(mine: false, text: "\(auraThem) проверила календарь",
                              detail: "общее окно: \(Format.dayPhrase(slot.start)) в \(Format.timeOf(slot.start))"))
            var facts: [String] = []
            if let place = n.place {
                if !place.city.isEmpty { facts.append(place.city) }
                if !place.tags.isEmpty { facts.append(Format.placeTags(place.tags)) }
                if !place.rating.isEmpty { facts.append("★ \(place.rating)") }
            }
            steps.append(Step(mine: false, text: "\(auraThem) предложила \(n.place?.name ?? "место")",
                              detail: facts.joined(separator: " · ")))
            steps.append(Step(mine: true, text: "\(auraMe) приняла место и время", detail: n.message, done: true))
        } else {
            steps.append(Step(mine: false, text: "\(auraThem) проверила календарь",
                              detail: "в этом окне общего времени нет"))
            steps.append(Step(mine: false,
                              text: n.counterSlots.isEmpty ? "Встречных вариантов нет"
                                  : "Встречные варианты: \(n.counterSlots.count)",
                              detail: n.message.isEmpty ? "нужно ваше решение" : n.message, fail: true))
        }
        return steps
    }
}

/// Карточка агента: аватар с кольцом и «голос» — полосы активнее у того, кто говорит.
private struct AgentCard: View {
    let name: String
    let aura: String
    let talking: Bool
    let them: Bool

    var body: some View {
        let shape = RoundedRectangle(cornerRadius: 22, style: .continuous)
        VStack(spacing: 4) {
            AuraAvatar(name: name, size: 56, ring: true)
                .padding(.bottom, 6)
            Text(name).font(AuraFont.titleMedium).foregroundStyle(AuraColor.textPrimary).lineLimit(1)
            Text(aura).font(AuraFont.small).foregroundStyle(AuraColor.textSecondary).lineLimit(1)
            HStack(alignment: .bottom, spacing: 2) {
                ForEach(0..<18, id: \.self) { i in
                    let level = 0.25 + abs(sin(Double(i) * 1.7 + (them ? 2 : 0))) * 0.65
                    RoundedRectangle(cornerRadius: 2)
                        .fill(LinearGradient(colors: [AuraColor.accent, Color(hex: 0x6366F1)],
                                             startPoint: .top, endPoint: .bottom))
                        .frame(height: 36 * (talking ? level : level * 0.35))
                        .animation(.easeInOut(duration: 0.35), value: talking)
                }
            }
            .frame(height: 36, alignment: .bottom)
            .padding(8)
            .frame(maxWidth: .infinity)
            .background(RoundedRectangle(cornerRadius: 12, style: .continuous).fill(.white.opacity(0.86)))
            .padding(.top, 8)
        }
        .padding(14)
        .frame(maxWidth: .infinity)
        .background(shape.fill(LinearGradient(
            colors: them ? [Color(hex: 0xBF99FF, alpha: 0.2), Color(hex: 0x7352DB, alpha: 0.1)]
                         : [Color(hex: 0x99ABFF, alpha: 0.18), Color(hex: 0x7859DB, alpha: 0.1)],
            startPoint: .top, endPoint: .bottom)))
        .overlay(shape.strokeBorder(them ? Color(hex: 0xCCAAFF, alpha: 0.42) : Color(hex: 0xB3BDFF, alpha: 0.36),
                                    lineWidth: 1))
    }
}

private struct StepRow: View {
    let number: Int
    let step: Step

    var body: some View {
        HStack(spacing: 10) {
            if step.mine { badge } else { Spacer(minLength: 24) }
            VStack(alignment: step.mine ? .leading : .trailing, spacing: 2) {
                Text(step.text).font(AuraFont.labelMedium).foregroundStyle(AuraColor.textPrimary)
                if !step.detail.isEmpty {
                    Text(step.detail).font(AuraFont.micro).foregroundStyle(AuraColor.textMuted)
                }
            }
            .multilineTextAlignment(step.mine ? .leading : .trailing)
            .padding(.horizontal, 14)
            .padding(.vertical, 9)
            .background(RoundedRectangle(cornerRadius: 20, style: .continuous).fill(.white.opacity(0.05)))
            .overlay(RoundedRectangle(cornerRadius: 20, style: .continuous).strokeBorder(.white.opacity(0.18), lineWidth: 1))
            if step.mine { Spacer(minLength: 24) } else { badge }
        }
    }

    private var badge: some View {
        let color = step.fail ? AuraColor.danger : AuraColor.accentSoft
        return ZStack {
            Circle().fill((step.fail ? AuraColor.danger : AuraColor.accent).opacity(0.15))
            Circle().strokeBorder(color, lineWidth: 1.5)
            if step.done {
                Image(systemName: "checkmark").font(.system(size: 13, weight: .bold)).foregroundStyle(AuraColor.textPrimary)
            } else {
                Text("\(number)").font(AuraFont.label).foregroundStyle(AuraColor.textPrimary)
            }
        }
        .frame(width: 32, height: 32)
    }
}

/// Итог: день, время, место и «Подтвердить и забронировать».
private struct ResultCard: View {

    @EnvironmentObject private var store: AppStore
    let negotiation: Negotiation

    var body: some View {
        let n = negotiation
        let slot = n.slot ?? Slot(start: "", end: "")
        var facts = ["Двое", "до \(Format.timeOf(slot.end))"]
        if let city = n.place?.city, !city.isEmpty { facts.append(city) }
        if let rating = n.place?.rating, !rating.isEmpty { facts.append("рейтинг \(rating)") }
        let shape = RoundedRectangle(cornerRadius: 24, style: .continuous)
        return VStack(alignment: .leading, spacing: 10) {
            Text("\(Format.dayTitle(slot.start)), \(Format.timeOf(slot.start))\n\(n.place?.name ?? "")")
                .font(AuraFont.headline)
                .foregroundStyle(AuraColor.textPrimary)
            Muted(facts.joined(separator: "  ·  "))
            if let booking = n.booking {
                Text("Столик забронирован · код \(booking) · встреча в задачах")
                    .font(AuraFont.labelMedium)
                    .foregroundStyle(AuraColor.success)
                GlassButton(title: "Открыть задачи") { store.selectTab(.tasks) }
            } else {
                GradientButton(title: "Подтвердить и забронировать", systemImage: "checkmark.seal",
                               busy: store.busy) {
                    store.bookNegotiation()
                }
                GlassButton(title: "Остановить", quiet: true) { store.clearNegotiation() }
            }
        }
        .padding(22)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(shape.fill(LinearGradient(colors: [Color(hex: 0x7863FF, alpha: 0.18), Color(hex: 0x3D78FF, alpha: 0.12)],
                                              startPoint: .leading, endPoint: .trailing)))
        .overlay(shape.strokeBorder((n.booking != nil ? AuraColor.success : AuraColor.accent).opacity(0.45), lineWidth: 1))
    }
}

/// Форма нового раунда: с кем, о чём, когда и сколько.
private struct NegotiationForm: View {

    @EnvironmentObject private var store: AppStore
    let onCancel: (() -> Void)?
    let onStart: () -> Void

    @State private var target = ""
    @State private var email = ""
    @State private var topic = ""
    @State private var window = "weekend"
    @State private var duration = 60

    private struct WindowOption {
        let key: String
        let title: String
    }

    private static let windows = [
        WindowOption(key: "tomorrow", title: "сегодня–завтра"),
        WindowOption(key: "weekend", title: "до конца недели"),
        WindowOption(key: "week", title: "две недели")
    ]

    init(onCancel: (() -> Void)?, onStart: @escaping () -> Void) {
        self.onCancel = onCancel
        self.onStart = onStart
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("С кем договориться?").font(AuraFont.titleMedium).foregroundStyle(AuraColor.textPrimary)
            if !store.contacts.isEmpty {
                FlowLayout(spacing: 8) {
                    ForEach(store.contacts) { contact in
                        AuraChip(title: contact.name, active: target == contact.email && email.isEmpty) {
                            target = contact.email
                            email = ""
                        }
                    }
                }
            }
            AuraField(text: $email, placeholder: "или email собеседника", systemImage: "envelope",
                      keyboard: .emailAddress)
                .onChange(of: email) {
                    if !email.trimmingCharacters(in: .whitespaces).isEmpty { target = email }
                }
            AuraField(text: $topic, placeholder: "Тема, например «обсудить стартап»")
            VStack(alignment: .leading, spacing: 8) {
                Text("Когда").font(AuraFont.labelMedium).foregroundStyle(AuraColor.textSecondary)
                FlowLayout(spacing: 8) {
                    ForEach(Self.windows, id: \.key) { option in
                        AuraChip(title: option.title, active: window == option.key) { window = option.key }
                    }
                }
                Text("Длительность").font(AuraFont.labelMedium).foregroundStyle(AuraColor.textSecondary)
                HStack(spacing: 8) {
                    ForEach([30, 60, 90], id: \.self) { minutes in
                        AuraChip(title: "\(minutes) мин", active: duration == minutes) { duration = minutes }
                    }
                }
            }
            GradientButton(title: "Начать переговоры", systemImage: "person.2.wave.2",
                           enabled: !target.isEmpty && !store.busy, busy: store.busy) {
                store.negotiate(target: target, topic: topic, durationMinutes: duration,
                                windowHours: Format.windowHours(window))
                onStart()
            }
            if let onCancel {
                GlassButton(title: "Отмена", quiet: true, action: onCancel)
            }
            Muted("Ауры сверят свободное время и подберут место с учётом питания обоих. Бронь только после вашего подтверждения.")
        }
        .glassCard(radius: 26, padding: 22)
        .onAppear {
            if target.isEmpty, let first = store.contacts.first { target = first.email }
        }
    }
}
