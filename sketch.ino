/*
  SISTEMA DE SENHAS - LANCHONETE
  ESP32 + LCD 16x2 I2C + Keypad 4x4 + Buzzer + LEDs

  PINOS:
  LCD SDA=GPIO22, SCL=GPIO23
  Keypad linhas R1-R4: 13, 12, 14, 27
  Keypad colunas C1-C4: 26, 25, 33, 32
  Buzzer=GPIO18 | LED verde=GPIO19 | LED vermelho=GPIO21

  TECLADO:
  D = entrar no modo manual / limpar todos os dígitos
  0-9 = digitar número da senha
  A = apagar último dígito / voltar da confirmação
  C = confirmar senha
  B = cancelar e sair do modo manual

  Bibliotecas externas em libraries.txt:
  Keypad
  LiquidCrystal I2C
  ArduinoJson
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <Wire.h>

// ==================== WIFI E SERVIDOR ====================
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";
WebServer server(80);

// ==================== LCD ====================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ==================== TECLADO 4x4 ====================
const byte LINHAS = 4;
const byte COLUNAS = 4;

char teclas[LINHAS][COLUNAS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte pinosLinhas[LINHAS] = {13, 12, 14, 27};
byte pinosColunas[COLUNAS] = {26, 25, 33, 32};

Keypad keypad = Keypad(makeKeymap(teclas), pinosLinhas, pinosColunas,
                      LINHAS, COLUNAS);

// ==================== PINOS DE SAÍDA ====================
const int BUZZER_PIN = 18;
const int LED_VERDE = 19;
const int LED_VERMELHO = 21;

// ==================== CONSTANTES ====================
const int MAX_SENHA = 9999;
const int MIN_SENHA = 1;
const int LIMITE_HISTORICO = 5;
const int MAX_DIGITOS = 4;

const int EEPROM_SIZE = 512;
const int ADDR_SENHA_ATUAL = 0;
const int ADDR_TOTAL_HIST = 4;
const int ADDR_HISTORICO = 8;

// ==================== ESTRUTURAS E ESTADO ====================
struct RegistroSenha {
  int numero;
  char horario[9]; // HH:MM:SS
  bool valido;
};

struct EstadoSistema {
  int senhaAtual;
  RegistroSenha historico[LIMITE_HISTORICO];
  int totalHistorico;
};

EstadoSistema estado = {};

char bufferDigita[MAX_DIGITOS + 1] = "";
int posDigita = 0;
bool modoManual = false;
bool modoConfirmacao = false;
unsigned long ultimaTecla = 0;

// ==================== HORÁRIO ====================
// Recebe: nada. Retorna: horário simulado desde a inicialização do ESP32.
String obterHorario() {
  unsigned long segs = millis() / 1000;
  int h = (segs / 3600) % 24;
  int m = (segs % 3600) / 60;
  int s = segs % 60;
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
  return String(buf);
}

// ==================== SONS ====================
// Recebe frequência (Hz) e duração (ms). Retorna: nada; toca um tom.
void tocarTom(int freq, int duracao) {
  tone(BUZZER_PIN, freq, duracao);
  delay(duracao + 10);
  noTone(BUZZER_PIN);
}

// Recebe: nada. Retorna: nada; toca três bipes de chamada.
void somChamada() {
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 880, 100);
    delay(130);
  }
  noTone(BUZZER_PIN);
}

// Recebe: nada. Retorna: nada; toca sequência de confirmação.
void somConfirmacao() {
  tone(BUZZER_PIN, 600, 80); delay(100);
  tone(BUZZER_PIN, 800, 80); delay(100);
  tone(BUZZER_PIN, 1000, 80); delay(100);
  noTone(BUZZER_PIN);
}

// Recebe: nada. Retorna: nada; toca sinal de erro.
void somErro() {
  tone(BUZZER_PIN, 200, 300);
  delay(320);
  noTone(BUZZER_PIN);
}

// Recebe: nada. Retorna: nada; toca um bip curto de tecla.
void somTecla() {
  tone(BUZZER_PIN, 1200, 30);
  delay(40);
  noTone(BUZZER_PIN);
}

// ==================== LEDs ====================
// Recebe true se Wi-Fi conectado; false caso contrário. Retorna: nada.
void atualizarLeds(bool wifiOk) {
  digitalWrite(LED_VERDE, wifiOk ? HIGH : LOW);
  digitalWrite(LED_VERMELHO, wifiOk ? LOW : HIGH);
}

// ==================== LCD ====================
// Recebe: nada. Retorna: nada; limpa o LCD.
void lcdLimpar() {
  lcd.clear();
  lcd.setCursor(0, 0);
}

// Recebe linha (0 ou 1) e texto. Retorna: nada; centraliza no LCD 16x2.
void lcdCentralizado(int linha, const char* texto) {
  int len = strlen(texto);
  int col = (len >= 16) ? 0 : (16 - len) / 2;
  lcd.setCursor(col, linha);
  lcd.print(texto);
}

// Recebe: nada. Retorna: nada; mostra a tela de espera.
void mostrarEspera() {
  lcdLimpar();
  lcdCentralizado(0, "Digite a");
  lcdCentralizado(1, "proxima senha");
}

// Recebe: nada. Retorna: nada; mostra o buffer de digitação.
void mostrarDigitacao() {
  lcdLimpar();
  lcd.setCursor(0, 0);
  lcd.print("Senha: ");
  lcd.print(bufferDigita);
  for (int i = posDigita; i < MAX_DIGITOS; i++) {
    lcd.print("_");
  }

  // Esta mensagem cabe nas 16 colunas do LCD.
  lcd.setCursor(0, 1);
  lcd.print("A=Apaga C=OK B=Sai");
}

// Recebe: nada. Retorna: nada; solicita confirmação da senha.
void mostrarConfirmacao() {
  lcdLimpar();
  lcdCentralizado(0, "Confirmar senha?");
  lcdCentralizado(1, "C=Sim A/B=Nao");
}

// Recebe o número da senha. Retorna: nada; exibe o registro concluído.
void mostrarSucesso(int senha) {
  lcdLimpar();
  lcdCentralizado(0, "Senha registrada!");
  char buf[17];
  snprintf(buf, sizeof(buf), "Senha: %04d", senha);
  lcdCentralizado(1, buf);
}

// Recebe uma mensagem. Retorna: nada; exibe o erro.
void mostrarErro(const char* msg) {
  lcdLimpar();
  lcdCentralizado(0, "ERRO");
  lcdCentralizado(1, msg);
}

// Recebe: nada. Retorna: nada; exibe o endereço IP do ESP32.
void mostrarIP() {
  lcdLimpar();
  lcdCentralizado(0, "IP do ESP32:");
  String ip = WiFi.localIP().toString();
  lcdCentralizado(1, ip.c_str());
}

// ==================== EEPROM ====================
// Recebe: nada. Retorna: nada; salva senha atual e histórico.
void salvarEstado() {
  EEPROM.writeInt(ADDR_SENHA_ATUAL, estado.senhaAtual);
  EEPROM.writeInt(ADDR_TOTAL_HIST, estado.totalHistorico);

  for (int i = 0; i < LIMITE_HISTORICO; i++) {
    EEPROM.put(ADDR_HISTORICO + i * sizeof(RegistroSenha),
               estado.historico[i]);
  }
  EEPROM.commit();
}

// Recebe: nada. Retorna: nada; recupera e valida os dados salvos.
void carregarEstado() {
  EEPROM.begin(EEPROM_SIZE);

  estado.senhaAtual = EEPROM.readInt(ADDR_SENHA_ATUAL);
  estado.totalHistorico = EEPROM.readInt(ADDR_TOTAL_HIST);

  if (estado.senhaAtual < MIN_SENHA || estado.senhaAtual > MAX_SENHA) {
    estado.senhaAtual = 0;
  }
  if (estado.totalHistorico < 0 ||
      estado.totalHistorico > LIMITE_HISTORICO) {
    estado.totalHistorico = 0;
  }

  for (int i = 0; i < LIMITE_HISTORICO; i++) {
    EEPROM.get(ADDR_HISTORICO + i * sizeof(RegistroSenha),
               estado.historico[i]);

    if (!estado.historico[i].valido ||
        estado.historico[i].numero < MIN_SENHA ||
        estado.historico[i].numero > MAX_SENHA) {
      estado.historico[i].numero = 0;
      estado.historico[i].horario[0] = '\0';
      estado.historico[i].valido = false;
    }
  }
}

// ==================== LÓGICA DE SENHAS ====================
// Recebe uma nova senha. Retorna: nada; move a anterior ao histórico.
void adicionarHistorico(int senha) {
  if (estado.senhaAtual > 0) {
    for (int i = LIMITE_HISTORICO - 1; i > 0; i--) {
      estado.historico[i] = estado.historico[i - 1];
    }

    estado.historico[0].numero = estado.senhaAtual;
    String horario = obterHorario();
    horario.toCharArray(estado.historico[0].horario,
                        sizeof(estado.historico[0].horario));
    estado.historico[0].valido = true;

    if (estado.totalHistorico < LIMITE_HISTORICO) {
      estado.totalHistorico++;
    }
  }

  estado.senhaAtual = senha;
  salvarEstado();
}

// Recebe: nada. Retorna: nada; gera e chama a próxima senha.
void chamarProxima() {
  int prox = estado.senhaAtual + 1;
  if (prox > MAX_SENHA) prox = MIN_SENHA;

  adicionarHistorico(prox);
  somChamada();
  somConfirmacao();
  mostrarSucesso(prox);
  delay(2000);
  mostrarEspera();
}

// Recebe: nada. Retorna: nada; zera contador e histórico.
void resetarContador() {
  estado.senhaAtual = 0;
  estado.totalHistorico = 0;

  for (int i = 0; i < LIMITE_HISTORICO; i++) {
    estado.historico[i].numero = 0;
    estado.historico[i].horario[0] = '\0';
    estado.historico[i].valido = false;
  }

  salvarEstado();
  somConfirmacao();
  lcdLimpar();
  lcdCentralizado(0, "Contador resetado");
  delay(1500);
  mostrarEspera();
}

// Recebe número digitado. Retorna: nada; valida e registra a senha.
void definirSenhaManual(int senha) {
  if (senha >= MIN_SENHA && senha <= MAX_SENHA) {
    adicionarHistorico(senha);
    somConfirmacao();
    mostrarSucesso(senha);
    delay(2000);
  } else {
    somErro();
    mostrarErro("Senha invalida");
    delay(1500);
  }
  mostrarEspera();
}

// ==================== PAINEL WEB (HTML EMBUTIDO) ====================
const char HTML_INDEX[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>Painel de Senhas</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
:root{--fundo:#0d1b2a;--painel:#1b2a4a;--destaque:#ffd60a;--verde:#00e676;--texto:#fff;--borda:#2d4466}
body{min-height:100vh;background:var(--fundo);color:var(--texto);font-family:sans-serif;display:flex;justify-content:center;align-items:center;padding:20px}
.painel{width:100%;max-width:1000px;background:var(--painel);border:1px solid var(--borda);border-radius:20px;padding:30px;box-shadow:0 15px 50px rgba(0,0,0,.4)}
.cabecalho{display:flex;justify-content:space-between;align-items:center;margin-bottom:20px}
.identificador{color:var(--destaque);font-size:12px;font-weight:bold;letter-spacing:2px}
h1{font-size:28px}
.status{display:flex;align-items:center;gap:8px;color:var(--verde)}
.status-indicador{width:10px;height:10px;background:var(--verde);border-radius:50%}
.senha-atual{background:#14253d;border:2px solid var(--borda);border-radius:16px;min-height:250px;display:flex;flex-direction:column;justify-content:center;align-items:center;padding:20px}
.legenda{color:#888;font-size:14px;font-weight:bold;letter-spacing:3px}
.numero-senha{color:var(--destaque);font-size:clamp(80px,15vw,150px);font-weight:900;font-family:monospace;margin:10px 0}
.historico{margin-top:20px}
.historico h2{font-size:20px;margin-bottom:15px}
.lista-historico{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:10px;list-style:none}
.lista-historico li{background:#14253d;border:1px solid var(--borda);border-radius:10px;padding:15px;text-align:center}
.historico-senha{font-size:24px;font-weight:bold;font-family:monospace}
.historico-horario{font-size:11px;color:#888;margin-top:5px;font-family:monospace}
.controles{display:flex;gap:10px;justify-content:center;margin-top:20px;flex-wrap:wrap}
button{background:var(--destaque);color:#0d1b2a;border:0;border-radius:10px;padding:15px 25px;font-size:16px;font-weight:bold;cursor:pointer}
button:hover{filter:brightness(1.1)}
.config{display:flex;gap:20px;justify-content:center;margin-top:15px;font-size:14px;color:#888}
.config label{display:flex;align-items:center;gap:8px;cursor:pointer}
.config input{width:18px;height:18px;accent-color:var(--destaque)}
</style>
</head>
<body>
<main class="painel">
<header class="cabecalho">
<div><span class="identificador">PAINEL DE ATENDIMENTO</span><h1>Senha Atual</h1></div>
<div class="status"><span class="status-indicador"></span><span>Conectado</span></div>
</header>
<section class="senha-atual" aria-live="assertive">
<span class="legenda">SENHA CHAMADA</span>
<div id="senhaAtual" class="numero-senha">---</div>
<p id="msg">Aguardando...</p>
</section>
<section class="historico">
<h2>Últimas senhas</h2>
<ul id="historico" class="lista-historico">
<li style="grid-column:1/-1;color:#888;padding:20px">Nenhuma senha ainda</li>
</ul>
</section>
<div class="controles">
<button onclick="testar()">Chamar (Teste)</button>
<button onclick="resetar()" style="background:#ff6b6b;color:white">Reset</button>
</div>
<div class="config">
<label><input type="checkbox" id="som" checked><span>Som</span></label>
<label><input type="checkbox" id="auto" checked><span>Atualização automática</span></label>
</div>
</main>
<script>
const senhaEl=document.getElementById('senhaAtual');
const histEl=document.getElementById('historico');
const msgEl=document.getElementById('msg');
let som=true, intervalo=null, ultimaSenha='---';

async function carregar(){
  try{
    const r=await fetch('/api/senha');
    const d=await r.json();
    atualizar(d);
  }catch(e){console.log('Erro ao carregar senha:',e);}
}

function atualizar(d){
  const senha=d.senha||'---';
  senhaEl.textContent=senha;
  msgEl.textContent=senha!=='---'?'Senha '+senha+' chamada!':'Aguardando...';

  if(senha!=='---' && som && senha!==ultimaSenha){
    const AudioCtx=window.AudioContext||window.webkitAudioContext;
    if(AudioCtx){
      const ctx=new AudioCtx();
      [0,180,360].forEach(t=>{
        const o=ctx.createOscillator(),g=ctx.createGain();
        o.type='sine';o.frequency.value=880;
        g.gain.setValueAtTime(.0001,ctx.currentTime+t/1000);
        g.gain.exponentialRampToValueAtTime(.25,ctx.currentTime+t/1000+.02);
        g.gain.exponentialRampToValueAtTime(.0001,ctx.currentTime+t/1000+.15);
        o.connect(g);g.connect(ctx.destination);
        o.start(ctx.currentTime+t/1000);
        o.stop(ctx.currentTime+t/1000+.15);
      });
    }
  }
  ultimaSenha=senha;
}

async function carregarHist(){
  try{
    const r=await fetch('/api/historico');
    const d=await r.json();
    histEl.innerHTML='';
    if(!d.historico||d.historico.length===0){
      histEl.innerHTML='<li style="grid-column:1/-1;color:#888;padding:20px">Nenhuma senha ainda</li>';
      return;
    }
    d.historico.forEach(h=>{
      const li=document.createElement('li');
      const n=document.createElement('div');
      n.className='historico-senha';n.textContent=h.senha;
      const t=document.createElement('div');
      t.className='historico-horario';t.textContent=h.horario;
      li.append(n,t);histEl.appendChild(li);
    });
  }catch(e){console.log('Erro no historico:',e);}
}

async function testar(){
  await fetch('/api/chamar',{method:'POST'});
  await carregar();await carregarHist();
}

async function resetar(){
  if(confirm('Resetar contador?')){
    await fetch('/api/reset',{method:'POST'});
    ultimaSenha='---';
    await carregar();await carregarHist();
  }
}

document.getElementById('som').onchange=e=>som=e.target.checked;
document.getElementById('auto').onchange=e=>{
  if(e.target.checked){
    if(!intervalo) intervalo=setInterval(()=>{carregar();carregarHist();},2000);
  }else{
    clearInterval(intervalo);intervalo=null;
  }
};

intervalo=setInterval(()=>{carregar();carregarHist();},2000);
carregar();carregarHist();
</script>
</body>
</html>
)rawliteral";

// ==================== ROTAS DO SERVIDOR WEB ====================
// Recebe GET /. Retorna a página HTML do painel.
void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", HTML_INDEX);
}

// Recebe GET /api/senha. Retorna JSON com senha atual e horário.
void handleApiSenha() {
  StaticJsonDocument<256> doc;
  doc["senha"] = estado.senhaAtual > 0 ? String(estado.senhaAtual) : "---";
  doc["horario"] = obterHorario();
  doc["online"] = true;
  doc["historico_count"] = estado.totalHistorico;

  String resp;
  serializeJson(doc, resp);
  server.send(200, "application/json", resp);
}

// Recebe GET /api/historico. Retorna JSON com as últimas senhas.
void handleApiHistorico() {
  StaticJsonDocument<512> doc;
  JsonArray arr = doc.createNestedArray("historico");

  for (int i = 0; i < estado.totalHistorico; i++) {
    if (!estado.historico[i].valido) continue;
    JsonObject obj = arr.createNestedObject();
    obj["senha"] = estado.historico[i].numero;
    obj["horario"] = estado.historico[i].horario;
  }

  String resp;
  serializeJson(doc, resp);
  server.send(200, "application/json", resp);
}

// Recebe POST /api/chamar. Retorna JSON atualizado da senha.
void handleApiChamar() {
  chamarProxima();
  handleApiSenha();
}

// Recebe POST /api/reset. Retorna JSON após reset.
void handleApiReset() {
  resetarContador();
  handleApiSenha();
}

// Recebe POST /api/set com parâmetro senha. Retorna JSON atualizado.
void handleApiSet() {
  if (server.hasArg("senha")) {
    int num = server.arg("senha").toInt();
    definirSenhaManual(num);
  }
  handleApiSenha();
}

// Recebe rota desconhecida. Retorna HTTP 404.
void handleNotFound() {
  server.send(404, "text/plain", "Nao encontrado");
}

// ==================== BUFFER DO TECLADO ====================
// Recebe: nada. Retorna: nada; limpa todos os dígitos digitados.
void limparBuffer() {
  memset(bufferDigita, 0, sizeof(bufferDigita));
  posDigita = 0;
}

// ==================== PROCESSAMENTO DO TECLADO ====================
// Recebe: caractere retornado por keypad.getKey().
// Retorna: nada; altera o modo, o buffer e a tela conforme a tecla.
void processarTecla(char tecla) {
  ultimaTecla = millis();
  somTecla();

  Serial.printf("\nTecla recebida: %c\n", tecla);

  // MODO DE CONFIRMAÇÃO: C confirma; A ou B volta à digitação.
  if (modoConfirmacao) {
    if (tecla == 'C') {
      int senha = atoi(bufferDigita);

      // Sai dos modos antes de executar a rotina de registro.
      modoConfirmacao = false;
      modoManual = false;

      Serial.printf("Confirmando senha manual: %d\n", senha);

      if (senha >= MIN_SENHA && senha <= MAX_SENHA) {
        definirSenhaManual(senha);
      } else {
        somErro();
        mostrarErro("Senha invalida");
        delay(1500);
        mostrarEspera();
      }
      limparBuffer();
    }
    else if (tecla == 'A' || tecla == 'B') {
      modoConfirmacao = false;
      mostrarDigitacao();
    }

    Serial.printf("Estado: Manual=%d Confirmacao=%d Buffer=%s\n",
                  modoManual, modoConfirmacao, bufferDigita);
    return;
  }

  // MODO MANUAL: aceita números e comandos A, B, C e D.
  if (modoManual) {
    if (tecla >= '0' && tecla <= '9') {
      if (posDigita < MAX_DIGITOS) {
        bufferDigita[posDigita] = tecla;
        posDigita++;
        bufferDigita[posDigita] = '\0';
      } else {
        somErro();
      }
    }
    else if (tecla == 'A') {
      if (posDigita > 0) {
        posDigita--;
        bufferDigita[posDigita] = '\0';
      }
    }
    else if (tecla == 'D') {
      limparBuffer();
    }
    else if (tecla == 'C') {
      if (posDigita > 0) {
        modoConfirmacao = true;
        mostrarConfirmacao();
      } else {
        somErro();
        mostrarErro("Digite uma senha");
        delay(1000);
      }
    }
    else if (tecla == 'B') {
      modoManual = false;
      modoConfirmacao = false;
      limparBuffer();
      mostrarEspera();
      Serial.println("Modo manual encerrado.");
      return;
    }

    if (!modoConfirmacao) {
      mostrarDigitacao();
    }

    Serial.printf("Estado: Manual=%d Confirmacao=%d Buffer=%s\n",
                  modoManual, modoConfirmacao, bufferDigita);
    return;
  }

  // MODO DE ESPERA: D inicia a digitação manual.
  if (tecla == 'D') {
    modoManual = true;
    modoConfirmacao = false;
    limparBuffer();
    mostrarDigitacao();
    Serial.println("MODO MANUAL ATIVADO. Digite de 1 a 4 digitos.");
  } else {
    Serial.println("Pressione D para entrar no modo manual.");
  }

  Serial.printf("Estado: Manual=%d Confirmacao=%d Buffer=%s\n",
                modoManual, modoConfirmacao, bufferDigita);
}

// ==================== WIFI ====================
// Recebe: nada. Retorna: nada; tenta conectar e atualiza LEDs/LCD.
void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  lcdLimpar();
  lcdCentralizado(0, "Conectando WiFi");

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 30) {
    lcd.setCursor(0, 1);
    lcd.print("Tentativa ");
    lcd.print(tentativas + 1);
    lcd.print("   ");
    delay(500);
    tentativas++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    lcdLimpar();
    lcdCentralizado(0, "WiFi Conectado!");
    String ip = WiFi.localIP().toString();
    lcdCentralizado(1, ip.c_str());
    atualizarLeds(true);
    Serial.print("WiFi conectado. IP: ");
    Serial.println(ip);
    delay(2000);
  } else {
    lcdLimpar();
    lcdCentralizado(0, "Falha WiFi");
    lcdCentralizado(1, "Modo Offline");
    atualizarLeds(false);
    Serial.println("WiFi nao conectado. Modo offline.");
    delay(2000);
  }
}

// ==================== SETUP ====================
// Recebe: nada. Retorna: nada; inicializa periféricos, memória, Wi-Fi e servidor.
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n=== Sistema de Senhas ESP32 ===");
  Serial.println("D=Manual | A=Apagar | C=Confirmar | B=Cancelar");

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, HIGH);
  noTone(BUZZER_PIN);

  keypad.setDebounceTime(50);
  keypad.setHoldTime(500);

  Wire.begin(22, 23);
  lcd.init();
  lcd.backlight();
  lcdLimpar();
  lcdCentralizado(0, "Iniciando...");
  lcdCentralizado(1, "v1.1");
  delay(1000);

  carregarEstado();
  conectarWiFi();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/senha", HTTP_GET, handleApiSenha);
  server.on("/api/historico", HTTP_GET, handleApiHistorico);
  server.on("/api/chamar", HTTP_POST, handleApiChamar);
  server.on("/api/reset", HTTP_POST, handleApiReset);
  server.on("/api/set", HTTP_POST, handleApiSet);
  server.onNotFound(handleNotFound);
  server.begin();

  somConfirmacao();
  mostrarEspera();

  Serial.printf("Sistema pronto! IP: %s\n",
                WiFi.localIP().toString().c_str());
}

// ==================== LOOP PRINCIPAL ====================
// Recebe: nada. Retorna: nada; atende web, lê teclado e controla timeouts.
void loop() {
  server.handleClient();

  // keypad.getKey() devolve a tecla ou NO_KEY quando nenhuma foi pressionada.
  char tecla = keypad.getKey();
  if (tecla != NO_KEY) {
    processarTecla(tecla);
  }

  // Encerra modo manual após 30 segundos sem atividade.
  if (modoManual && millis() - ultimaTecla > 30000) {
    modoManual = false;
    modoConfirmacao = false;
    limparBuffer();
    mostrarEspera();
    Serial.println("Tempo esgotado: modo manual encerrado.");
  }

  // Cancela a confirmação após 10 segundos, mantendo a digitação.
  if (modoConfirmacao && millis() - ultimaTecla > 10000) {
    modoConfirmacao = false;
    mostrarDigitacao();
    Serial.println("Tempo de confirmacao esgotado.");
  }

  // Verifica Wi-Fi a cada 30 segundos e tenta reconectar se necessário.
  static unsigned long ultimaVerificacao = 0;
  if (millis() - ultimaVerificacao > 30000) {
    ultimaVerificacao = millis();

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi caiu. Tentando reconectar...");
      atualizarLeds(false);
      conectarWiFi();
    }
  }

  delay(10);
}