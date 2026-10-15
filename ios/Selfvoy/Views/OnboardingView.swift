// OnboardingView.swift — анкета после регистрации: день рождения, аллергии,
// диета, передвижение, город, бюджет. Всё необязательно («Пропустить»), но
// заполненная анкета сразу попадает в контекст Selfvoy (prefs.set → AI context).
import SwiftUI

struct OnboardingView: View {

    @EnvironmentObject private var store: AppStore
    @State private var birthday = ""
    @State private var allergies = ""
    @State private var city = ""
    @State private var budget = ""
    @State private var diet = Set<String>()
    @State private var transport = "walk"

    private let dietOptions = [("vegan", "Веган"), ("vegetarian", "Вегетарианец"), ("gluten_free", "Без глютена"),
                               ("lactose_free", "Без лактозы"), ("halal", "Халяль"), ("kosher", "Кошер")]
    private let transportOptions = [("walk", "Пешком"), ("bike", "Велосипед"), ("transit", "Транспорт"),
                                    ("car", "Машина")]

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                VStack(spacing: 14) {
                    SelfvoyEmblem(size: 56)
                    Text("Расскажите о себе")
                        .font(SelfvoyFont.headline)
                        .foregroundStyle(SelfvoyColor.textPrimary)
                    Muted("Selfvoy использует это для мест, времени и рекомендаций. Всё необязательно и меняется в настройках.",
                          alignment: .center)
                }
                .frame(maxWidth: .infinity)
                SelfvoyField(text: $birthday, placeholder: "ГГГГ-ММ-ДД", label: "День рождения",
                          keyboard: .numbersAndPunctuation)
                SelfvoyField(text: $allergies, placeholder: "через запятую", label: "Аллергии")
                VStack(alignment: .leading, spacing: 8) {
                    Text("Диета").font(SelfvoyFont.labelMedium).foregroundStyle(SelfvoyColor.textSecondary)
                    FlowLayout(spacing: 8) {
                        ForEach(dietOptions.indices, id: \.self) { index in
                            let option = dietOptions[index]
                            SelfvoyChip(title: option.1, active: diet.contains(option.0)) {
                                if diet.contains(option.0) { diet.remove(option.0) } else { diet.insert(option.0) }
                            }
                        }
                    }
                }
                VStack(alignment: .leading, spacing: 8) {
                    Text("Передвижение").font(SelfvoyFont.labelMedium).foregroundStyle(SelfvoyColor.textSecondary)
                    FlowLayout(spacing: 8) {
                        ForEach(transportOptions.indices, id: \.self) { index in
                            let option = transportOptions[index]
                            SelfvoyChip(title: option.1, active: transport == option.0) { transport = option.0 }
                        }
                    }
                }
                SelfvoyField(text: $city, placeholder: "Например, Москва", label: "Город")
                SelfvoyField(text: $budget, placeholder: "Например, 25", label: "Бюджет на встречу", keyboard: .decimalPad)
                GradientButton(title: "Сохранить", busy: store.busy) {
                    store.submitOnboarding(
                        birthday: birthday.trimmingCharacters(in: .whitespaces),
                        allergies: allergies.split(separator: ",")
                            .map { $0.trimmingCharacters(in: .whitespaces) }
                            .filter { !$0.isEmpty },
                        diet: dietOptions.map { $0.0 }.filter { diet.contains($0) },
                        transport: transport,
                        city: city.trimmingCharacters(in: .whitespaces),
                        budget: budget.trimmingCharacters(in: .whitespaces))
                }
                GlassButton(title: "Пропустить", quiet: true) { store.skipOnboarding() }
            }
            .glassCard(radius: 34, padding: 24)
            .padding(20)
            .frame(maxWidth: 560)
            .frame(maxWidth: .infinity)
        }
        .scrollDismissesKeyboard(.interactively)
        .onAppear {
            let saved = store.prefs.string("city")
            if city.isEmpty, !saved.isEmpty { city = saved }
        }
    }
}
