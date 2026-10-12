// MyAuraView.swift — «Моя Аура» (макет Chat): реплики пользователя слева,
// ответы Ауры справа. Опасные действия (письмо, сообщение, бронь) Аура сама
// не выполняет: под ответом кнопки «Подтвердить / Отклонить».
import SwiftUI

struct MyAuraView: View {

    @EnvironmentObject private var store: AppStore
    @State private var draft = ""
    @State private var showMemory = false

    private let suggestions = [
        "Хочу встретиться с Димой в эти выходные и обсудить стартап",
        "Что у меня сегодня в расписании?",
        "Напомни завтра в 10 позвонить юристу"
    ]

    var body: some View {
        VStack(spacing: 0) {
            header
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(spacing: 14) {
                        if store.dialog.isEmpty { intro }
                        ForEach(store.dialog) { entry in
                            DialogRow(entry: entry, pending: Set(store.confirmations.map(\.id)))
                                .id(entry.id)
                        }
                        if store.busy && store.dialog.last?.fromAura == false {
                            Muted("Аура думает…")
                                .frame(maxWidth: .infinity, alignment: .trailing)
                                .id("thinking")
                        }
                    }
                    .padding(.horizontal, 16)
                    .padding(.vertical, 18)
                }
                .scrollDismissesKeyboard(.interactively)
                .onChange(of: store.dialog.count) {
                    if let last = store.dialog.last {
                        withAnimation(.easeOut(duration: 0.25)) { proxy.scrollTo(last.id, anchor: .bottom) }
                    }
                }
            }
            inputBar
        }
        .sheet(isPresented: $showMemory) { MemorySheet() }
    }

    private var header: some View {
        HStack(spacing: 14) {
            AuraOrb(size: 52)
            VStack(alignment: .leading, spacing: 2) {
                Text("Aura").font(AuraFont.title).foregroundStyle(AuraColor.textPrimary)
                Text((store.busy ? "Думает" : "В сети") + " · Персональный ассистент")
                    .font(AuraFont.small)
                    .foregroundStyle(AuraColor.textSecondary)
            }
            Spacer()
            RoundIconButton(systemImage: "brain", label: "Память", size: 42) {
                store.loadMemory()
                showMemory = true
            }
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 14)
        .background(LinearGradient(colors: [AuraColor.lilac.opacity(0.16), .clear],
                                   startPoint: .top, endPoint: .bottom))
    }

    private var intro: some View {
        VStack(alignment: .trailing, spacing: 10) {
            Bubble(fromAura: true,
                   text: "Привет, \(Format.firstName(store.userName))! Я ваша Аура. Скажите, что нужно сделать, "
                       + "и я договорюсь с Аурами других людей, поставлю напоминание или подготовлю письмо.")
            FlowLayout(spacing: 8, alignment: .trailing) {
                ForEach(suggestions, id: \.self) { text in
                    AuraChip(title: text) { send(text) }
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .trailing)
    }

    private var inputBar: some View {
        HStack(spacing: 10) {
            RoundIconButton(systemImage: "mic", label: "Сказать голосом", size: 48) {
                store.voiceOverlayVisible = true
            }
            AuraField(text: $draft, placeholder: "Напишите что-нибудь…", submitLabel: .send, onSubmit: { send(draft) })
            RoundIconButton(systemImage: "paperplane.fill", label: "Отправить", accent: true, size: 50,
                            enabled: !draft.trimmingCharacters(in: .whitespaces).isEmpty && !store.busy) {
                send(draft)
            }
        }
        .padding(.horizontal, 16)
        .padding(.top, 6)
        .padding(.bottom, 12)
    }

    private func send(_ text: String) {
        guard !text.trimmingCharacters(in: .whitespaces).isEmpty, !store.busy else { return }
        store.ask(text)
        draft = ""
    }
}

/// Реплика диалога с кнопками решения под ответом Ауры.
private struct DialogRow: View {

    @EnvironmentObject private var store: AppStore
    let entry: DialogEntry
    let pending: Set<Int64>

    var body: some View {
        HStack {
            if entry.fromAura { Spacer(minLength: 40) }
            Bubble(fromAura: entry.fromAura, text: entry.text, error: entry.error) {
                ForEach(entry.actions, id: \.confirmationId) { action in
                    let open = pending.contains(action.confirmationId)
                    Text((open ? "Ждёт вашего решения: " : "Решено: ") + action.summary)
                        .font(AuraFont.small)
                        .foregroundStyle(open ? AuraColor.warning : AuraColor.textSecondary)
                    if open {
                        HStack(spacing: 8) {
                            AuraChip(title: "Подтвердить") {
                                store.decideConfirmation(id: action.confirmationId, approve: true)
                            }
                            AuraChip(title: "Отклонить") {
                                store.decideConfirmation(id: action.confirmationId, approve: false)
                            }
                        }
                    }
                }
                if entry.hasA2a {
                    AuraChip(title: "Открыть переговоры") { store.selectTab(.negotiations) }
                }
            }
            if !entry.fromAura { Spacer(minLength: 40) }
        }
    }
}

/// Реплика: пользователь — тёмное стекло, Аура — светлое стекло (макет Chat).
struct Bubble<Extra: View>: View {
    let fromAura: Bool
    let text: String
    var error = false
    @ViewBuilder var extra: () -> Extra

    init(fromAura: Bool, text: String, error: Bool = false,
         @ViewBuilder extra: @escaping () -> Extra = { EmptyView() }) {
        self.fromAura = fromAura
        self.text = text
        self.error = error
        self.extra = extra
    }

    var body: some View {
        let shape = RoundedRectangle(cornerRadius: AuraRadius.bubble, style: .continuous)
        VStack(alignment: .leading, spacing: 10) {
            Text(text)
                .font(AuraFont.body)
                .foregroundStyle(error ? AuraColor.danger : AuraColor.textPrimary)
                .fixedSize(horizontal: false, vertical: true)
            extra()
        }
        .padding(.horizontal, 18)
        .padding(.vertical, 14)
        .frame(maxWidth: 320, alignment: .leading)
        // «Жидкое стекло»: системный материал с размытием, тонировка (у Ауры —
        // сиреневая), блик сверху и светлый кант по верхнему краю.
        .background {
            shape.fill(.ultraThinMaterial)
                .overlay(shape.fill(fromAura ? Color(hex: 0x3C2C64).opacity(0.38) : Color(hex: 0x120E1E).opacity(0.45)))
                .overlay(shape.fill(LinearGradient(
                    colors: [.white.opacity(fromAura ? 0.24 : 0.15), .white.opacity(0.04), .white.opacity(0.09)],
                    startPoint: .top, endPoint: .bottom)))
                .shadow(color: fromAura ? AuraColor.accentSoft.opacity(0.35) : .black.opacity(0.4), radius: 16, y: 8)
        }
        .overlay(shape.strokeBorder(error
            ? AnyShapeStyle(AuraColor.danger.opacity(0.6))
            : AnyShapeStyle(LinearGradient(colors: [.white.opacity(0.45), .white.opacity(0.06)],
                                           startPoint: .top, endPoint: .bottom)),
            lineWidth: 1))
        .environment(\.colorScheme, .dark)
    }
}

/// «Что Аура знает о вас»: город из анкеты и факты из разговоров.
private struct MemorySheet: View {

    @EnvironmentObject private var store: AppStore
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        ZStack {
            AuraColor.bgElevated.ignoresSafeArea()
            ScrollView {
                VStack(alignment: .leading, spacing: 10) {
                    HStack {
                        Text("Что Аура знает о вас").font(AuraFont.titleMedium).foregroundStyle(AuraColor.textPrimary)
                        Spacer()
                        Button("Закрыть") { dismiss() }.font(AuraFont.labelMedium)
                    }
                    Muted("Город")
                    let city = store.prefs.string("city")
                    Text(city.isEmpty ? "—" : city).font(AuraFont.body).foregroundStyle(AuraColor.textPrimary)
                    Muted("Факты из разговоров")
                    if store.memory.isEmpty {
                        Text("Пока ничего").font(AuraFont.body).foregroundStyle(AuraColor.textSecondary)
                    }
                    ForEach(store.memory.prefix(12), id: \.self) { entry in
                        Text("• \(entry.text)").font(AuraFont.small).foregroundStyle(AuraColor.textSecondary)
                    }
                    Muted("Другие Ауры видят только, свободны ли вы в предложенное время, а не ваш календарь и не эти записи.")
                        .padding(.top, 8)
                }
                .padding(24)
            }
        }
        .presentationDetents([.medium, .large])
    }
}
