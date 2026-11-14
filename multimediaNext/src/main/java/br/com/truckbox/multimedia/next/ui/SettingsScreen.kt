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
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.foundation.text.KeyboardOptions

private val sections=listOf("Geral","Veículo e perfil","Módulos e permissões","Personalizar painel","Conexões","Tela e aparência","Atualizações","Manutenção e testes","Sobre")
@Composable fun SettingsScreen(state: AppState,preferences: LocalPreferences,onPreferences: (LocalPreferences)->Unit,now: Instant,onCoreRetry: () -> Unit = {},
    cloudUi: CloudUiState=CloudUiState(),onCloudSignIn: (String,String)->Unit={_,_->},onCloudSignOut: ()->Unit={},onCloudRefresh: ()->Unit={},onCloudTestActivate: (String)->Unit={}) {
    var section by remember { mutableIntStateOf(0) }
    var coreHost by remember(preferences.coreHost) { mutableStateOf(preferences.coreHost) }
    var email by remember {mutableStateOf("")}
    var password by remember {mutableStateOf("")}
    var testCode by remember {mutableStateOf("")}
    ChoiceRow(sections,section,{section=it},Modifier.horizontalScroll(rememberScrollState()))
    if(section==0 || section==1) Row(horizontalArrangement=Arrangement.spacedBy(12.dp)) {
        PanelCard(Modifier.weight(1f)) {
            SectionTitle("Veículo ativo",Icons.Outlined.LocalShipping)
            Text(state.vehicleLabel ?: "Vínculo não verificado",fontWeight=FontWeight.Bold)
            Caption("Perfil, veículo, eixos e capacidade vêm do cadastro autenticado na Cloud.")
            Caption("A capacidade de 720 L do FH de exemplo só é referência após ambos tanques cheios.")
        }
        PanelCard(Modifier.weight(1f)) {
            SectionTitle("Perfil de uso",Icons.Outlined.PersonOutline)
            Text(if(state.cloudScope!=null) "Identidade consultada na Cloud" else "Identidade não provisionada")
            Caption("Vínculo de multimídia, veículo e usuário aprovado pelo proprietário na Cloud.")
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
        ConnectionLine("Internet via Core",state.coreInternetSharing?.label(now) ?: "Compartilhamento não informado pelo firmware")
        ConnectionLine("GPS pela Cloud",if(state.gps!=null) "Posição recebida · conferir data e origem" else "Sem posição recebida")
        ConnectionLine("4G / SD do Core","Não verificado · Rev.B.1")
        Caption("Última resposta Core: ${state.lastCoreResponse ?: "Não recebida"}")
        Caption("Host local de leitura (sem token)")
        ChoiceRow(listOf("truckbox.local","192.168.4.1"),when(preferences.coreHost){"truckbox.local"->0;"192.168.4.1"->1;else->-1},{onPreferences(preferences.copy(coreHost=if(it==0) "truckbox.local" else "192.168.4.1"))})
        OutlinedTextField(value=coreHost,onValueChange={coreHost=it.trim()},singleLine=true,label={Text("Core na rede Starlink · IP local")},
            supportingText={Text("Use truckbox.local ou o IP privado do Core, sem http://, porta ou senha.")},isError=!validCoreHost(coreHost))
        TextButton(onClick={onPreferences(preferences.copy(coreHost=coreHost))},enabled=validCoreHost(coreHost) && coreHost!=preferences.coreHost) { Text("Aplicar endereço do Core") }
        Caption("Se o nome local não responder, a consulta tenta também o IP. O acesso usa o Wi-Fi, mesmo sem internet.")
        TextButton(onClick=onCoreRetry,enabled=!state.demo) { Text("Testar conexão agora") }
        state.coreDiagnostics?.let { report ->
            Caption("Último teste: ${report.checkedAt}")
            report.attempts.forEach { attempt ->
                Caption("${attempt.host}${if(attempt.resource=="/api/state") "" else attempt.resource} · ${attempt.issue?.label ?: "JSON recebido"}${attempt.httpStatus?.let { " · HTTP $it" } ?: ""}")
            }
            if(report.responded) Caption("Campos numéricos reconhecidos: ${report.recognizedValues}")
            Text(report.explanation)
        }
        HorizontalDivider(color=Stroke)
        SectionTitle("Acesso à Cloud",Icons.Outlined.Cloud)
        if(!cloudUi.authenticated) {
            Caption("Teste de viagem · conecte ao Core, cole o código recebido e ative uma vez neste aparelho. Permissões e validade vêm da Cloud.")
            OutlinedTextField(value=testCode,onValueChange={testCode=it.trim().take(80)},label={Text("Código de teste")},singleLine=true,visualTransformation=PasswordVisualTransformation(),keyboardOptions=KeyboardOptions(keyboardType=KeyboardType.Password))
            Button(onClick={onCloudTestActivate(testCode);testCode=""},enabled=cloudUi.configured && !cloudUi.busy && !state.demo && state.coreDeviceUid!=null && br.com.truckbox.multimedia.next.data.isFieldTestToken(testCode)) {Text("Ativar teste de viagem")}
        }
        Text(cloudUi.message)
        cloudUi.deviceId?.let { Caption("Identificador da multimídia para aprovação: $it") }
        Caption("Última consulta Cloud: ${state.lastCloudResponse ?: "Não recebida"}")
        if(!cloudUi.authenticated) {
            OutlinedTextField(value=email,onValueChange={email=it},label={Text("E-mail do acesso TruckBox")},singleLine=true,
                enabled=cloudUi.configured && !cloudUi.busy && !state.demo,keyboardOptions=KeyboardOptions(keyboardType=KeyboardType.Email))
            OutlinedTextField(value=password,onValueChange={password=it},label={Text("Senha")},singleLine=true,
                enabled=cloudUi.configured && !cloudUi.busy && !state.demo,visualTransformation=PasswordVisualTransformation(),keyboardOptions=KeyboardOptions(keyboardType=KeyboardType.Password))
            Button(onClick={onCloudSignIn(email,password);password=""},enabled=cloudUi.configured && !cloudUi.busy && !state.demo && email.contains('@') && password.isNotEmpty()) {Text("Entrar na Cloud")}
        }
        if(cloudUi.authenticated || cloudUi.busy) Row(horizontalArrangement=Arrangement.spacedBy(10.dp)) {
            OutlinedButton(onClick=onCloudRefresh,enabled=!cloudUi.busy && !state.demo) {Text("Sincronizar Cloud")}
            TextButton(onClick=onCloudSignOut,enabled=!state.demo) {Text("Sair da Cloud")}
        }
        Caption("Entrar solicita o vínculo; permissões Financeiro/Jornada continuam somente leitura e são concedidas na Cloud.")
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
        Caption("O app consulta GET /api/sensors paginado e, em firmware sem esse recurso, GET /api/state. Nenhum reset, teste de barramento, limpeza de fila ou atualização de firmware é enviado.")
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
