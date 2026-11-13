package br.com.truckbox.multimedia.next.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.*
import androidx.compose.ui.text.ExperimentalTextApi
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import br.com.truckbox.multimedia.next.R
import br.com.truckbox.multimedia.next.domain.*
import java.time.Instant
import java.util.Locale

val Orange = Color(0xFFE87924)
val Background = Color(0xFF080A0C)
val Panel = Color(0xFF171B1E)
val Stroke = Color(0xFF363E43)
val Muted = Color(0xFFA7ADB0)
val Ink = Color(0xFFEDF0F0)
@OptIn(ExperimentalTextApi::class)
val Inter = FontFamily(
    Font(R.font.inter,FontWeight.Normal,variationSettings=FontVariation.Settings(FontVariation.weight(400))),
    Font(R.font.inter,FontWeight.SemiBold,variationSettings=FontVariation.Settings(FontVariation.weight(600))),
    Font(R.font.inter,FontWeight.Bold,variationSettings=FontVariation.Settings(FontVariation.weight(700))),
)
@OptIn(ExperimentalTextApi::class)
val Oxanium = FontFamily(Font(R.font.oxanium,FontWeight.Bold,variationSettings=FontVariation.Settings(FontVariation.weight(700))))

@Composable fun BlackOrangeTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme=darkColorScheme(primary=Orange,onPrimary=Color.Black,secondary=Orange,onSecondary=Color.Black,
        secondaryContainer=Color(0xFF34251B),onSecondaryContainer=Ink,background=Background,surface=Panel,onSurface=Ink,onBackground=Ink,
        onSurfaceVariant=Muted,outline=Stroke,surfaceTint=Color.Transparent,surfaceContainerLowest=Background,
        surfaceContainerLow=Panel,surfaceContainer=Panel,surfaceContainerHigh=Color(0xFF1C2024),surfaceContainerHighest=Color(0xFF23282D)),
        typography=Typography(bodyLarge=androidx.compose.ui.text.TextStyle(fontFamily=Inter,fontSize=14.sp),bodyMedium=androidx.compose.ui.text.TextStyle(fontFamily=Inter,fontSize=13.sp),titleMedium=androidx.compose.ui.text.TextStyle(fontFamily=Inter,fontWeight=FontWeight.Bold,fontSize=17.sp))) {
        CompositionLocalProvider(LocalContentColor provides Ink,content=content)
    }
}

@Composable fun PanelCard(modifier: Modifier = Modifier, accent: Boolean = false, content: @Composable ColumnScope.() -> Unit) {
    Surface(modifier,shape=RoundedCornerShape(14.dp),color=Panel,border=BorderStroke(1.dp,if(accent) Orange.copy(alpha=.55f) else Stroke)) {
        Column(Modifier.background(Brush.linearGradient(listOf(Color.White.copy(alpha=.015f),Color.Black.copy(alpha=.12f)))).padding(16.dp),verticalArrangement=Arrangement.spacedBy(10.dp),content=content)
    }
}
@Composable fun SectionTitle(title: String, icon: ImageVector = Icons.Outlined.Insights, modifier: Modifier = Modifier) {
    Row(modifier,verticalAlignment=Alignment.CenterVertically,horizontalArrangement=Arrangement.spacedBy(9.dp)) {
        Icon(icon,null,tint=Orange,modifier=Modifier.size(22.dp))
        Text(title,fontFamily=Inter,fontSize=16.sp,fontWeight=FontWeight.Bold)
    }
}
@Composable fun Caption(text: String, modifier: Modifier = Modifier) { Text(text,modifier,color=Muted,fontSize=12.sp,fontFamily=Inter) }
@Composable fun Metric(title: String, value: String?, unit: String = "", modifier: Modifier = Modifier, note: String? = null) {
    PanelCard(modifier) {
        Caption(title)
        Row(verticalAlignment=Alignment.Bottom,horizontalArrangement=Arrangement.spacedBy(5.dp)) {
            Text(value ?: "—",fontFamily=Oxanium,fontWeight=FontWeight.Bold,fontStyle=FontStyle.Italic,fontSize=34.sp,maxLines=1,overflow=TextOverflow.Ellipsis,modifier=Modifier.weight(1f,false))
            Caption(unit,Modifier.padding(bottom=5.dp))
        }
        Caption(note ?: if(value==null) "Sem leitura" else "")
    }
}
@Composable fun ChoiceRow(items: List<String>, selected: Int, onSelect: (Int)->Unit, modifier: Modifier = Modifier) {
    Row(modifier,horizontalArrangement=Arrangement.spacedBy(6.dp)) {
        items.forEachIndexed { i,label ->
            FilterChip(selected=i==selected,onClick={onSelect(i)},label={Text(label,fontSize=12.sp)},modifier=Modifier.heightIn(min=48.dp),colors=FilterChipDefaults.filterChipColors(selectedContainerColor=Orange.copy(alpha=.28f),selectedLabelColor=Ink))
        }
    }
}
fun format(value: Double?, digits: Int=1): String? = value?.takeIf { it.isFinite() }?.let { String.format(Locale.forLanguageTag("pt-BR"),"%.${digits}f",it) }
fun instrumentIcon(sensor: Instrument): ImageVector = when(sensor) {
    Instrument.OIL_PRESSURE,Instrument.OIL_TEMP -> Icons.Outlined.OilBarrel
    Instrument.BOOST -> Icons.Outlined.Speed
    Instrument.FUEL_PRESSURE -> Icons.Outlined.LocalGasStation
    Instrument.COOLANT,Instrument.INTAKE,Instrument.AMBIENT -> Icons.Outlined.Thermostat
    Instrument.GEARBOX,Instrument.GEAR -> Icons.Outlined.Settings
    Instrument.VOLTAGE -> Icons.Outlined.BatteryChargingFull
    Instrument.PEDAL -> Icons.Outlined.Compress
    Instrument.AIR -> Icons.Outlined.TireRepair
    Instrument.TORQUE -> Icons.Outlined.Speed
}
fun visibleValue(reading: Reading?, now: Instant, demo: Boolean): Double? =
    if(demo && reading?.source==Origin.DEMO) reading.value else reading?.current(now)

@Composable fun InstrumentCard(sensor: Instrument, value: InstrumentValue?, now: Instant, demo: Boolean, size: Int, modifier: Modifier = Modifier, onClick: ()->Unit) {
    val wide=LocalWindowInfo.current.containerSize.width/LocalDensity.current.density>=1500
    val first = visibleValue(value?.first,now,demo)
    val second = visibleValue(value?.second,now,demo)
    val text = if(sensor==Instrument.AIR) "${format(first) ?: "—"} / ${format(second) ?: "—"}" else format(first,if(sensor in listOf(Instrument.GEAR,Instrument.PEDAL,Instrument.TORQUE)) 0 else 1) ?: "—"
    Surface(modifier.clickable(onClick=onClick),shape=RoundedCornerShape(13.dp),border=BorderStroke(1.dp,Stroke),color=Panel) {
        Column(Modifier.fillMaxSize().background(Brush.verticalGradient(listOf(Color.White.copy(alpha=.025f),Color.Transparent))).padding(if(size==2) 9.dp else 13.dp),horizontalAlignment=Alignment.CenterHorizontally,verticalArrangement=Arrangement.SpaceBetween) {
            Box(Modifier.width(40.dp).height(1.dp).background(Orange.copy(alpha=.7f)))
            Icon(instrumentIcon(sensor),null,tint=Orange,modifier=Modifier.size(if(size==2) 19.dp else 24.dp))
            Text(sensor.label,fontSize=(if(size==2) 11 else 14).sp,fontWeight=FontWeight.SemiBold,maxLines=3,overflow=TextOverflow.Ellipsis,minLines=2,textAlign=androidx.compose.ui.text.style.TextAlign.Center)
            Row(verticalAlignment=Alignment.Bottom,horizontalArrangement=Arrangement.spacedBy(4.dp)) {
                Text(text,fontFamily=Oxanium,fontStyle=FontStyle.Italic,fontWeight=FontWeight.Bold,fontSize=(when { sensor==Instrument.AIR -> if(wide) 28 else 21; size==0 -> if(wide) 72 else 48; size==1 -> if(wide) 54 else 36; else -> if(wide) 42 else 29 }).sp,maxLines=1,overflow=TextOverflow.Ellipsis,modifier=Modifier.weight(1f,false))
                if(sensor!=Instrument.AIR) Text(sensor.unit,color=Color(0xFFD1A17D),fontSize=12.sp,modifier=Modifier.padding(bottom=5.dp))
            }
            Caption(if(first==null || (sensor==Instrument.AIR && second==null)) "Sem leitura" else if(demo) "Demonstração" else "Leitura validada")
        }
    }
}
