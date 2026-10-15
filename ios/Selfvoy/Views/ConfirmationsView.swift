// ConfirmationsView.swift — опасные действия Selfvoy, ждущие решения человека
// (confirmation.approve / confirmation.deny). Сам Selfvoy их не выполняет.
import SwiftUI

struct ConfirmationsView: View {

    @EnvironmentObject private var store: AppStore

    var body: some View {
        VStack(spacing: 0) {
            SubscreenBar()
            list
        }
    }

    private var list: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                ScreenHeader("Подтверждения")
                Text("Письма, сообщения и брони Selfvoy не выполняет сам: каждое такое действие ждёт здесь вашего решения.")
                    .font(SelfvoyFont.body)
                    .foregroundStyle(SelfvoyColor.textSecondary)
                if store.confirmations.isEmpty {
                    Muted("Сейчас ничего не ждёт решения.")
                }
                ForEach(store.confirmations) { action in
                    VStack(alignment: .leading, spacing: 10) {
                        HStack(spacing: 8) {
                            Circle().fill(SelfvoyColor.warning).frame(width: 8, height: 8)
                            Text("Требуется подтверждение").font(SelfvoyFont.labelMedium).foregroundStyle(SelfvoyColor.warning)
                        }
                        Text(action.summary.isEmpty ? action.tool : action.summary)
                            .font(SelfvoyFont.titleSmall)
                            .foregroundStyle(SelfvoyColor.textPrimary)
                        Muted("инструмент: \(Format.toolLabel(action.tool))")
                        HStack(spacing: 10) {
                            GradientButton(title: "Подтвердить", height: 44) {
                                store.decideConfirmation(id: action.id, approve: true)
                            }
                            GlassButton(title: "Отклонить", danger: true, height: 44) {
                                store.decideConfirmation(id: action.id, approve: false)
                            }
                        }
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .glassCard()
                }
            }
            .padding(.horizontal, 20)
            .padding(.top, 6)
            .padding(.bottom, 20)
        }
        .refreshable { await store.loadConfirmations() }
    }
}
