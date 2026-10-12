package ai.aura.agent

import android.Manifest
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import ai.aura.agent.ui.AuraApp
import ai.aura.agent.ui.AuraTheme

/** Единственная Activity: весь интерфейс — Compose, навигация — состоянием AppViewModel. */
class MainActivity : ComponentActivity() {
    private val vm: AppViewModel by viewModels()

    private val notificationsPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { /* без разрешения просто не покажем пуши */ }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Push.ensureChannel(this)
        if (Build.VERSION.SDK_INT >= 33) notificationsPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
        setContent {
            AuraTheme {
                AuraApp(vm)
            }
        }
    }
}
