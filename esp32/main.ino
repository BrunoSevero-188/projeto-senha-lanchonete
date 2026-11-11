/*
  Sistema de Senhas - Lanchonete
  ESP32 com LCD 16x2 I2C, Keypad 4x4, Buzzer, LEDs

  Conexões (conforme diagram.json):
  - LCD I2C: SDA=GPIO22, SCL=GPIO23 (com pull-ups 4.7k)
  - Keypad 4x4:
      R1=GPIO13, R2=GPIO12, R3=GPIO14, R4=GPIO27
      C1=GPIO26, C2=GPIO25, C3=GPIO33, C4=GPIO32
  - Buzzer: GPIO18
  - LED Verde: GPIO19
  - LED Vermelho: GPIO21

  Keypad Layout (wokwi-membrane-keypad):
  1 2 3 A   <- A = APAGAR
  4 5 6 B
  7 8 9 C   <- C = CONFIRMAR
  * 0 # D

  Bibliotecas:
  - LiquidCrystal_I2C
  - Keypad
  - ArduinoJson
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

// ==================== CONFIGURAÇÕES WIFI ====================
const char* WIFI_SSID = "SUA_REDE_WIFI";
const char* WIFI_PASSWORD = "SUA_SENHA_WIFI";

// ==================== SERVIDOR WEB ====================
WebServer server(80);

// ==================== LCD 16x2 I2C ====================
// Endereço padrão 0x27 (teste 0x3F se não funcionar)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ==================== KEYPAD 4x4 ====================
const byte LINHAS = 4;
const byte COLUNAS = 4;

// Layout EXATO do wokwi-membrane-keypad
char teclas[LINHAS][COLUNAS] = {
  {'1', '2', '3', 'A'},  // Linha 0 (R1=GPIO13)
  {'4', '5', '6', 'B'},  // Linha 1 (R2=GPIO12)
  {'7', '8', '9', 'C'},  // Linha 2 (R3=GPIO14)
  {'*', '0', '#', 'D'}   // Linha 3 (R4=GPIO27)
};

// Pinos conforme diagram.json
byte pinosLinhas[LINHAS] = {13, 12, 14, 27};   // R1, R2, R3, R4
byte pinosColunas[COLUNAS] = {26, 25, 33, 32}; // C1, C2, C3, C4

Keypad keypad = Keypad(makeKeymap(teclas), pinosLinhas, pinosColunas, LINHAS, COLUNAS);

// ==================== PINOS DE SAÍDA ====================
const int BUZZER_PIN = 18;
const int LED_VERDE = 19;
const int LED_VERMELHO = 21;

// ==================== CONSTANTES DO SISTEMA ====================
const int MAX_SENHA = 9999;
const int MIN_SENHA = 1;
const int LIMITE_HISTORICO = 5;
const int MAX_DIGITOS = 4;

// EEPROM
const int EEPROM_SIZE = 512;
const int ADDR_SENHA_ATUAL = 0;
const int ADDR_TOTAL_HIST = 4;
const int ADDR_HISTORICO = 8;

// ==================== ESTRUTURAS ====================
struct RegistroSenha {
  int numero;
  char horario[9];  // HH:MM:SS
  bool valido;
};

struct EstadoSistema {
  int senhaAtual;
  RegistroSenha historico[LIMITE_HISTORICO];
  int totalHistorico;
};

EstadoSistema estado;

// Buffer de digitação
char bufferDigita[5] = {0};
int posDigita = 0;
bool modoConfirmacao = false;
bool modoManual = false;
unsigned long ultimaTecla = 0;

// ==================== FUNÇÕES AUXILIARES ====================
String obterHorario() {
  unsigned long segs = millis() / 1000;
  int h = (segs / 3600) % 24;
  int m = (segs % 3600) / 60;
  int s = segs % 60;
  char buf[9];
  sprintf(buf, "%02d:%02d:%02d", h, m, s);
  return String(buf);
}

// --- SONS ---
void tocarTom(int freq, int duracao) {
  tone(BUZZER_PIN, freq, duracao);
  delay(duracao + 10);
  noTone(BUZZER_PIN);
}

void somChamada() {
  // 3 bips de 880Hz (igual ao web)
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 880, 100);
    delay(130);
  }
  noTone(BUZZER_PIN);
}

void somConfirmacao() {
  tone(BUZZER_PIN, 600, 80); delay(100);
  tone(BUZZER_PIN, 800, 80); delay(100);
  tone(BUZZER_PIN, 1000, 80); delay(100);
  noTone(BUZZER_PIN);
}

void somErro() {
  tone(BUZZER_PIN, 200, 300);
  delay(320);
  noTone(BUZZER_PIN);
}

void somTecla() {
  tone(BUZZER_PIN, 1200, 30);
  delay(40);
  noTone(BUZZER_PIN);
}

// --- LEDs ---
void atualizarLeds(bool wifiOk) {
  digitalWrite(LED_VERDE, wifiOk ? HIGH : LOW);
  digitalWrite(LED_VERMELHO, wifiOk ? LOW : HIGH);
}

// ==================== LCD ====================
void lcdLimpar() { lcd.clear(); lcd.setCursor(0, 0); }

void lcdCentralizado(int linha, const char* texto) {
  int len = strlen(texto);
  int col = max(0, (16 - len) / 2);
  lcd.setCursor(col, linha);
  lcd.print(texto);
}

void mostrarEspera() {
  lcdLimpar();
  lcdCentralizado(0, "Digite a");
  lcdCentralizado(1, "proxima senha");
}

void mostrarDigitacao() {
  lcdLimpar();
  lcd.setCursor(0, 0);
  lcd.print("Senha: ");
  lcd.print(bufferDigita);
  for (int i = posDigita; i < MAX_DIGITOS; i++) lcd.print("_");
  lcdCentralizado(1, "C=OK  A=Apaga D=Limpa");
}

void mostrarConfirmacao() {
  lcdLimpar();
  lcdCentralizado(0, "Tem certeza?");
  lcdCentralizado(1, "C=Sim  A=Nao");
}

void mostrarSucesso(int senha) {
  lcdLimpar();
  lcdCentralizado(0, "Senha registrada!");
  char buf[17];
  sprintf(buf, "Senha: %04d", senha);
  lcdCentralizado(1, buf);
}

void mostrarErro(const char* msg) {
  lcdLimpar();
  lcdCentralizado(0, "ERRO");
  lcdCentralizado(1, msg);
}

void mostrarIP() {
  lcdLimpar();
  lcdCentralizado(0, "IP do ESP32:");
  lcdCentralizado(1, WiFi.localIP().toString().c_str());
}

// ==================== EEPROM ====================
void salvarEstado() {
  EEPROM.writeInt(ADDR_SENHA_ATUAL, estado.senhaAtual);
  EEPROM.writeInt(ADDR_TOTAL_HIST, estado.totalHistorico);
  for (int i = 0; i < LIMITE_HISTORICO; i++) {
    EEPROM.put(ADDR_HISTORICO + i * sizeof(RegistroSenha), estado.historico[i]);
  }
  EEPROM.commit();
}

void carregarEstado() {
  EEPROM.begin(EEPROM_SIZE);
  estado.senhaAtual = EEPROM.readInt(ADDR_SENHA_ATUAL);
  estado.totalHistorico = EEPROM.readInt(ADDR_TOTAL_HIST);

  if (estado.senhaAtual < MIN_SENHA || estado.senhaAtual > MAX_SENHA) estado.senhaAtual = 0;
  if (estado.totalHistorico < 0 || estado.totalHistorico > LIMITE_HISTORICO) estado.totalHistorico = 0;

  for (int i = 0; i < LIMITE_HISTORICO; i++) {
    EEPROM.get(ADDR_HISTORICO + i * sizeof(RegistroSenha), estado.historico[i]);
    if (!estado.historico[i].valido) estado.historico[i] = {0, "", false};
  }
}

// ==================== LÓGICA DE SENHAS ====================
void adicionarHistorico(int senha) {
  if (estado.senhaAtual > 0) {
    for (int i = LIMITE_HISTORICO - 1; i > 0; i--) {
      estado.historico[i] = estado.historico[i - 1];
    }
    estado.historico[0] = {estado.senhaAtual, "", true};
    strcpy(estado.historico[0].horario, obterHorario().c_str());
    if (estado.totalHistorico < LIMITE_HISTORICO) estado.totalHistorico++;
  }
  estado.senhaAtual = senha;
  salvarEstado();
}

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

void resetarContador() {
  estado.senhaAtual = 0;
  estado.totalHistorico = 0;
  for (int i = 0; i < LIMITE_HISTORICO; i++) estado.historico[i] = {0, "", false};
  salvarEstado();
  somConfirmacao();
  lcdLimpar();
  lcdCentralizado(0, "Contador resetado");
  delay(1500);
  mostrarEspera();
}

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

// ==================== SERVIDOR WEB ====================
void handleRoot() {
  String html = FPSTR(HTML_INDEX);
  server.send(200, "text/html", html);
}

void handleApiSenha() {
  StaticJsonDocument<256> doc;
  doc["senha"] = estado.senhaAtual > 0 ? String(estado.senhaAtual) : "---";
  doc["horario"] = obterHorario();
  doc["online"] = true;
  doc["historico_count"] = estado.totalHistorico;
  String resp; serializeJson(doc, resp);
  server.send(200, "application/json", resp);
}

void handleApiHistorico() {
  StaticJsonDocument<512> doc;
  JsonArray arr = doc.createNestedArray("historico");
  for (int i = 0; i < estado.totalHistorico; i++) {
    JsonObject obj = arr.createNestedObject();
    obj["senha"] = estado.historico[i].numero;
    obj["horario"] = estado.historico[i].horario;
  }
  String resp; serializeJson(doc, resp);
  server.send(200, "application/json", resp);
}

void handleApiChamar() { chamarProxima(); handleApiSenha(); }
void handleApiReset() { resetarContador(); handleApiSenha(); }

void handleApiSet() {
  if (server.hasArg("senha")) {
    int num = server.arg("senha").toInt();
    definirSenhaManual(num);
  }
  handleApiSenha();
}

void handleNotFound() { server.send(404, "text/plain", "Nao encontrado"); }

// HTML embutido (PROGMEM) - versão compacta para ESP32
const char HTML_INDEX[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="pt-BR"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>Painel de Senhas</title><style>*{margin:0;padding:0;box-sizing:border-box}:root{--fundo:#0d1b2a;--painel:#1b2a4a;--destaque:#ffd60a;--verde:#00e676;--texto:#fff;--borda:#2d4466}body{min-height:100vh;background:var(--fundo);color:var(--texto);font-family:sans-serif;display:flex;justify-content:center;align-items:center;padding:20px}.painel{width:100%;max-width:1000px;background:var(--painel);border:1px solid var(--borda);border-radius:20px;padding:30px;box-shadow:0 15px 50px rgba(0,0,0,0.4)}.cabecalho{display:flex;justify-content:space-between;align-items:center;margin-bottom:20px}.identificador{color:var(--destaque);font-size:12px;font-weight:bold;letter-spacing:2px}h1{font-size:28px}.status{display:flex;align-items:center;gap:8px;color:var(--verde)}.status-indicador{width:10px;height:10px;background:var(--verde);border-radius:50%}.senha-atual{background:#14253d;border:2px solid var(--borda);border-radius:16px;min-height:250px;display:flex;flex-direction:column;justify-content:center;align-items:center;padding:20px}.legenda{color:#888;font-size:14px;font-weight:bold;letter-spacing:3px}.numero-senha{color:var(--destaque);font-size:clamp(80px,15vw,150px);font-weight:900;font-family:monospace;margin:10px 0}.historico{margin-top:20px}.historico h2{font-size:20px;margin-bottom:15px}.lista-historico{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:10px;list-style:none}.lista-historico li{background:#14253d;border:1px solid var(--borda);border-radius:10px;padding:15px;text-align:center}.historico-senha{font-size:24px;font-weight:bold;font-family:monospace}.historico-horario{font-size:11px;color:#888;margin-top:5px;font-family:monospace}.controles{display:flex;gap:10px;justify-content:center;margin-top:20px;flex-wrap:wrap}button{background:var(--destaque);color:#0d1b2a;border:none;border-radius:10px;padding:15px 25px;font-size:16px;font-weight:bold;cursor:pointer;transition:0.2s}button:hover{background:#ffe078;transform:translateY(-2px)}button:active{transform:scale(0.97)}.config{display:flex;gap:20px;justify-content:center;margin-top:15px;font-size:14px;color:#888}.config label{display:flex;align-items:center;gap:8px;cursor:pointer}.config input{width:18px;height:18px;accent-color:var(--destaque)}</style></head><body><main class="painel"><header class="cabecalho"><div><span class="identificador">PAINEL DE ATENDIMENTO</span><h1>Senha Atual</h1></div><div class="status"><span class="status-indicador"></span><span>Conectado</span></div></header><section class="senha-atual" aria-live="assertive"><span class="legenda">SENHA CHAMADA</span><div id="senhaAtual" class="numero-senha">---</div><p id="msg">Aguardando...</p></section><section class="historico"><h2>Últimas senhas</h2><ul id="historico" class="lista-historico"><li style="grid-column:1/-1;color:#888;padding:20px">Nenhuma senha ainda</li></ul></section><div class="controles"><button id="btnTeste" onclick="testar()">🔔 Chamar (Teste)</button><button id="btnReset" onclick="resetar()" style="background:#ff6b6b;color:white">🗑️ Reset</button></div><div class="config"><label><input type="checkbox" id="som" checked><span>Som</span></label><label><input type="checkbox" id="auto" checked><span>Auto</span></label></div></main><script>const senhaEl=document.getElementById('senhaAtual'), histEl=document.getElementById('historico'), msgEl=document.getElementById('msg');let som=true, auto=true;async function carregar(){try{const r=await fetch('/api/senha');const d=await r.json();atualizar(d);}catch(e){console.log(e)}}function atualizar(d){senhaEl.textContent=d.senha||'---';msgEl.textContent=d.senha!=='---'?'Senha '+d.senha+' chamada!':'Aguardando...';if(d.senha!=='---'&&som){const ctx=new (window.AudioContext||window.webkitAudioContext)();[0,180,360].forEach(t=>{const o=ctx.createOscillator(),g=ctx.createGain();o.type='sine';o.frequency.value=880;g.gain.setValueAtTime(0.0001,ctx.currentTime+t/1000);g.gain.exponentialRampToValueAtTime(0.3,ctx.currentTime+t/1000+0.02);g.gain.exponentialRampToValueAtTime(0.0001,ctx.currentTime+t/1000+0.15);o.connect(g);g.connect(ctx.destination);o.start(ctx.currentTime+t/1000);o.stop(ctx.currentTime+t/1000+0.15);});}}async function carregarHist(){try{const r=await fetch('/api/historico');const d=await r.json();histEl.innerHTML='';d.historico.forEach(h=>{const li=document.createElement('li');li.innerHTML='<div class="historico-senha">'+h.senha+'</div><div class="historico-horario">'+h.horario+'</div>';histEl.appendChild(li);});}catch(e){}}function testar(){fetch('/api/chamar',{method:'POST'}).then(carregar).then(carregarHist)}function resetar(){if(confirm('Resetar contador?')){fetch('/api/reset',{method:'POST'}).then(carregar).then(carregarHist)}}document.getElementById('som').onchange=e=>som=e.target.checked;document.getElementById('auto').onchange=e=>{auto=e.target.checked;if(auto)interval=setInterval(carregar,2000);else clearInterval(interval)};let interval=setInterval(carregar,2000);carregar();carregarHist();</script></body></html>
)rawliteral";

// ==================== TECLADO - NOVA LÓGICA ====================
// A = APAGAR (backspace)
// C = CONFIRMAR (enter/ok)
// B = CANCELAR/NAO (na confirmação)
// D = LIMPAR TUDO / MODO MANUAL
// * e # = não usados (podem ser usados no futuro)

void processarTecla(char tecla) {
  ultimaTecla = millis();
  somTecla();

  Serial.printf("Tecla: %c | ModoManual: %d | ModoConfirm: %d | Buffer: %s\n",
                tecla, modoManual, modoConfirmacao, bufferDigita);

  // --- MODO CONFIRMAÇÃO ---
  if (modoConfirmacao) {
    if (tecla == 'C') {  // C = CONFIRMAR = SIM
      int senha = atoi(bufferDigita);
      if (senha >= MIN_SENHA && senha <= MAX_SENHA) {
        definirSenhaManual(senha);
      } else {
        somErro();
        mostrarErro("Senha invalida");
        delay(1500);
        mostrarEspera();
      }
      limparBuffer();
      modoConfirmacao = false;
      modoManual = false;
    }
    else if (tecla == 'A' || tecla == 'B') {  // A = APAGAR = NÃO / B = CANCELAR = NÃO
      modoConfirmacao = false;
      mostrarDigitacao();
    }
    // Ignora outras teclas
    return;
  }

  // --- MODO DIGITAÇÃO MANUAL ---
  if (modoManual) {
    if (tecla >= '0' && tecla <= '9') {
      if (posDigita < MAX_DIGITOS) {
        bufferDigita[posDigita++] = tecla;
        bufferDigita[posDigita] = '\0';
      }
    }
    else if (tecla == 'A') {  // A = APAGAR (backspace)
      if (posDigita > 0) {
        bufferDigita[--posDigita] = '\0';
      }
    }
    else if (tecla == 'D') {  // D = LIMPAR TUDO
      limparBuffer();
    }
    else if (tecla == 'C') {  // C = CONFIRMAR
      if (posDigita > 0) {
        modoConfirmacao = true;
        mostrarConfirmacao();
      }
    }
    else if (tecla == 'B') {  // B = SAIR modo manual
      modoManual = false;
      limparBuffer();
      mostrarEspera();
      return;
    }
    mostrarDigitacao();
    return;
  }

  // --- MODO ESPERA (inicial) ---
  // D = Entrar modo digitação manual
  if (tecla == 'D') {
    modoManual = true;
    limparBuffer();
    mostrarDigitacao();
  }
  // Outras teclas ignoradas no modo espera
}

void limparBuffer() {
  memset(bufferDigita, 0, sizeof(bufferDigita));
  posDigita = 0;
}

// ==================== WIFI ====================
void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  lcdLimpar();
  lcdCentralizado(0, "Conectando WiFi");
  lcd.setCursor(0, 1);
  lcd.print(WIFI_SSID);

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 30) {
    delay(500);
    lcd.print(".");
    tentativas++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    lcdLimpar();
    lcdCentralizado(0, "WiFi Conectado!");
    lcdCentralizado(1, WiFi.localIP().toString().c_str());
    atualizarLeds(true);
    delay(2000);
  } else {
    lcdLimpar();
    lcdCentralizado(0, "Falha WiFi");
    lcdCentralizado(1, "Modo Offline");
    atualizarLeds(false);
    delay(2000);
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n=== Sistema de Senhas ESP32 ===");
  Serial.println("Keypad: A=Apagar, C=Confirmar, B=Cancelar, D=Manual");

  // Pinos
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, HIGH);
  noTone(BUZZER_PIN);

  // Keypad
  keypad.setDebounceTime(50);

  // LCD I2C (SDA=22, SCL=23)
  Wire.begin(22, 23);
  lcd.init();
  lcd.backlight();
  lcdLimpar();
  lcdCentralizado(0, "Iniciando...");
  lcdCentralizado(1, "v1.0");
  delay(1000);

  // EEPROM
  carregarEstado();

  // WiFi
  conectarWiFi();

  // Servidor Web
  server.on("/", handleRoot);
  server.on("/api/senha", handleApiSenha);
  server.on("/api/historico", handleApiHistorico);
  server.on("/api/chamar", HTTP_POST, handleApiChamar);
  server.on("/api/reset", HTTP_POST, handleApiReset);
  server.on("/api/set", HTTP_POST, handleApiSet);
  server.onNotFound(handleNotFound);
  server.begin();

  // Inicialização
  somConfirmacao();
  delay(100);
  somConfirmacao();

  mostrarEspera();

  Serial.printf("Sistema pronto! IP: %s\n", WiFi.localIP().toString().c_str());
}

// ==================== LOOP ====================
void loop() {
  server.handleClient();

  char tecla = keypad.getKey();
  if (tecla) processarTecla(tecla);

  // Timeout modo manual (30s)
  if (modoManual && millis() - ultimaTecla > 30000) {
    modoManual = false;
    modoConfirmacao = false;
    limparBuffer();
    mostrarEspera();
  }

  // Timeout confirmação (10s)
  if (modoConfirmacao && millis() - ultimaTecla > 10000) {
    modoConfirmacao = false;
    if (modoManual) mostrarDigitacao();
    else mostrarEspera();
  }

  // Reconexão WiFi
  static unsigned long ultimaVerificacao = 0;
  if (millis() - ultimaVerificacao > 30000) {
    ultimaVerificacao = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi caiu, reconectando...");
      atualizarLeds(false);
      conectarWiFi();
    }
  }

  delay(10);
}