package br.com.truckbox.app.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp
import br.com.truckbox.app.preferences.AccentOption

val TruckBg = Color(0xFF050A12)
val TruckSurface = Color(0xFF091321)
val TruckSurface2 = Color(0xFF0D1929)
val TruckSurface3 = Color(0xFF122238)
val TruckText = Color(0xFFF4F8FF)
val TruckMuted = Color(0xFF8795AC)
val TruckBorder = Color(0xFF20344F)
val StatusOk = Color(0xFF22D760)
val StatusWarn = Color(0xFFFFB000)
val StatusCritical = Color(0xFFFF3B45)
val StatusInfo = Color(0xFF2196F3)

@Composable
fun TruckBoxTheme(accent: AccentOption, content: @Composable () -> Unit) {
    val accentColor = Color(accent.argb)
    MaterialTheme(
        colorScheme = darkColorScheme(
            primary = accentColor,
            secondary = accentColor,
            background = TruckBg,
            surface = TruckSurface,
            surfaceVariant = TruckSurface2,
            onPrimary = Color.White,
            onBackground = TruckText,
            onSurface = TruckText,
            error = StatusCritical,
        ),
        typography = MaterialTheme.typography.copy(
            displayLarge = TextStyle(
                fontFamily = FontFamily.SansSerif,
                fontWeight = FontWeight.Black,
                fontStyle = FontStyle.Italic,
                fontSize = 58.sp,
            ),
            headlineLarge = TextStyle(
                fontFamily = FontFamily.SansSerif,
                fontWeight = FontWeight.Black,
                fontStyle = FontStyle.Italic,
                fontSize = 38.sp,
            ),
            headlineMedium = TextStyle(
                fontFamily = FontFamily.SansSerif,
                fontWeight = FontWeight.Bold,
                fontStyle = FontStyle.Italic,
                fontSize = 28.sp,
            ),
            titleLarge = TextStyle(
                fontFamily = FontFamily.SansSerif,
                fontWeight = FontWeight.Bold,
                fontSize = 20.sp,
            ),
            titleMedium = TextStyle(
                fontFamily = FontFamily.SansSerif,
                fontWeight = FontWeight.Bold,
                fontSize = 15.sp,
            ),
            bodyLarge = TextStyle(
                fontFamily = FontFamily.SansSerif,
                fontSize = 15.sp,
            ),
            bodyMedium = TextStyle(
                fontFamily = FontFamily.SansSerif,
                fontSize = 13.sp,
            ),
        ),
        content = content,
    )
}
