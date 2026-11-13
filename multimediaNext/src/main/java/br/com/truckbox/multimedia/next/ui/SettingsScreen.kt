package br.com.truckbox.multimedia.next.ui

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import br.com.truckbox.multimedia.next.BuildConfig
import br.com.truckbox.multimedia.next.domain.*
import java.time.Instant

private val sections=listOf("Geral","Veículo e perfil","Módulos e permissões","Personalizar painel","Conexões","Tela e aparência","Atualizações","Manutenção e testes","Sobre")
@Composable fun SettingsScreen(state: AppState,preferences: LocalPreferences,onPreferences: (LocalPreferences)->Unit,now: Instant) {
    var section by remember { mutableIntStateOf(0) }
    ChoiceRow(sections,section,{section=it},Modifier.horizontalScroll(rememberScrollState()))
    if(section==0 || section==1) Row(horizontalArrangement=Arrangement.spacedBy(12.dp)) {
        PanelCard(Modifier.weight(1f)) {
            SectionTitle("Veículo ativo",Icons.Outlined.LocalShipping)
            Text("Vínculo não verificado",fontWeight=FontWeight.Bold)
            Caption("Perfil, veículo, eixos e capacidade vêm do cadastro autenticado na Cloud.")
            Caption("A capacidade de 720 L do FH de exemplo só é referência após ambos tanques cheios.")
        }
        PanelCard(Modifier.weight(1f)) {
            SectionTitle("Perfil de uso",Icons.Outlined.PersonOutline)
            Text("Identidade não provisionada")
            Caption("Autenticação por multimídia + veículo + usuário pendente do contrato seguro.")
        }
    }
    if(section==0 || section==2) PanelCard(Modifier.fillMaxWidth(),accent=true) {
        SectionTitle("Módulos administrados pela Cloud",Icons.Outlined.Lock)
        PermissionLine("Financeiro",state.financialAllowed,state.policyLabel)
        PermissionLine("Jornada 24 h",state.journeyAllowed,state.policyLabel)
        Caption("Somente leitura · versão ${state.policyVersion ?: "não recebida"}")
        Caption("Somente o proprietário/gestor na plataforma altera políticas. Sem política válida, conteúdo protegido indisponível.")
    }
    if(section==3) PanelCard(Modifier.fillMaxWidth()) {
        SectionTitle("Personalizar painel · 13 instrumentos",Icons.Outlined.GridView)
        Text("Edição indisponível",fontWeight=FontWeight.Bold)
        Caption(if(canEditDashboard(state.speed,now,false)) "" else "Exige usuário autorizado e veículo comprovadamente parado. Persistência por usuário/veículo aguarda contrato Cloud.")
        Instrument.defaults.forEachIndexed { index,sensor ->
            Row(Modifier.fillMaxWidth(),horizontalArrangement=Arrangement.spacedBy(14.dp)) {
                Caption("${index+1} · ${if(index<3) "Grande" else if(index<7) "Médio" else "Pequeno"}")
                Text(sensor.label)
            }
        }
    }
    if(section==0 || section==4) PanelCard(Modifier.fillMaxWidth()) {
        SectionTitle("Conexões e dispositivos",Icons.Outlined.Wifi)
        ConnectionLine("Core · HTTP local",state.core.label)
        ConnectionLine("Cloud",state.cloud.label)
        ConnectionLine("Internet local","Não verificado")
        ConnectionLine("GPS do Motorista","Não verificado")
        ConnectionLine("4G / SD do Core","Não verificado · Rev.B.1")
        Caption("Última resposta Core: ${state.lastCoreResponse ?: "Não recebida"}")
        Caption("Host local de leitura (sem token)")
        ChoiceRow(listOf("truckbox.local","192.168.4.1"),if(preferences.coreHost=="truckbox.local") 0 else 1,{onPreferences(preferences.copy(coreHost=if(it==0) "truckbox.local" else "192.168.4.1"))})
    }
    if(section==0 || section==5) PanelCard(Modifier.fillMaxWidth()) {
        SectionTitle("Tela e aparência",Icons.Outlined.DarkMode)
        ChoiceRow(listOf("Automático","Noite","Dia"),preferences.appearance.ordinal,{onPreferences(preferences.copy(appearance=Appearance.entries[it]))})
        Caption("Automático 18h–6h · America/Sao_Paulo · brilho noturno solicitado de 12% quando suportado")
        Caption("Preferências locais persistidas; não alteram permissões da Cloud.")
    }
    if(section==6 || section==0) PanelCard(Modifier.fillMaxWidth()) {
        SectionTitle("Atualizações",Icons.Outlined.SystemUpdate)
        Text("App ${BuildConfig.VERSION_NAME} · ${BuildConfig.FLAVOR}")
        Caption("Firmware Core: não consultado · serviço de IA: não verificado")
        Caption("Atualização automática indisponível. Homologação instalada em paralelo, com rollback por desinstalação deste pacote.")
    }
    if(section==7) PanelCard(Modifier.fillMaxWidth()) {
        SectionTitle("Manutenção e testes",Icons.Outlined.Build)
        Caption("O app consulta apenas GET /api/state. Nenhum reset, teste de barramento, limpeza de fila ou atualização de firmware é enviado.")
        Caption("Diagnóstico: ${state.core.label}. Não há logs com payload, credenciais ou dados protegidos.")
    }
    if(section==8) PanelCard(Modifier.fillMaxWidth()) {
        SectionTitle("Sobre o TruckBox",Icons.Outlined.Info)
        Text("Black & Orange · Homologação",fontWeight=FontWeight.Bold)
        Caption("Core coleta; Motorista registra e transmite; Cloud gerencia operações e permissões; Multimídia acompanha.")
        Caption("Oxanium e Inter · SIL Open Font License 1.1. Licenças incluídas em assets/licenses.")
        Caption("Integração de campo e hardware real ainda não verificados. Nenhum status de app comercial pronto.")
    }
}
@Composable private fun PermissionLine(title: String,allowed: Boolean,status: String) {
    Row(Modifier.fillMaxWidth(),horizontalArrangement=Arrangement.SpaceBetween) {Text(title);Text(if(allowed) "Liberado pela Cloud" else status,color=if(allowed) Orange else Muted)}
}
@Composable private fun ConnectionLine(title: String,status: String) {
    Row(Modifier.fillMaxWidth().padding(vertical=6.dp),horizontalArrangement=Arrangement.SpaceBetween){Text(title);Caption(status)}
    HorizontalDivider(color=Stroke)
}
