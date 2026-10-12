// NotificationsView.swift — входящие уведомления (notifications.list/read)
// и устройства, на которые приходят push (devices.push.*).
import SwiftUI

struct NotificationsView: View {

    @EnvironmentObject private var store: AppStore

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 10) {
                // «Прочитать все» — в строке «назад»: рядом с заголовком 38 pt ему не хватает места.
                HStack {
                    BackToHomeButton()
                    Spacer()
                    if store.notices.contains(where: { !$0.read }) {
                        GlassButton(title: "Прочитать все", height: 38, fullWidth: false) {
                            store.markNotificationsRead()
                        }
                    }
                }
                ScreenHeader("Уведомления")
                if store.notices.isEmpty {
                    Muted("Пока пусто.")
                }
                ForEach(store.notices) { notice in
                    Button {
                        if !notice.read { store.markNotificationsRead(id: notice.id) }
                    } label: {
                        HStack(alignment: .top, spacing: 12) {
                            Circle()
                                .fill(notice.read ? AuraColor.textSecondary.opacity(0.3) : AuraColor.status(notice.kind))
                                .frame(width: 9, height: 9)
                                .padding(.top, 5)
                            VStack(alignment: .leading, spacing: 3) {
                                Text(notice.title).font(AuraFont.titleSmall).foregroundStyle(AuraColor.textPrimary)
                                if !notice.body.isEmpty {
                                    Text(notice.body).font(AuraFont.small).foregroundStyle(AuraColor.textSecondary)
                                }
                                Muted("\(Format.dayPhrase(notice.createdAt)), \(Format.timeOf(notice.createdAt))")
                            }
                            Spacer(minLength: 0)
                        }
                        .multilineTextAlignment(.leading)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .glassCard(padding: 16)
                    }
                    .buttonStyle(PressStyle())
                }
                SectionTitle(text: "Push-устройства").padding(.top, 12)
                if store.pushDevices.isEmpty {
                    Muted("Push на этом устройстве ещё не зарегистрирован: разрешите уведомления в настройках iOS.")
                }
                ForEach(store.pushDevices) { device in
                    HStack {
                        Text("\(device.platform.uppercased()) · \(device.enabled ? "включено" : "выключено")")
                            .font(AuraFont.small)
                            .foregroundStyle(AuraColor.textPrimary)
                        Spacer()
                        GlassButton(title: "Отозвать", quiet: true, height: 36, fullWidth: false) {
                            store.revokePushDevice(id: device.id)
                        }
                    }
                }
            }
            .padding(20)
        }
        .refreshable { await store.loadNotifications() }
        .onAppear { store.loadPushDevices() }
    }
}
