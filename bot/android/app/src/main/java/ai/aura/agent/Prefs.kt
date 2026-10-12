package ai.aura.agent

import android.content.Context
import android.content.SharedPreferences
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey

/** Настройки клиента. Токен сессии — только в EncryptedSharedPreferences (ключ в Android Keystore). */
class Prefs(context: Context) {
    private val sp: SharedPreferences = open(context.applicationContext)

    var token: String?
        get() = sp.getString("token", null)
        set(v) = sp.edit().putString("token", v).apply()

    var serverUrl: String
        get() = sp.getString("server", null) ?: BuildConfig.DEFAULT_API_URL
        set(v) = sp.edit().putString("server", v.trim().trimEnd('/')).apply()

    var speakReplies: Boolean
        get() = sp.getBoolean("speak", false)
        set(v) = sp.edit().putBoolean("speak", v).apply()

    var fcmToken: String?
        get() = sp.getString("fcm", null)
        set(v) = sp.edit().putString("fcm", v).apply()

    companion object {
        private const val FILE = "aura_secure"

        private fun create(context: Context): SharedPreferences {
            val key = MasterKey.Builder(context).setKeyScheme(MasterKey.KeyScheme.AES256_GCM).build()
            return EncryptedSharedPreferences.create(
                context, FILE, key,
                EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
                EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM,
            )
        }

        private fun open(context: Context): SharedPreferences = try {
            create(context)
        } catch (e: Exception) {
            // Повреждённое хранилище (например, после восстановления бэкапа) — сбрасываем: придётся войти заново.
            context.deleteSharedPreferences(FILE)
            create(context)
        }
    }
}
