package br.com.truckbox.multimedia.next.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.platform.testTag
import br.com.truckbox.multimedia.next.domain.*
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter

@Composable fun TruckBoxApp(
    input: AppState,
    preferences: LocalPreferences = LocalPreferences(),
    onPreferences: (LocalPreferences)->Unit = {},
    onNight: (Boolean)->Unit = {},
    startingPage: Page = Page.HOME,
    nowOverride: Instant? = null,
) {
    val state = input.redacted()
    var now by remember { mutableStateOf(nowOverride ?: Instant.now()) }
    LaunchedEffect(nowOverride) { if(nowOverride==null) while(true) { now=Instant.now(); delay(1000) } else now=nowOverride }
    val night = isNight(now,preferences.appearance)
    LaunchedEffect(night) { onNight(night) }
    var pageName by rememberSaveable { mutableStateOf(startingPage.name) }
    val page = Page.valueOf(pageName)
    val drawer = rememberDrawerState(DrawerValue.Closed)
    val scope = rememberCoroutineScope()
    BlackOrangeTheme {
        ModalNavigationDrawer(drawerState=drawer,gesturesEnabled=drawer.isOpen,drawerContent={
            ModalDrawerSheet(modifier=Modifier.width(300.dp)) {
                Text("TRUCKBOX",Modifier.padding(24.dp),fontSize=24.sp,fontWeight=FontWeight.Black)
                Page.entries.forEach { p ->
                    NavigationDrawerItem(label={Text(p.title)},selected=page==p,onClick={pageName=p.name;scope.launch{drawer.close()}},modifier=Modifier.testTag("nav-${p.name}"))
                }
                Spacer(Modifier.height(24.dp));Caption("Black & Orange · Homologação",Modifier.padding(16.dp))
            }
        }) {
            Column(Modifier.fillMaxSize().background(Background).safeDrawingPadding().testTag("app-root")) {
                Row(Modifier.fillMaxWidth().heightIn(min=64.dp).padding(horizontal=16.dp),verticalAlignment=Alignment.CenterVertically,horizontalArrangement=Arrangement.spacedBy(12.dp)) {
                    Text("Truck",fontStyle=FontStyle.Italic,fontWeight=FontWeight.Black,fontSize=25.sp)
                    Text("Box",color=Orange,fontStyle=FontStyle.Italic,fontWeight=FontWeight.Black,fontSize=25.sp,modifier=Modifier.offset(x=(-12).dp))
                    Box(Modifier.width(1.dp).height(26.dp).background(Stroke))
                    Text(page.title,Modifier.weight(1f),fontWeight=FontWeight.Bold,fontSize=19.sp)
                    Caption(if(state.demo) "DEMONSTRAÇÃO" else "HOMOLOGAÇÃO")
                    Text(DateTimeFormatter.ofPattern("HH:mm").withZone(ZoneId.of("America/Sao_Paulo")).format(now),fontWeight=FontWeight.Bold)
                    IconButton(onClick={onPreferences(preferences.copy(appearance=if(night) Appearance.DAY else Appearance.NIGHT))}) { Icon(Icons.Outlined.DarkMode,"Cabine escura",tint=Orange) }
                }
                HorizontalDivider(color=Stroke)
                if(state.demo) {
                    Text("DEMONSTRAÇÃO · valores fictícios · sem conexão com Core ou Cloud",Modifier.fillMaxWidth().background(Orange.copy(alpha=.15f)).padding(8.dp),color=Orange,fontSize=12.sp)
                }
                Row(Modifier.fillMaxWidth().padding(horizontal=20.dp,vertical=6.dp),horizontalArrangement=Arrangement.spacedBy(24.dp)) {
                    Caption("Core: ${state.core.label}")
                    Caption("Cloud: ${state.cloud.label}")
                }
                Box(Modifier.weight(1f).fillMaxWidth()) {
                    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start=56.dp,end=20.dp,top=10.dp,bottom=20.dp),verticalArrangement=Arrangement.spacedBy(14.dp)) {
                        when(page) {
                            Page.HOME -> HomeScreen(state,now)
                            Page.CONSUMPTION -> ConsumptionScreen(state,now)
                            Page.TRIP -> TripScreen(state,now)
                            Page.TRUCK -> TruckScreen(state,now)
                            Page.SETTINGS -> SettingsScreen(state,preferences,onPreferences,now)
                        }
                    }
                    FilledTonalIconButton(onClick={scope.launch{drawer.open()}},modifier=Modifier.align(Alignment.CenterStart).padding(start=8.dp).width(36.dp).height(72.dp).testTag("drawer-handle")) {
                        Icon(Icons.Outlined.ChevronRight,"Abrir menu",tint=Orange)
                    }
                }
            }
        }
    }
}
