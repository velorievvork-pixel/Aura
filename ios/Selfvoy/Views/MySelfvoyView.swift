// MySelfvoyView.swift — «Мой Selfvoy» (макет Chat): реплики пользователя слева,
// ответы Selfvoy справа. Опасные действия (письмо, сообщение, бронь) Selfvoy сам
// не выполняет: под ответом кнопки «Подтвердить / Отклонить».
import SwiftUI

struct MySelfvoyView: View {

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
                        if store.busy && store.dialog.last?.fromSelfvoy == false {
                            Muted("Selfvoy думает…")
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
            SelfvoyOrb(size: 52)
            VStack(alignment: .leading, spacing: 2) {
                Text("Selfvoy").font(SelfvoyFont.title).foregroundStyle(SelfvoyColor.textPrimary)
                Text((store.busy ? "Думает" : "В сети") + " · Персональный ассистент")
                    .font(SelfvoyFont.small)
                    .foregroundStyle(SelfvoyColor.textSecondary)
            }
            Spacer()
            RoundIconButton(systemImage: "brain", label: "Память", size: 42) {
                store.loadMemory()
                showMemory = true
            }
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 14)
        .background(LinearGradient(colors: [SelfvoyColor.lilac.opacity(0.16), .clear],
                                   startPoint: .top, endPoint: .bottom))
    }

    private var intro: some View {
        VStack(alignment: .trailing, spacing: 10) {
            Bubble(fromSelfvoy: true,
                   text: "Привет, \(Format.firstName(store.userName))! Я ваш Selfvoy. Скажите, что нужно сделать, "
                       + "и я договорюсь с Selfvoy других людей, поставлю напоминание или подготовлю письмо.")
            FlowLayout(spacing: 8, alignment: .trailing) {
                ForEach(suggestions, id: \.self) { text in
                    SelfvoyChip(title: text) { send(text) }
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
            SelfvoyField(text: $draft, placeholder: "Напишите что-нибудь…", submitLabel: .send, onSubmit: { send(draft) })
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

/// Реплика диалога с кнопками решения под ответом Selfvoy.
private struct DialogRow: View {

    @EnvironmentObject private var store: AppStore
    let entry: DialogEntry
    let pending: Set<Int64>

    var body: some View {
        HStack {
            if entry.fromSelfvoy { Spacer(minLength: 40) }
            Bubble(fromSelfvoy: entry.fromSelfvoy, text: entry.text, error: entry.error) {
                ForEach(entry.actions, id: \.confirmationId) { action in
                    let open = pending.contains(action.confirmationId)
                    Text((open ? "Ждёт вашего решения: " : "Решено: ") + action.summary)
                        .font(SelfvoyFont.small)
                        .foregroundStyle(open ? SelfvoyColor.warning : SelfvoyColor.textSecondary)
                    if open {
                        HStack(spacing: 8) {
                            SelfvoyChip(title: "Подтвердить") {
                                store.decideConfirmation(id: action.confirmationId, approve: true)
                            }
                            SelfvoyChip(title: "Отклонить") {
                                store.decideConfirmation(id: action.confirmationId, approve: false)
                            }
                        }
                    }
                }
                if entry.hasA2a {
                    SelfvoyChip(title: "Открыть переговоры") { store.selectTab(.negotiations) }
                }
            }
            if !entry.fromSelfvoy { Spacer(minLength: 40) }
        }
    }
}

/// Реплика: пользователь — тёмное стекло, Selfvoy — светлое стекло (макет Chat).
struct Bubble<Extra: View>: View {
    let fromSelfvoy: Bool
    let text: String
    var error = false
    @ViewBuilder var extra: () -> Extra

    init(fromSelfvoy: Bool, text: String, error: Bool = false,
         @ViewBuilder extra: @escaping () -> Extra = { EmptyView() }) {
        self.fromSelfvoy = fromSelfvoy
        self.text = text
        self.error = error
        self.extra = extra
    }

    var body: some View {
        let shape = RoundedRectangle(cornerRadius: SelfvoyRadius.bubble, style: .continuous)
        VStack(alignment: .leading, spacing: 10) {
            Text(text)
                .font(SelfvoyFont.body)
                .foregroundStyle(error ? SelfvoyColor.danger : SelfvoyColor.textPrimary)
                .fixedSize(horizontal: false, vertical: true)
            extra()
        }
        .padding(.horizontal, 18)
        .padding(.vertical, 14)
        .frame(maxWidth: 320, alignment: .leading)
        // «Жидкое стекло»: системный материал с размытием, тонировка (у Selfvoy —
        // сиреневая), блик сверху и светлый кант по верхнему краю.
        .background {
            shape.fill(.ultraThinMaterial)
                .overlay(shape.fill(fromSelfvoy ? Color(hex: 0x3C2C64).opacity(0.38) : Color(hex: 0x120E1E).opacity(0.45)))
                .overlay(shape.fill(LinearGradient(
                    colors: [.white.opacity(fromSelfvoy ? 0.24 : 0.15), .white.opacity(0.04), .white.opacity(0.09)],
                    startPoint: .top, endPoint: .bottom)))
                .shadow(color: fromSelfvoy ? SelfvoyColor.accentSoft.opacity(0.35) : .black.opacity(0.4), radius: 16, y: 8)
        }
        .overlay(shape.strokeBorder(error
            ? AnyShapeStyle(SelfvoyColor.danger.opacity(0.6))
            : AnyShapeStyle(LinearGradient(colors: [.white.opacity(0.45), .white.opacity(0.06)],
                                           startPoint: .top, endPoint: .bottom)),
            lineWidth: 1))
        .environment(\.colorScheme, .dark)
    }
}

/// «Что Selfvoy знает о вас»: город из анкеты и факты из разговоров.
private struct MemorySheet: View {

    @EnvironmentObject private var store: AppStore
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        ZStack {
            SelfvoyColor.bgElevated.ignoresSafeArea()
            ScrollView {
                VStack(alignment: .leading, spacing: 10) {
                    HStack {
                        Text("Что Selfvoy знает о вас").font(SelfvoyFont.titleMedium).foregroundStyle(SelfvoyColor.textPrimary)
                        Spacer()
                        Button("Закрыть") { dismiss() }.font(SelfvoyFont.labelMedium)
                    }
                    Muted("Город")
                    let city = store.prefs.string("city")
                    Text(city.isEmpty ? "—" : city).font(SelfvoyFont.body).foregroundStyle(SelfvoyColor.textPrimary)
                    Muted("Факты из разговоров")
                    if store.memory.isEmpty {
                        Text("Пока ничего").font(SelfvoyFont.body).foregroundStyle(SelfvoyColor.textSecondary)
                    }
                    ForEach(store.memory.prefix(12), id: \.self) { entry in
                        Text("• \(entry.text)").font(SelfvoyFont.small).foregroundStyle(SelfvoyColor.textSecondary)
                    }
                    Muted("Другие Selfvoy видят только, свободны ли вы в предложенное время, а не ваш календарь и не эти записи.")
                        .padding(.top, 8)
                }
                .padding(24)
            }
        }
        .presentationDetents([.medium, .large])
    }
}
