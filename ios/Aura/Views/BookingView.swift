// BookingView.swift — «Запись»: страницы записи владельца (booking.*).
// Постоянная ссылка #/b/<slug>: люди без Aura сами выбирают свободное время
// (страница гостя — в веб-клиенте). Здесь — создать/изменить страницу, пауза,
// удаление, ссылка (копировать/поделиться) и записи гостей с отменой.
import AuraKit
import SwiftUI
import UIKit

struct BookingView: View {

    @EnvironmentObject private var store: AppStore
    @State private var editing: BookingPage?
    @State private var creating = false

    var body: some View {
        VStack(spacing: 0) {
            SubscreenBar {
                if !creating && editing == nil {
                    GlassButton(title: "Новая", systemImage: "plus", height: 38, fullWidth: false) { creating = true }
                }
            }
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    ScreenHeader("Запись")
                    Muted("Постоянная ссылка: люди без Aura сами выбирают свободное время в вашем расписании — можно сразу серией раз в неделю.")
                    if creating || editing != nil {
                        BookingPageForm(page: editing) {
                            creating = false
                            editing = nil
                        }
                        .id(editing?.id ?? 0)
                    }
                    SectionTitle(text: "Страницы").padding(.top, 6)
                    if let pages = store.bookingPages {
                        if pages.isEmpty && !creating {
                            Muted("Создайте первую страницу — например, «Консультация, 30 минут» — и отправьте ссылку клиентам.")
                        }
                        ForEach(pages) { page in
                            BookingPageCard(page: page) {
                                creating = false
                                editing = page
                            }
                        }
                    } else {
                        Muted("Загружаем…")
                    }
                    SectionTitle(text: "Записи гостей").padding(.top, 6)
                    if store.bookings.isEmpty {
                        Muted("Здесь появятся люди, которые записались по вашим ссылкам.")
                    }
                    ForEach(store.bookings) { booking in
                        BookingRowView(booking: booking)
                    }
                }
                .padding(20)
            }
        }
        .onAppear { store.loadBooking() }
    }
}

private struct BookingPageForm: View {

    @EnvironmentObject private var store: AppStore
    let page: BookingPage?
    let onClose: () -> Void

    @State private var title = ""
    @State private var details = ""
    @State private var duration = 30
    @State private var days = 14
    @State private var perDay = "4"
    @State private var notice = "2"
    @State private var series = "1"
    @State private var weekends = false
    @State private var problem: String?
    @State private var busy = false

    private let durations = [(15, "15 мин"), (30, "30 мин"), (45, "45 мин"), (60, "1 час"), (90, "1,5 часа")]
    private let horizons = [(7, "Неделя"), (14, "2 недели"), (30, "Месяц"), (60, "2 месяца")]

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(page == nil ? "Новая страница записи" : "Изменить страницу")
                .font(AuraFont.titleSmall).foregroundStyle(AuraColor.textPrimary)
            AuraField(text: $title, placeholder: "Например, консультация", label: "Название")
            AuraField(text: $details, placeholder: "Что будет на встрече, где и как", label: "Описание для гостей")
            Muted("Длительность")
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 8) {
                    ForEach(durations, id: \.0) { item in
                        AuraChip(title: item.1, active: duration == item.0) { duration = item.0 }
                    }
                }
            }
            Muted("Запись открыта на")
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 8) {
                    ForEach(horizons, id: \.0) { item in
                        AuraChip(title: item.1, active: days == item.0) { days = item.0 }
                    }
                }
            }
            AuraField(text: $perDay, placeholder: "4", label: "Встреч в день, не больше", keyboard: .numberPad)
            AuraField(text: $notice, placeholder: "2", label: "Запас до встречи, часов", keyboard: .numberPad)
            AuraField(text: $series, placeholder: "1", label: "Серия: до скольких встреч раз в неделю", keyboard: .numberPad)
            Toggle(isOn: $weekends) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Выходные").font(AuraFont.titleSmall).foregroundStyle(AuraColor.textPrimary)
                    Muted(weekends ? "Будни и выходные" : "Только будни")
                }
            }
            if let problem {
                Text(problem).font(AuraFont.small).foregroundStyle(AuraColor.danger)
            }
            HStack(spacing: 10) {
                GlassButton(title: busy ? "Сохраняем…" : (page == nil ? "Создать" : "Сохранить"), enabled: !busy) { save() }
                GlassButton(title: "Отмена", quiet: true) { onClose() }
            }
        }
        .glassCard(padding: 16)
        .onAppear {
            guard let fields = page?.fields else { return }
            title = fields.title
            details = fields.description
            duration = fields.durationMinutes
            days = fields.daysAhead
            perDay = String(fields.maxPerDay)
            notice = String(fields.noticeHours)
            series = String(fields.maxSeries)
            weekends = fields.weekends
        }
    }

    private func save() {
        let fields = BookingPageFields(
            title: title, description: details, durationMinutes: duration, daysAhead: days,
            maxPerDay: Int(perDay) ?? 0, noticeHours: Int(notice) ?? -1, maxSeries: Int(series) ?? 0,
            weekends: weekends)
        problem = fields.validate()
        guard problem == nil else { return }
        busy = true
        // Состояние View (@State busy, onClose) — только на главном потоке.
        Task { @MainActor in
            let saved = await store.saveBookingPage(id: page?.id, fields: fields)
            busy = false
            if saved { onClose() }
        }
    }
}

private struct BookingPageCard: View {

    @EnvironmentObject private var store: AppStore
    let page: BookingPage
    let onEdit: () -> Void
    @State private var confirmDelete = false
    @State private var copied = false

    private var facts: String {
        var parts = ["\(page.fields.durationMinutes) мин",
                     "на \(page.fields.daysAhead) \(Format.plural(page.fields.daysAhead, "день", "дня", "дней")) вперёд",
                     "до \(page.fields.maxPerDay) в день"]
        if page.fields.maxSeries > 1 { parts.append("серия до \(page.fields.maxSeries)") }
        parts.append(page.fields.weekends ? "и в выходные" : "только будни")
        return parts.joined(separator: " · ")
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text(page.fields.title).font(AuraFont.titleSmall).foregroundStyle(AuraColor.textPrimary)
                Spacer(minLength: 8)
                Text(page.status == "active" ? "Открыта" : "На паузе")
                    .font(AuraFont.small)
                    .foregroundStyle(page.status == "active" ? AuraColor.textSecondary : AuraColor.danger)
            }
            Muted(facts)
            if !page.fields.description.isEmpty { Muted(page.fields.description) }
            Text(page.link).font(AuraFont.small).foregroundStyle(AuraColor.textPrimary).textSelection(.enabled)
            if page.url.isEmpty {
                Muted("Сервер не знает свой публичный адрес (AURA_PUBLIC_URL) — полную ссылку соберите из адреса веб-клиента.")
            }
            Muted(page.upcoming > 0
                  ? "Предстоит: \(page.upcoming) \(Format.plural(page.upcoming, "встреча", "встречи", "встреч"))"
                  : "Пока никто не записался")
            HStack(spacing: 8) {
                GlassButton(title: copied ? "Скопировано" : "Копировать", systemImage: "doc.on.doc", height: 40) {
                    UIPasteboard.general.string = page.link
                    copied = true
                }
                ShareLink(item: page.link) {
                    Label("Поделиться", systemImage: "square.and.arrow.up")
                        .font(AuraFont.small)
                        .frame(maxWidth: .infinity, minHeight: 40)
                }
                .foregroundStyle(AuraColor.textPrimary)
            }
            if confirmDelete {
                HStack(spacing: 8) {
                    Muted("Удалить страницу?")
                    Spacer(minLength: 4)
                    GlassButton(title: "Удалить", danger: true, height: 38, fullWidth: false) {
                        store.deleteBookingPage(page)
                        confirmDelete = false
                    }
                    GlassButton(title: "Нет", quiet: true, height: 38, fullWidth: false) { confirmDelete = false }
                }
            } else {
                HStack(spacing: 8) {
                    GlassButton(title: "Изменить", systemImage: "pencil", quiet: true, height: 38) { onEdit() }
                    if page.status == "active" {
                        GlassButton(title: "Пауза", systemImage: "pause", quiet: true, height: 38) {
                            store.setBookingPagePaused(page, paused: true)
                        }
                    } else {
                        GlassButton(title: "Открыть", systemImage: "play", quiet: true, height: 38) {
                            store.setBookingPagePaused(page, paused: false)
                        }
                    }
                    GlassButton(title: "", systemImage: "trash", quiet: true, height: 38, fullWidth: false) {
                        confirmDelete = true
                    }
                }
            }
        }
        .glassCard(padding: 16)
    }
}

private struct BookingRowView: View {

    @EnvironmentObject private var store: AppStore
    let booking: Booking
    @State private var confirming = false

    private var upcoming: [BookingOccurrence] {
        booking.occurrences.filter { occurrence in
            occurrence.active && (Format.parseISO(occurrence.start).map { $0 > Date() } ?? false)
        }
    }

    private var statusText: String {
        if booking.status == "cancelled" { return booking.cancelledBy == "owner" ? "Отменили вы" : "Отменил гость" }
        return upcoming.isEmpty ? "Прошла" : "Предстоит"
    }

    var body: some View {
        let shown = Array((upcoming.isEmpty ? booking.occurrences : upcoming).prefix(3))
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text("\(booking.guestName) · \(booking.pageTitle)")
                    .font(AuraFont.titleSmall).foregroundStyle(AuraColor.textPrimary)
                Spacer(minLength: 8)
                Text(statusText).font(AuraFont.small)
                    .foregroundStyle(booking.status == "cancelled" ? AuraColor.danger : AuraColor.textSecondary)
            }
            Muted(shown.map { "\(Format.dayPhrase($0.start)), \(Format.timeOf($0.start))" }.joined(separator: " · ")
                  + (booking.reschedules > 0 ? " · переносов: \(booking.reschedules)" : ""))
            if !booking.guestEmail.isEmpty { Muted(booking.guestEmail) }
            if !booking.guestComment.isEmpty { Muted("«\(booking.guestComment)»") }
            if !upcoming.isEmpty && booking.status == "active" {
                if confirming {
                    HStack(spacing: 8) {
                        Muted("Отменить все встречи?")
                        Spacer(minLength: 4)
                        GlassButton(title: "Да", danger: true, height: 36, fullWidth: false) {
                            store.cancelBooking(booking)
                            confirming = false
                        }
                        GlassButton(title: "Нет", quiet: true, height: 36, fullWidth: false) { confirming = false }
                    }
                } else {
                    GlassButton(title: "Отменить запись", quiet: true, height: 36, fullWidth: false) { confirming = true }
                }
            }
        }
        .glassCard(padding: 16)
    }
}
