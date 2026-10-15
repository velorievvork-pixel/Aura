// TasksView.swift — задачи (макет Tasks): неделя, задачи выбранного дня,
// «Дальше», «Выполнено», подсказка «до следующего события» и «+» для новой
// задачи (tasks.create с due_at). Долгое нажатие — перенос в «Напоминания» iOS.
import SwiftUI

struct TasksView: View {

    @EnvironmentObject private var store: AppStore
    @State private var day = Calendar.current.startOfDay(for: Date())
    @State private var adding = false

    /// Совместимость с тестами и EventKit: ISO-строка сервера → Date.
    static func parseISO(_ text: String) -> Date? {
        Format.parseISO(text)
    }

    private var calendar: Calendar { Calendar.current }

    private var week: [Date] {
        var iso = Calendar(identifier: .iso8601)
        iso.timeZone = calendar.timeZone
        let monday = iso.dateInterval(of: .weekOfYear, for: day)?.start ?? day
        return (0..<7).compactMap { calendar.date(byAdding: .day, value: $0, to: monday) }
    }

    private var open: [SelfvoyTask] {
        store.tasks.filter { $0.status == "pending" }
            .sorted { ($0.whenAt.isEmpty ? "9999" : $0.whenAt) < ($1.whenAt.isEmpty ? "9999" : $1.whenAt) }
    }

    private func dateOf(_ task: SelfvoyTask) -> Date? {
        Format.parseISO(task.whenAt).map { calendar.startOfDay(for: $0) }
    }

    var body: some View {
        let onDay = open.filter { (dateOf($0) ?? calendar.startOfDay(for: Date())) == day }
        let later = open.filter { (dateOf($0) ?? .distantPast) > day }
        let done = Array(store.tasks.filter(\.isDone).prefix(6))
        let next = open.first { (Format.parseISO($0.whenAt) ?? .distantPast) > Date() }

        ZStack(alignment: .bottomTrailing) {
            ScrollView {
                VStack(alignment: .leading, spacing: 20) {
                    ScreenHeader("Задачи") {
                        GradientButton(title: "Сегодня", height: 40, fullWidth: false) {
                            day = calendar.startOfDay(for: Date())
                        }
                    }
                    weekSwitcher
                    weekStrip
                    hint(next)
                    section(calendar.isDateInToday(day) ? "Сегодня"
                            : "\(Format.dayShort(day)), \(Format.dateLabel(day))", onDay, empty: "Свободный день")
                    section("Дальше", later, empty: "Больше ничего не запланировано")
                    section("Выполнено", done, empty: "Пока ничего")
                    Spacer(minLength: 80)
                }
                .padding(.horizontal, 20)
                .padding(.vertical, 16)
            }
            RoundIconButton(systemImage: "plus", label: "Новая задача", accent: true, size: 60) { adding = true }
                .padding(20)
        }
        .sheet(isPresented: $adding) { AddTaskSheet(day: day) }
    }

    private var weekSwitcher: some View {
        HStack(spacing: 12) {
            RoundIconButton(systemImage: "chevron.left", label: "Предыдущая неделя", size: 34) {
                day = calendar.date(byAdding: .day, value: -7, to: day) ?? day
            }
            Text("\(Format.dateLabel(week.first ?? day)) – \(Format.dateLabel(week.last ?? day))")
                .font(SelfvoyFont.labelMedium)
                .foregroundStyle(SelfvoyColor.textPrimary)
            RoundIconButton(systemImage: "chevron.right", label: "Следующая неделя", size: 34) {
                day = calendar.date(byAdding: .day, value: 7, to: day) ?? day
            }
        }
    }

    private var weekStrip: some View {
        HStack(spacing: 4) {
            ForEach(week, id: \.self) { date in
                let selected = date == day
                let has = open.contains { dateOf($0) == date }
                Button {
                    day = date
                } label: {
                    VStack(spacing: 2) {
                        Text(Format.dayShort(date)).font(SelfvoyFont.micro).foregroundStyle(SelfvoyColor.textMuted)
                        Text("\(calendar.component(.day, from: date))")
                            .font(SelfvoyFont.manrope(20, .bold))
                            .foregroundStyle(calendar.isDateInToday(date) ? SelfvoyColor.lilac : SelfvoyColor.textPrimary)
                        Circle().fill(has ? SelfvoyColor.accentSoft : .clear).frame(width: 5, height: 5)
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 8)
                    .background(RoundedRectangle(cornerRadius: 14, style: .continuous)
                        .fill(selected ? SelfvoyColor.accent.opacity(0.2) : .clear))
                    .overlay(RoundedRectangle(cornerRadius: 14, style: .continuous)
                        .strokeBorder(selected ? SelfvoyColor.lilac.opacity(0.45) : .clear, lineWidth: 1))
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
            }
        }
    }

    /// Подсказка Selfvoy: сколько осталось до следующего события.
    private func hint(_ next: SelfvoyTask?) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Подсказки Selfvoy").font(SelfvoyFont.manrope(20, .bold)).foregroundStyle(SelfvoyColor.textPrimary)
            Text(next.map { "«\($0.title)» \(Format.untilText($0.whenAt))" } ?? "Впереди ничего не запланировано.")
                .font(SelfvoyFont.titleSmall)
                .foregroundStyle(SelfvoyColor.textPrimary)
            if let next, let at = Format.parseISO(next.whenAt) {
                let minutes = at.timeIntervalSinceNow / 60
                let progress = min(1, max(0.05, 1 - minutes / 120))
                GeometryReader { geo in
                    ZStack(alignment: .leading) {
                        Capsule().fill(.white.opacity(0.08))
                        Capsule().fill(SelfvoyColor.cta).frame(width: geo.size.width * progress)
                    }
                }
                .frame(height: 6)
                Muted("Время до следующего события")
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .glassCard()
    }

    private func section(_ title: String, _ items: [SelfvoyTask], empty: String) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(title).font(SelfvoyFont.title).foregroundStyle(SelfvoyColor.textPrimary)
            if items.isEmpty { Muted(empty) }
            ForEach(items) { task in
                TaskRow(task: task, day: day)
            }
        }
    }
}

private struct TaskRow: View {

    @EnvironmentObject private var store: AppStore
    let task: SelfvoyTask
    let day: Date

    var body: some View {
        let at = Format.parseISO(task.whenAt)
        let tone: Color = {
            if task.isDone { return SelfvoyColor.success }
            guard let at else { return SelfvoyColor.accentSoft }
            if at < Date() { return SelfvoyColor.danger }
            if at.timeIntervalSinceNow < 120 * 60 { return SelfvoyColor.warning }
            return SelfvoyColor.accentSoft
        }()
        let shape = RoundedRectangle(cornerRadius: 16, style: .continuous)
        return HStack(spacing: 12) {
            Button {
                store.setTaskStatus(id: task.id, status: task.isDone ? "reopen" : "complete")
            } label: {
                ZStack {
                    RoundedRectangle(cornerRadius: 7).fill(task.isDone ? SelfvoyColor.accent : .clear)
                    RoundedRectangle(cornerRadius: 7)
                        .strokeBorder(task.isDone ? SelfvoyColor.accent : .white.opacity(0.3), lineWidth: 1.5)
                    if task.isDone {
                        Image(systemName: "checkmark").font(.system(size: 11, weight: .bold)).foregroundStyle(.white)
                    }
                }
                .frame(width: 22, height: 22)
            }
            .buttonStyle(.plain)
            .accessibilityLabel(task.isDone ? "Вернуть в работу" : "Выполнено")
            Text(task.title)
                .font(SelfvoyFont.titleSmall)
                .foregroundStyle(SelfvoyColor.textPrimary)
                .strikethrough(task.isDone)
                .lineLimit(1)
            Spacer(minLength: 4)
            if let at {
                // День недели — только внутри показанной недели, иначе «11 октября».
                let inWeek = Calendar(identifier: .iso8601).isDate(at, equalTo: day, toGranularity: .weekOfYear)
                Muted(Calendar.current.isDate(at, inSameDayAs: day)
                      ? Format.timeOf(task.whenAt)
                      : "\(inWeek ? Format.dayShort(at) : Format.dateLabel(at)), \(Format.timeOf(task.whenAt))")
            }
            Button {
                store.deleteTask(id: task.id)
            } label: {
                Image(systemName: "trash").font(.system(size: 14)).foregroundStyle(SelfvoyColor.textMuted)
            }
            .buttonStyle(.plain)
            .accessibilityLabel("Удалить")
            Circle().fill(tone).frame(width: 10, height: 10)
        }
        .padding(.horizontal, 16)
        .frame(height: 56)
        .background(shape.fill(LinearGradient(colors: [.white.opacity(0.09), SelfvoyColor.accent.opacity(0.08),
                                                       .white.opacity(0.03)],
                                              startPoint: .leading, endPoint: .trailing)))
        .overlay(shape.strokeBorder(.white.opacity(0.16), lineWidth: 1))
        .opacity(task.isDone ? 0.62 : 1)
        .contextMenu {
            Button {
                Task {
                    let error = await EventKitService.shared.exportTask(title: task.title, notes: task.notes,
                                                                        dueAt: Format.parseISO(task.whenAt))
                    if let error {
                        store.errorMessage = error
                    } else {
                        store.setStatus("Задача добавлена в «Напоминания»")
                    }
                }
            } label: {
                Label("В «Напоминания» iOS", systemImage: "checklist")
            }
        }
    }
}

/// Новая задача на выбранный день: название и время.
private struct AddTaskSheet: View {

    @EnvironmentObject private var store: AppStore
    @Environment(\.dismiss) private var dismiss
    let day: Date
    @State private var title = ""
    @State private var time: Date

    init(day: Date) {
        self.day = day
        _time = State(initialValue: Calendar.current.date(bySettingHour: 18, minute: 0, second: 0, of: day) ?? day)
    }

    var body: some View {
        ZStack {
            SelfvoyColor.bgElevated.ignoresSafeArea()
            VStack(alignment: .leading, spacing: 16) {
                Text("Новая задача · \(Format.dateLabel(day))")
                    .font(SelfvoyFont.titleMedium)
                    .foregroundStyle(SelfvoyColor.textPrimary)
                SelfvoyField(text: $title, placeholder: "Например, отправить питч-дек", label: "Что сделать")
                DatePicker("Время", selection: $time, displayedComponents: .hourAndMinute)
                    .font(SelfvoyFont.labelMedium)
                    .foregroundStyle(SelfvoyColor.textPrimary)
                GradientButton(title: "Добавить", enabled: !title.trimmingCharacters(in: .whitespaces).isEmpty) {
                    let parts = Calendar.current.dateComponents([.hour, .minute], from: time)
                    let due = Calendar.current.date(bySettingHour: parts.hour ?? 18, minute: parts.minute ?? 0,
                                                    second: 0, of: day) ?? time
                    store.createTask(title: title, dueAt: Format.isoString(due))
                    dismiss()
                }
                GlassButton(title: "Отмена", quiet: true) { dismiss() }
                Spacer()
            }
            .padding(24)
        }
        .presentationDetents([.medium])
    }
}
