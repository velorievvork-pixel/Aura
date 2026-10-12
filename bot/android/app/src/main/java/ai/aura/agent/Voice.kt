package ai.aura.agent

import android.content.Context
import android.media.MediaRecorder
import android.os.Build
import android.speech.tts.TextToSpeech
import java.io.File
import java.util.Locale

/** Запись голосового в m4a (AAC) — формат, который принимает Whisper-совместимый STT. */
class Recorder(private val context: Context) {
    private var recorder: MediaRecorder? = null
    private var file: File? = null

    fun start(): Boolean {
        val f = File(context.cacheDir, "voice_${System.currentTimeMillis()}.m4a")
        val r = if (Build.VERSION.SDK_INT >= 31) MediaRecorder(context) else @Suppress("DEPRECATION") MediaRecorder()
        return try {
            r.setAudioSource(MediaRecorder.AudioSource.MIC)
            r.setOutputFormat(MediaRecorder.OutputFormat.MPEG_4)
            r.setAudioEncoder(MediaRecorder.AudioEncoder.AAC)
            r.setAudioSamplingRate(16000)
            r.setAudioEncodingBitRate(64000)
            r.setMaxDuration(5 * 60 * 1000)
            r.setOutputFile(f.absolutePath)
            r.prepare()
            r.start()
            recorder = r
            file = f
            true
        } catch (e: Exception) {
            r.release()
            f.delete()
            false
        }
    }

    /** null — запись слишком короткая или не удалась. */
    fun stop(): File? {
        val r = recorder ?: return null
        val f = file
        recorder = null
        file = null
        return try {
            r.stop()
            f?.takeIf { it.length() > 0 }
        } catch (e: RuntimeException) {
            f?.delete()
            null
        } finally {
            r.release()
        }
    }

    fun cancel() {
        stop()?.delete()
    }
}

/** Озвучка ответов агента на устройстве (системный TextToSpeech). */
class Speaker(context: Context) : TextToSpeech.OnInitListener {
    private val tts = TextToSpeech(context.applicationContext, this)
    private var ready = false

    override fun onInit(status: Int) {
        ready = status == TextToSpeech.SUCCESS
        if (ready) tts.language = Locale("ru", "RU")
    }

    fun speak(text: String) {
        if (ready) tts.speak(text.take(3900), TextToSpeech.QUEUE_FLUSH, null, "aura-${System.nanoTime()}")
    }

    fun stop() {
        tts.stop()
    }

    fun shutdown() {
        tts.shutdown()
    }
}
