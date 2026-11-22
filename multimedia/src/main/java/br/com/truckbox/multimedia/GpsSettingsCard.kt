package br.com.truckbox.multimedia

import android.Manifest
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.text.DateFormat
import java.util.Date

@Composable
internal fun GpsSettingsCard() {
    val context = LocalContext.current
    val settings = remember { GpsSettings(context) }
    val status by GpsSharingService.state.collectAsStateWithLifecycle()
    val scope = rememberCoroutineScope()
    var uid by remember { mutableStateOf(settings.deviceUid) }
    var token by remember { mutableStateOf("") }
    var error by remember { mutableStateOf<String?>(null) }
    var starting by remember { mutableStateOf(false) }

    fun startSharing() {
        if (!hasPreciseLocation(context)) {
            error = "Permita a localização precisa do Android para usar o GPS da central."
        } else {
            settings.enabled = true
            if (GpsSharingService.start(context)) token = ""
            else {
                settings.enabled = false
                error = "O Android bloqueou a ativação. Abra Config e tente novamente."
            }
        }
        starting = false
    }

    val permission = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { startSharing() }

    Card(colors = CardDefaults.cardColors(containerColor = Color(0xFF151A20))) {
        Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text("GPS da multimídia", fontWeight = FontWeight.Bold)
            if (status.running) {
                Button(onClick = { GpsSharingService.stop(context) }) { Text("Parar compartilhamento") }
                if (status.upload.isNotEmpty()) Text(status.upload)
                Text("Posições aguardando envio: ${status.pending}")
                if (status.discarded > 0) Text("Posições descartadas por fila cheia: ${status.discarded}")
                if (status.lastConfirmedAt > 0) {
                    val time = DateFormat.getTimeInstance().format(Date(status.lastConfirmedAt))
                    Text("Último envio confirmado às $time")
                }
            } else {
                Text("Compartilhe a posição da central com a TruckBox Cloud. As posições ficam na central quando falta internet.")
                OutlinedTextField(uid, { uid = it }, label = { Text("ID do dispositivo GPS") },
                    singleLine = true, enabled = !starting, modifier = Modifier.fillMaxWidth())
                OutlinedTextField(token, { token = it }, label = { Text("Token GPS da TruckBox Cloud") },
                    supportingText = { Text("Se o token já foi salvo para este ID, deixe em branco.") },
                    visualTransformation = PasswordVisualTransformation(), keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
                    singleLine = true, enabled = !starting, modifier = Modifier.fillMaxWidth())
                Button(enabled = !starting, onClick = {
                    val credentials = GpsCredentials(uid.trim(), token.trim())
                    starting = true
                    error = null
                    scope.launch {
                        val saved = withContext(Dispatchers.IO) {
                            if (credentials.token.isBlank()) settings.credentials()?.deviceUid == credentials.deviceUid
                            else credentials.isValid() && settings.save(credentials)
                        }
                        if (!saved) {
                            error = "Confira o ID e o token de GPS cadastrados na TruckBox Cloud."
                            starting = false
                        } else if (hasPreciseLocation(context) && Build.VERSION.SDK_INT < 33) startSharing()
                        else permission.launch(buildList {
                            add(Manifest.permission.ACCESS_FINE_LOCATION)
                            add(Manifest.permission.ACCESS_COARSE_LOCATION)
                            if (Build.VERSION.SDK_INT >= 33) add(Manifest.permission.POST_NOTIFICATIONS)
                        }.toTypedArray())
                    }
                }) { Text("Ativar GPS para a Cloud") }
            }
            Text(status.capture)
            error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
        }
    }
}
