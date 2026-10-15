// VoiceOverlay.swift — голосовой запрос: расшифровка в реальном времени,
// по окончании текст уходит Selfvoy (SpeechService.onFinalTranscript).
import SwiftUI

struct VoiceOverlay: View {

    @EnvironmentObject private var store: AppStore
    @ObservedObject private var speech: SpeechService

    init(speech: SpeechService) {
        _speech = ObservedObject(wrappedValue: speech)
    }

    var body: some View {
        VStack(spacing: 16) {
            SelfvoyOrb(size: 64)
                .scaleEffect(speech.state == .listening ? 1.08 : 1)
                .animation(.easeInOut(duration: 0.8).repeatForever(autoreverses: true), value: speech.state)
            Text(speech.transcript.isEmpty ? (speech.state == .listening ? "Слушаю…" : "Нажмите на микрофон")
                 : speech.transcript)
                .font(SelfvoyFont.manrope(17))
                .foregroundStyle(SelfvoyColor.textPrimary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: .infinity)
            HStack(spacing: 16) {
                RoundIconButton(systemImage: "xmark", label: "Закрыть", size: 48) {
                    speech.cancelListening()
                    store.voiceOverlayVisible = false
                }
                RoundIconButton(systemImage: speech.state == .listening ? "stop.fill" : "mic.fill",
                                label: speech.state == .listening ? "Готово" : "Слушать", accent: true, size: 64) {
                    if speech.state == .listening {
                        speech.stopListening()
                    } else {
                        speech.startListening()
                    }
                }
            }
        }
        .glassCard(radius: SelfvoyRadius.shell, glow: true, padding: 24)
        .padding(.horizontal, 16)
        .padding(.bottom, 90)
        .onAppear {
            if speech.state == .idle { speech.startListening() }
        }
    }
}
