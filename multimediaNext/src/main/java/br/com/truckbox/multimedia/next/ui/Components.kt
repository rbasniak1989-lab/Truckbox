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
import androidx.compose.ui.unit.Dp
import br.com.truckbox.multimedia.next.R
import br.com.truckbox.multimedia.next.domain.*
import br.com.truckbox.multimedia.next.data.canBeLastInstrumentValue
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
    Font(R.font.inter,FontWeight.Medium,variationSettings=FontVariation.Settings(FontVariation.weight(500))),
    Font(R.font.inter,FontWeight.SemiBold,variationSettings=FontVariation.Settings(FontVariation.weight(600))),
    Font(R.font.inter,FontWeight.Bold,variationSettings=FontVariation.Settings(FontVariation.weight(700))),
    Font(R.font.inter,FontWeight.ExtraBold,variationSettings=FontVariation.Settings(FontVariation.weight(800))),
    Font(R.font.inter,FontWeight.Black,variationSettings=FontVariation.Settings(FontVariation.weight(900))),
)
@OptIn(ExperimentalTextApi::class)
val Oxanium = FontFamily(
    Font(R.font.oxanium,FontWeight.Bold,variationSettings=FontVariation.Settings(FontVariation.weight(700))),
    Font(R.font.oxanium,FontWeight.ExtraBold,variationSettings=FontVariation.Settings(FontVariation.weight(800))),
)

@Composable fun BlackOrangeTheme(content: @Composable () -> Unit) {
    val base=Typography()
    val typography=base.copy(
        displayLarge=base.displayLarge.copy(fontFamily=Inter),displayMedium=base.displayMedium.copy(fontFamily=Inter),displaySmall=base.displaySmall.copy(fontFamily=Inter),
        headlineLarge=base.headlineLarge.copy(fontFamily=Inter),headlineMedium=base.headlineMedium.copy(fontFamily=Inter),headlineSmall=base.headlineSmall.copy(fontFamily=Inter),
        titleLarge=base.titleLarge.copy(fontFamily=Inter),titleMedium=base.titleMedium.copy(fontFamily=Inter,fontWeight=FontWeight.Bold,fontSize=17.sp),titleSmall=base.titleSmall.copy(fontFamily=Inter),
        bodyLarge=base.bodyLarge.copy(fontFamily=Inter,fontSize=14.sp,lineHeight=18.sp),bodyMedium=base.bodyMedium.copy(fontFamily=Inter,fontSize=13.sp,lineHeight=17.sp),bodySmall=base.bodySmall.copy(fontFamily=Inter),
        labelLarge=base.labelLarge.copy(fontFamily=Inter),labelMedium=base.labelMedium.copy(fontFamily=Inter),labelSmall=base.labelSmall.copy(fontFamily=Inter),
    )
    MaterialTheme(colorScheme=darkColorScheme(primary=Orange,onPrimary=Color.Black,secondary=Orange,onSecondary=Color.Black,
        secondaryContainer=Color(0xFF34251B),onSecondaryContainer=Ink,background=Background,surface=Panel,onSurface=Ink,onBackground=Ink,
        onSurfaceVariant=Muted,outline=Stroke,surfaceTint=Color.Transparent,surfaceContainerLowest=Background,
        surfaceContainerLow=Panel,surfaceContainer=Panel,surfaceContainerHigh=Color(0xFF1C2024),surfaceContainerHighest=Color(0xFF23282D)),
        typography=typography) {
        CompositionLocalProvider(LocalContentColor provides Ink,content=content)
    }
}

@Composable fun PanelCard(modifier: Modifier = Modifier, accent: Boolean = false, contentPadding: Dp = 16.dp, spacing: Dp = 10.dp, content: @Composable ColumnScope.() -> Unit) {
    Surface(modifier,shape=RoundedCornerShape(14.dp),color=Panel,border=BorderStroke(1.dp,if(accent) Orange.copy(alpha=.55f) else Stroke)) {
        Column(Modifier.background(Brush.linearGradient(listOf(Color.White.copy(alpha=.015f),Color.Black.copy(alpha=.12f)))).padding(contentPadding),verticalArrangement=Arrangement.spacedBy(spacing),content=content)
    }
}
@Composable fun SectionTitle(title: String, icon: ImageVector = Icons.Outlined.Insights, modifier: Modifier = Modifier) {
    Row(modifier,verticalAlignment=Alignment.CenterVertically,horizontalArrangement=Arrangement.spacedBy(9.dp)) {
        Icon(icon,null,tint=Orange,modifier=Modifier.size(22.dp))
        Text(title,fontFamily=Inter,fontSize=16.sp,fontWeight=FontWeight.Bold)
    }
}
@Composable fun Caption(text: String, modifier: Modifier = Modifier) { Text(text,modifier,color=Muted,fontSize=12.sp,lineHeight=15.sp,fontFamily=Inter) }
@Composable fun Metric(title: String, value: String?, unit: String = "", modifier: Modifier = Modifier, note: String? = null) {
    PanelCard(modifier) {
        Caption(title)
        Row(verticalAlignment=Alignment.Bottom,horizontalArrangement=Arrangement.spacedBy(5.dp)) {
            Text(value ?: "—",fontFamily=Oxanium,fontWeight=FontWeight.ExtraBold,fontStyle=FontStyle.Italic,fontSize=34.sp,maxLines=1,overflow=TextOverflow.Ellipsis,modifier=Modifier.weight(1f,false))
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
    if(demo && reading?.source==Origin.DEMO) reading.value else reading?.observed(now)
fun instrumentDisplayValue(reading: Reading?,now: Instant,demo: Boolean): Double? =
    visibleValue(reading,now,demo) ?: reading?.takeIf {it.canBeLastInstrumentValue() && it.sampledAt?.isAfter(now)==false}?.value
fun instrumentAge(reading: Reading?,now: Instant): String {
    val at=reading?.sampledAt ?: return "idade desconhecida"
    val age=java.time.Duration.between(at,now).seconds.coerceAtLeast(0)
    return when {age>=86400->"${age/86400}d";age>=3600->"${age/3600}h";age>=60->"${age/60}min";else->"${age}s"}
}

@Composable fun InstrumentCard(sensor: Instrument, value: InstrumentValue?, now: Instant, demo: Boolean, size: Int, modifier: Modifier = Modifier, onClick: ()->Unit) {
    val first = instrumentDisplayValue(value?.first,now,demo)
    val second = instrumentDisplayValue(value?.second,now,demo)
    val old=!demo && ((first!=null && value?.first?.observed(now)==null) || (second!=null && value?.second?.observed(now)==null))
    val oldest=listOfNotNull(value?.first,value?.second).filter {it.canBeLastInstrumentValue()}.minByOrNull {it.sampledAt!!}
    val text = if(sensor==Instrument.AIR) "${format(first) ?: "—"} / ${format(second) ?: "—"}" else format(first,if(sensor in listOf(Instrument.GEAR,Instrument.PEDAL,Instrument.TORQUE)) 0 else 1) ?: "—"
    Surface(modifier.clickable(onClick=onClick),shape=RoundedCornerShape(13.dp),border=BorderStroke(1.dp,Stroke),color=Panel) {
      BoxWithConstraints(Modifier.fillMaxSize()) {
        val compact=maxHeight<150.dp
        val tight=maxHeight<105.dp
        val showIcon=maxHeight>=90.dp
        val padding=if(tight) 5.dp else if(compact || size==2) 8.dp else 13.dp
        val availableWidth=(maxWidth-padding*2).value
        val digitSize=minOf(when(size){0->80f;1->62f;else->48f},maxHeight.value*(if(tight) .28f else .33f),
            (availableWidth-if(sensor==Instrument.AIR) 0f else 24f)/(text.length.coerceAtLeast(2)*.65f)).coerceAtLeast(12f)
        Column(Modifier.fillMaxSize().background(Brush.verticalGradient(listOf(Color.White.copy(alpha=.025f),Color.Transparent))).padding(padding),horizontalAlignment=Alignment.CenterHorizontally,verticalArrangement=Arrangement.SpaceBetween) {
            if(showIcon) {
                Box(Modifier.width(40.dp).height(1.dp).background(Orange.copy(alpha=.7f)))
                Icon(instrumentIcon(sensor),null,tint=Orange,modifier=Modifier.size(if(tight) 14.dp else if(size==2 || compact) 19.dp else 24.dp))
            }
            Text(sensor.label,fontFamily=Inter,fontSize=(if(tight) 10 else if(size==2) 11 else if(compact) 12 else 14).sp,lineHeight=(if(tight) 12 else if(size==2) 13 else 16).sp,fontWeight=FontWeight.Bold,maxLines=2,overflow=TextOverflow.Ellipsis,minLines=if(tight) 1 else 2,textAlign=androidx.compose.ui.text.style.TextAlign.Center)
            Row(verticalAlignment=Alignment.Bottom,horizontalArrangement=Arrangement.spacedBy(4.dp)) {
                Text(text,color=if(old) Muted else Ink,fontFamily=Oxanium,fontStyle=FontStyle.Italic,fontWeight=FontWeight.ExtraBold,fontSize=digitSize.sp,lineHeight=(digitSize*1.12f).sp,maxLines=1,overflow=TextOverflow.Clip,softWrap=false,modifier=Modifier.weight(1f,false))
                if(sensor!=Instrument.AIR) Text(sensor.unit,color=Color(0xFFD1A17D),fontSize=(if(tight || size==2) 9 else 12).sp,modifier=Modifier.padding(bottom=3.dp))
            }
            if(sensor==Instrument.AIR) Text("bar · circuitos 1/2",fontFamily=Inter,color=Muted,fontSize=9.sp,lineHeight=11.sp,maxLines=1)
            Text(if(first==null && second==null) "Sem leitura" else if(demo) "Demonstração" else if(old) "Última · ${instrumentAge(oldest,now)}" else if(value?.first?.quality==Quality.PROVISIONAL || value?.second?.quality==Quality.PROVISIONAL) "Não verificado" else "Leitura validada",fontFamily=Inter,color=Muted,fontSize=(if(tight || size==2) 9 else 11).sp,lineHeight=(if(tight || size==2) 11 else 14).sp,maxLines=1)
        }
      }
    }
}
