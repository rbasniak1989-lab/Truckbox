package br.com.truckbox.app.ui.screens

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.GpsFixed
import androidx.compose.material.icons.filled.LocalShipping
import androidx.compose.material.icons.filled.Wifi
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import br.com.truckbox.app.data.TruckState
import br.com.truckbox.app.ui.theme.*
import kotlinx.coroutines.delay
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@Composable
fun TruckHeader(state: TruckState) {
    var now by remember { mutableLongStateOf(System.currentTimeMillis()) }
    LaunchedEffect(Unit) {
        while (true) { delay(1000); now = System.currentTimeMillis() }
    }
    val time = remember(now) { SimpleDateFormat("HH:mm", Locale.getDefault()).format(Date(now)) }
    val date = remember(now) { SimpleDateFormat("dd/MM/yyyy", Locale.getDefault()).format(Date(now)) }
    val c = state.connection
    Row(
        Modifier.fillMaxWidth().height(70.dp).padding(horizontal = 20.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text("Truck", fontSize = 27.sp, fontWeight = FontWeight.Black, fontStyle = FontStyle.Italic)
        Text("Box", fontSize = 27.sp, fontWeight = FontWeight.Black, fontStyle = FontStyle.Italic, color = Color(0xFFFF8A00))
        Spacer(Modifier.width(25.dp))
        Icon(Icons.Default.LocalShipping, null, tint = TruckMuted, modifier = Modifier.size(31.dp))
        Spacer(Modifier.width(8.dp))
        Column {
            Text("Volvo FH 540", fontWeight = FontWeight.Bold, fontSize = 16.sp)
            Text("CAN-J1939", color = TruckMuted, fontSize = 11.sp)
        }
        HeaderDivider()
        StatusDot(c.wifiBound, "Wi-Fi", if (c.wifiBound) "TruckBox-V1" else "sem rede")
        HeaderDivider()
        StatusDot(c.tcpConnected, "ESP32", if (c.tcpConnected) "TCP conectado" else "192.168.4.1:35000")
        HeaderDivider()
        StatusDot(c.canActive, "CAN", if (c.canActive) "${c.framesPerSecond.toInt()} fps" else "sem frames")
        HeaderDivider()
        Row(verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Default.GpsFixed, null, tint = if (state.gps.hasFix) StatusOk else TruckMuted, modifier = Modifier.size(23.dp))
            Spacer(Modifier.width(7.dp))
            Column {
                Text("GPS", fontWeight = FontWeight.Bold, fontSize = 14.sp)
                Text(if (state.gps.hasFix) "${state.gps.satellites} sat" else "sem fix", color = TruckMuted, fontSize = 10.sp)
            }
        }
        Spacer(Modifier.weight(1f))
        Column(horizontalAlignment = Alignment.End) {
            Text(time, fontWeight = FontWeight.Black, fontStyle = FontStyle.Italic, fontSize = 25.sp)
            Text(date, color = TruckMuted, fontSize = 11.sp)
        }
    }
    HorizontalDivider(color = TruckBorder.copy(alpha = 0.65f))
}

@Composable private fun HeaderDivider() = Box(Modifier.padding(horizontal = 16.dp).width(1.dp).height(38.dp).background(TruckBorder))

@Composable
private fun StatusDot(ok: Boolean, title: String, subtitle: String) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(12.dp), contentAlignment = Alignment.Center) {
            Surface(shape = RoundedCornerShape(99.dp), color = if (ok) StatusOk else TruckMuted.copy(alpha = 0.45f), modifier = Modifier.fillMaxSize()) { }
        }
        Spacer(Modifier.width(7.dp))
        Column {
            Text(title, fontWeight = FontWeight.Bold, fontSize = 13.sp)
            Text(subtitle, color = TruckMuted, fontSize = 9.sp)
        }
    }
}

@Composable
fun TruckCard(
    modifier: Modifier = Modifier,
    accentBorder: Boolean = false,
    content: @Composable ColumnScope.() -> Unit,
) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(17.dp),
        color = TruckSurface2,
        border = BorderStroke(1.dp, if (accentBorder) MaterialTheme.colorScheme.primary.copy(alpha = 0.85f) else TruckBorder.copy(alpha = 0.8f)),
        tonalElevation = 0.dp,
    ) {
        Column(Modifier.fillMaxSize().padding(14.dp), content = content)
    }
}

fun fmt1(v: Double?): String = v?.let { String.format(Locale.getDefault(), "%.1f", it) } ?: "—"
fun fmt2(v: Double?): String = v?.let { String.format(Locale.getDefault(), "%.2f", it) } ?: "—"
fun fmt0(v: Double?): String = v?.let { String.format(Locale.getDefault(), "%.0f", it) } ?: "—"
fun formatDuration(seconds: Long): String {
    val h = seconds / 3600
    val m = (seconds % 3600) / 60
    return if (h > 0) "${h}h ${m}min" else "${m} min"
}
