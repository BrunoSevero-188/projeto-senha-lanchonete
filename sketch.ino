#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Keypad.h>

// ============================================================
// CONFIGURAÇÃO DE REDE WI-FI
// No Wokwi: Wokwi-GUEST / senha vazia
// Em hardware físico: configure via portal cativo (segure # por 3s)
// ============================================================
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// ============================================================
// CONFIGURAÇÃO MQTT (opcional - para acesso remoto)
// ============================================================
const char* MQTT_HOST = "broker.emqx.io";
const uint16_t MQTT_PORT = 1883;
const char* TOPICO_ESTADO   = "lanchonete/senhas/brunosevero-7f3a9c/estado";
const char* TOPICO_COMANDO  = "lanchonete/senhas/brunosevero-7f3a9c/comando";
const char* TOPICO_STATUS   = "lanchonete/senhas/brunosevero-7f3a9c/status";

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
unsigned long ultimaTentativaWiFi = 0;
unsigned long ultimaTentativaMQTT = 0;

// ============================================================
// SERVIDOR WEB LOCAL (painel acessível via http://IP_DO_ESP32)
// ============================================================
WebServer server(80);

// ============================================================
// PINOS (conforme diagram.json)
// ============================================================
#define TFT_CS        5
#define TFT_DC        2
#define TFT_RST       15
#define TFT_MOSI      23
#define TFT_SCK       18
#define TFT_MISO      19
#define BUZZER        4
#define LED_VERDE     16   // Verde = Sucesso / Confirmação
#define LED_VERMELHO  17   // Vermelho = Apagar / Limpar / Erro

Adafruit_ILI9341 tela(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST, TFT_MISO);

// ============================================================
// TECLADO 4x4
// ============================================================
const byte LINHAS = 4;
const byte COLUNAS = 4;
char teclas[LINHAS][COLUNAS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte pinosLinhas[LINHAS] = {13, 12, 14, 27};
byte pinosColunas[COLUNAS] = {26, 25, 33, 32};
Keypad teclado = Keypad(makeKeymap(teclas), pinosLinhas, pinosColunas, LINHAS, COLUNAS);

// ============================================================
// ESTADO DO SISTEMA
// ============================================================
String entrada = "";              // Buffer de digitação (0-4 dígitos)
String senhaAtual = "";           // Senha sendo exibida no painel
String horarioSenhaAtual = "";    // Horário da senha atual
String historico[5];              // Últimas 5 senhas
String horariosHistorico[5];      // Horários do histórico

// Estados de conexão
bool wifiConectado = false;
bool mqttConectado = false;
unsigned long ultimoLoopMQTT = 0;
unsigned long ultimaAtualizacaoWeb = 0;

// ============================================================
// CORES DO DISPLAY
// ============================================================
#define COR_FUNDO       ILI9341_BLACK
#define COR_PAINEL      tela.color565(20, 30, 50)
#define COR_DESTAQUE    ILI9341_YELLOW
#define COR_VERDE       ILI9341_GREEN
#define COR_VERMELHO    ILI9341_RED
#define COR_BRANCO      ILI9341_WHITE
#define COR_CINZA       tela.color565(120, 120, 120)
#define COR_CIANO       ILI9341_CYAN
#define COR_LARANJA     tela.color565(255, 150, 0)

// ============================================================
// HTML DO PAINEL WEB (armazenado em PROGMEM)
// ============================================================
const char HTML_PAINEL[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Painel de Senhas - Lanchonete</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
:root{--bg:#0d1b2a;--panel:#1b2a4a;--accent:#ffd60a;--green:#00e676;--red:#ff4444;--text:#fff;--border:#2d4466;--muted:#888}
body{min-height:100vh;background:var(--bg);color:var(--text);font-family:system-ui,sans-serif;display:flex;flex-direction:column;align-items:center;padding:20px}
.container{width:100%;max-width:600px}
.header{display:flex;justify-content:space-between;align-items:center;margin-bottom:20px;padding:15px;background:var(--panel);border:1px solid var(--border);border-radius:12px}
.header h1{font-size:22px;color:var(--accent)}
.status{display:flex;align-items:center;gap:8px;font-size:14px}
.dot{width:10px;height:10px;border-radius:50%}
.dot.online{background:var(--green)} .dot.offline{background:var(--red)} .dot.mqtt{background:var(--accent)}
.main{background:var(--panel);border:1px solid var(--border);border-radius:16px;padding:25px;box-shadow:0 10px 40px rgba(0,0,0,.3)}
.current{text-align:center;margin-bottom:25px}
.label{font-size:14px;color:var(--muted);font-weight:bold;letter-spacing:2px;margin-bottom:8px}
.number{font-size:clamp(80px,18vw,120px);font-weight:900;font-family:monospace;color:var(--accent);line-height:1}
.time{font-size:14px;color:var(--muted);margin-top:8px;font-family:monospace}
.controls{display:grid;grid-template-columns:repeat(2,1fr);gap:12px;margin-bottom:25px}
.btn{padding:18px;border:0;border-radius:10px;font-size:16px;font-weight:bold;cursor:pointer;transition:filter .2s}
.btn:hover{filter:brightness(1.1)} .btn:active{transform:scale(.98)}
.btn-primary{background:var(--accent);color:#0d1b2a}
.btn-danger{background:var(--red);color:#fff}
.btn-success{background:var(--green);color:#0d1b2a}
.history{margin-top:10px}
.history h3{font-size:16px;margin-bottom:12px;color:var(--accent)}
.history-list{display:grid;grid-template-columns:repeat(auto-fit,minmax(100px,1fr));gap:10px}
.history-item{background:#14253d;border:1px solid var(--border);border-radius:10px;padding:15px;text-align:center}
.history-num{font-size:28px;font-weight:bold;font-family:monospace;color:var(--accent)}
.history-time{font-size:11px;color:var(--muted);margin-top:4px;font-family:monospace}
.empty{grid-column:1/-1;color:var(--muted);padding:20px;text-align:center}
.footer{margin-top:20px;text-align:center;font-size:12px;color:var(--muted)}
input[type="number"]{width:100%;padding:12px;border:1px solid var(--border);border-radius:8px;background:#14253d;color:var(--text);font-size:16px;text-align:center}
.input-group{display:flex;gap:8px;margin-bottom:15px}
</style>
</head>
<body>
<div class="container">
<header class="header">
<h1>🍔 Painel de Senhas</h1>
<div class="status">
<span class="dot" id="wifiDot"></span>
<span id="wifiStatus">WiFi</span>
<span class="dot" id="mqttDot"></span>
<span id="mqttStatus">MQTT</span>
</div>
</header>
<main class="main">
<section class="current">
<div class="label">SENHA CHAMADA</div>
<div id="number" class="number">----</div>
<div id="time" class="time">Aguardando...</div>
</section>
<div class="controls">
<button class="btn btn-success" onclick="chamarProxima()">➕ Próxima</button>
<button class="btn btn-primary" onclick="promptSenha()">🔢 Chamar Nº</button>
<button class="btn btn-danger" onclick="limparPainel()">🗑️ Limpar Tudo</button>
<button class="btn btn-primary" onclick="sincronizar()">🔄 Sincronizar</button>
</div>
<div class="input-group" style="display:none" id="inputGroup">
<input type="number" id="senhaInput" min="1" max="9999" placeholder="Número da senha (1-9999)">
<button class="btn btn-success" onclick="chamarManual()">Confirmar</button>
<button class="btn" style="background:var(--muted)" onclick="document.getElementById('inputGroup').style.display='none'">Cancelar</button>
</div>
<section class="history">
<h3>📜 Últimas 5 Chamadas</h3>
<div id="history" class="history-list"><div class="empty">Nenhuma senha chamada ainda</div></div>
</section>
</main>
<div class="footer">ESP32 Painel Local • <span id="ipDisplay"></span></div>
</div>
<script>
// Configuração - detecta automaticamente se está no ESP32 ou externo
const API_BASE = window.location.origin;
let ws = null;

async function fetchJSON(path, options = {}) {
  try {
    const r = await fetch(API_BASE + path, { ...options, headers: { 'Content-Type': 'application/json', ...(options.headers||{}) } });
    return await r.json();
  } catch (e) { console.error('Erro API:', e); return null; }
}

function updateStatus(dotId, textId, online, label) {
  const dot = document.getElementById(dotId);
  const txt = document.getElementById(textId);
  dot.className = 'dot ' + (online ? 'online' : 'offline');
  txt.textContent = label + ': ' + (online ? 'OK' : '--');
}

async function carregarEstado() {
  const d = await fetchJSON('/api/estado');
  if (!d) return;

  document.getElementById('number').textContent = d.senha || '----';
  document.getElementById('time').textContent = d.senha ? 'Chamada: ' + d.horario : 'Aguardando...';

  updateStatus('wifiDot', 'wifiStatus', d.wifi, 'WiFi');
  updateStatus('mqttDot', 'mqttStatus', d.mqtt, 'MQTT');

  const hist = document.getElementById('history');
  if (!d.historico || d.historico.length === 0) {
    hist.innerHTML = '<div class="empty">Nenhuma senha chamada ainda</div>';
    return;
  }
  hist.innerHTML = '';
  d.historico.forEach(h => {
    const div = document.createElement('div');
    div.className = 'history-item';
    div.innerHTML = '<div class="history-num">' + h.senha + '</div><div class="history-time">' + h.horario + '</div>';
    hist.appendChild(div);
  });
}

async function chamarProxima() {
  await fetchJSON('/api/proxima', { method: 'POST' });
  await carregarEstado();
}

function promptSenha() {
  document.getElementById('inputGroup').style.display = 'flex';
  document.getElementById('senhaInput').focus();
}

async function chamarManual() {
  const val = document.getElementById('senhaInput').value;
  if (!val) return alert('Digite um número');
  await fetchJSON('/api/chamar', { method: 'POST', body: JSON.stringify({ senha: val }) });
  document.getElementById('inputGroup').style.display = 'none';
  document.getElementById('senhaInput').value = '';
  await carregarEstado();
}

async function limparPainel() {
  if (!confirm('Limpar painel e histórico?')) return;
  await fetchJSON('/api/limpar', { method: 'POST' });
  await carregarEstado();
}

async function sincronizar() {
  await fetchJSON('/api/sincronizar', { method: 'POST' });
  await carregarEstado();
}

// Auto-refresh a cada 2s
setInterval(carregarEstado, 2000);
carregarEstado();

// Mostra IP local
document.getElementById('ipDisplay').textContent = API_BASE;
</script>
</body>
</html>
)rawliteral";

// ============================================================
// FUNÇÕES AUXILIARES
// ============================================================
String horarioAtual() {
  unsigned long segs = millis() / 1000;
  unsigned long h = (segs / 3600) % 24;
  unsigned long m = (segs % 3600) / 60;
  unsigned long s = segs % 60;
  char buf[9];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, s);
  return String(buf);
}

void bipSucesso() {
  tone(BUZZER, 1000, 100); delay(120);
  tone(BUZZER, 1300, 120); delay(140);
  noTone(BUZZER);
}

void bipErro() {
  tone(BUZZER, 250, 350);
  delay(370);
  noTone(BUZZER);
}

void bipTecla() {
  tone(BUZZER, 900, 35);
  delay(40);
  noTone(BUZZER);
}

void bipConfirmacao() {
  tone(BUZZER, 800, 80); delay(100);
  tone(BUZZER, 1000, 80); delay(100);
  noTone(BUZZER);
}

// ============================================================
// LEDs: Verde = Sucesso/Confirmação | Vermelho = Apagar/Limpar/Erro
// ============================================================
void ledSucesso(bool ligado) {
  digitalWrite(LED_VERDE, ligado ? HIGH : LOW);
}

void ledErro(bool ligado) {
  digitalWrite(LED_VERMELHO, ligado ? HIGH : LOW);
}

void ledsApagar() {
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, LOW);
}

// ============================================================
// DESENHO DO DISPLAY TFT
// ============================================================
void desenharCabecalho() {
  tela.fillRect(0, 0, 320, 48, COR_PAINEL);
  tela.drawFastHLine(0, 48, 320, COR_CINZA);

  tela.setTextColor(COR_CIANO);
  tela.setTextSize(2);
  tela.setCursor(10, 10);
  tela.print("PAINEL DE SENHAS");

  // Status WiFi
  tela.setTextSize(1);
  tela.setCursor(200, 12);
  if (wifiConectado) {
    tela.setTextColor(COR_VERDE);
    tela.print("WiFi: OK");
  } else {
    tela.setTextColor(COR_VERMELHO);
    tela.print("WiFi: --");
  }

  // Status MQTT
  tela.setCursor(200, 26);
  if (mqttConectado) {
    tela.setTextColor(COR_VERDE);
    tela.print("MQTT: OK");
  } else {
    tela.setTextColor(COR_LARANJA);
    tela.print("MQTT: --");
  }

  // IP + Web Server
  tela.setCursor(10, 30);
  tela.setTextColor(COR_CINZA);
  tela.setTextSize(1);
  if (wifiConectado) {
    tela.print("Web: http://");
    tela.print(WiFi.localIP().toString());
  } else {
    tela.print("Aguardando conexao WiFi...");
  }
}

void desenharSenhaAtual() {
  tela.fillRoundRect(10, 60, 300, 90, 8, COR_PAINEL);
  tela.drawRoundRect(10, 60, 300, 90, 8, COR_CINZA);

  tela.setTextColor(COR_CINZA);
  tela.setTextSize(2);
  tela.setCursor(20, 70);
  tela.print("SENHA ATUAL");

  tela.setTextColor(COR_DESTAQUE);
  tela.setTextSize(5);
  String exibir = senhaAtual.length() ? senhaAtual : "----";
  int16_t x1, y1;
  uint16_t w, h;
  tela.getTextBounds(exibir.c_str(), 0, 0, &x1, &y1, &w, &h);
  int x = 160 - w / 2;
  tela.setCursor(x, 95);
  tela.print(exibir);

  if (horarioSenhaAtual.length()) {
    tela.setTextColor(COR_CINZA);
    tela.setTextSize(1);
    tela.setCursor(20, 138);
    tela.print("Chamada: ");
    tela.print(horarioSenhaAtual);
  }
}

void desenharEntrada() {
  tela.fillRoundRect(10, 160, 300, 55, 8, COR_PAINEL);
  tela.drawRoundRect(10, 160, 300, 55, 8, COR_CINZA);

  tela.setTextColor(COR_BRANCO);
  tela.setTextSize(2);
  tela.setCursor(20, 170);
  tela.print("DIGITE SENHA: ");

  String mostrar = "";
  for (int i = 0; i < 4; i++) {
    if (i < entrada.length()) mostrar += entrada[i];
    else mostrar += "_";
  }

  tela.setTextColor(COR_DESTAQUE);
  tela.setTextSize(3);
  tela.setCursor(20, 195);
  tela.print(mostrar);
}

void desenharHistorico() {
  tela.fillRoundRect(10, 225, 300, 95, 8, COR_PAINEL);
  tela.drawRoundRect(10, 225, 300, 95, 8, COR_CINZA);

  tela.setTextColor(COR_CINZA);
  tela.setTextSize(1);
  tela.setCursor(20, 232);
  tela.print("ULTIMAS 5 CHAMADAS:");

  int y = 248;
  for (int i = 0; i < 3; i++) {
    if (historico[i].length()) {
      tela.setTextColor(COR_BRANCO);
      tela.setTextSize(2);
      tela.setCursor(20, y);
      tela.print(historico[i]);
      tela.setTextColor(COR_CINZA);
      tela.setTextSize(1);
      tela.setCursor(100, y + 4);
      tela.print(horariosHistorico[i]);
      y += 28;
    }
  }

  if (historico[3].length() || historico[4].length()) {
    tela.setTextColor(COR_CINZA);
    tela.setTextSize(1);
    tela.setCursor(20, y + 2);
    tela.print("+ mais no painel web");
  }
}

void desenharBotoes() {
  tela.fillRect(0, 280, 320, 40, COR_PAINEL);
  tela.drawFastHLine(0, 280, 320, COR_CINZA);

  tela.setTextColor(COR_CINZA);
  tela.setTextSize(1);
  tela.setCursor(10, 286);
  tela.print("[A] Proxima   [B] Limpar Tudo");
  tela.setCursor(10, 300);
  tela.print("[*] Apagar    [#] Confirmar");

  if (entrada.length()) {
    tela.setTextColor(COR_DESTAQUE);
    tela.setCursor(200, 286);
    tela.print("Entrada: ");
    tela.print(entrada);
  }
}

void desenharTelaCompleta(const String& mensagem = "") {
  tela.fillScreen(COR_FUNDO);
  desenharCabecalho();
  desenharSenhaAtual();
  desenharEntrada();
  desenharHistorico();
  desenharBotoes();

  if (mensagem.length()) {
    tela.fillRoundRect(30, 120, 260, 35, 6, tela.color565(0, 0, 80));
    tela.setTextColor(COR_BRANCO);
    tela.setTextSize(2);
    int16_t x1, y1;
    uint16_t w, h;
    tela.getTextBounds(mensagem.c_str(), 0, 0, &x1, &y1, &w, &h);
    int x = 160 - w / 2;
    tela.setCursor(x, 128);
    tela.print(mensagem);
  }
}

void desenharTelaSimples(const String& titulo, const String& msg, uint16_t corFundo) {
  tela.fillScreen(COR_FUNDO);
  tela.fillRoundRect(20, 80, 280, 160, 10, corFundo);

  tela.setTextColor(COR_BRANCO);
  tela.setTextSize(2);
  int16_t x1, y1;
  uint16_t w, h;

  tela.getTextBounds(titulo.c_str(), 0, 0, &x1, &y1, &w, &h);
  tela.setCursor(160 - w/2, 100);
  tela.print(titulo);

  tela.setTextSize(3);
  tela.getTextBounds(msg.c_str(), 0, 0, &x1, &y1, &w, &h);
  tela.setCursor(160 - w/2, 145);
  tela.print(msg);

  tela.setTextColor(COR_CINZA);
  tela.setTextSize(1);
  tela.setCursor(20, 260);
  tela.print("Aguarde...");
}

// ============================================================
// SERIALIZAR ESTADO PARA JSON (usado por Web e MQTT)
// ============================================================
void serializarEstado(JsonDocument& doc) {
  doc["senha"] = senhaAtual;
  doc["horario"] = horarioSenhaAtual;
  doc["wifi"] = wifiConectado;
  doc["mqtt"] = mqttConectado;
  doc["ip"] = wifiConectado ? WiFi.localIP().toString() : "";

  JsonArray lista = doc.createNestedArray("historico");
  for (int i = 0; i < 5; i++) {
    if (historico[i].length()) {
      JsonObject item = lista.createNestedObject();
      item["senha"] = historico[i];
      item["horario"] = horariosHistorico[i];
    }
  }
}

// ============================================================
// MQTT - PUBLICAR ESTADO
// ============================================================
void publicarEstado() {
  if (!mqttConectado) return;

  StaticJsonDocument<768> doc;
  doc["tipo"] = "estado";
  serializarEstado(doc);

  char payload[768];
  size_t tamanho = serializeJson(doc, payload, sizeof(payload));
  mqtt.publish(TOPICO_ESTADO, reinterpret_cast<const uint8_t*>(payload), tamanho, true);
  Serial.print("[MQTT] Estado publicado: ");
  Serial.println(payload);
}

// ============================================================
// LÓGICA DE SENHAS (comum: teclado, web, MQTT)
// ============================================================
void executarChamada(String numero) {
  numero.trim();
  if (numero.length() == 0) return;

  int valor = numero.toInt();
  if (valor < 1 || valor > 9999) return;

  char formatada[5];
  snprintf(formatada, sizeof(formatada), "%04d", valor);
  String novaSenha = String(formatada);

  if (novaSenha == senhaAtual) return;

  // Move atual para histórico
  if (senhaAtual.length()) {
    for (int i = 4; i > 0; i--) {
      historico[i] = historico[i - 1];
      horariosHistorico[i] = horariosHistorico[i - 1];
    }
    historico[0] = senhaAtual;
    horariosHistorico[0] = horarioSenhaAtual;
  }

  senhaAtual = novaSenha;
  horarioSenhaAtual = horarioAtual();
  entrada = "";

  // LED VERDE = Sucesso/Confirmação
  ledSucesso(true);
  ledErro(false);
  bipSucesso();

  desenharTelaCompleta("Senha chamada!");
  Serial.println("[SENHA] Chamada: " + senhaAtual);

  delay(1500);
  ledSucesso(false);

  publicarEstado();
  desenharTelaCompleta();
}

void chamarSenha(String numero, bool publicar = true) {
  executarChamada(numero);
}

void proximaSenha(bool publicar = true) {
  int proxima = senhaAtual.length() ? senhaAtual.toInt() + 1 : 1;
  if (proxima > 9999) proxima = 1;
  executarChamada(String(proxima));
}

void limparPainel(bool publicar = true) {
  senhaAtual = "";
  horarioSenhaAtual = "";
  entrada = "";
  for (int i = 0; i < 5; i++) {
    historico[i] = "";
    horariosHistorico[i] = "";
  }

  // LED VERMELHO = Apagar/Limpar
  ledErro(true);
  ledSucesso(false);
  bipConfirmacao();

  desenharTelaSimples("LIMPO", "Painel reiniciado", COR_VERMELHO);
  delay(1000);
  ledErro(false);

  if (publicar) publicarEstado();
  desenharTelaCompleta();
}

void apagarUltimoDigito() {
  if (entrada.length()) {
    entrada.remove(entrada.length() - 1);
    bipTecla();
    desenharTelaCompleta();
  }
}

// ============================================================
// SERVIDOR WEB - ROTAS
// ============================================================
void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", HTML_PAINEL);
}

void handleAPIEstado() {
  StaticJsonDocument<768> doc;
  serializarEstado(doc);
  String resp;
  serializeJson(doc, resp);
  server.send(200, "application/json", resp);
}

void handleAPIChamar() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<128> doc;
    deserializeJson(doc, server.arg("plain"));
    const char* num = doc["senha"] | "";
    if (strlen(num) > 0) executarChamada(String(num));
  }
  handleAPIEstado();
}

void handleAPIProxima() {
  proximaSenha();
  handleAPIEstado();
}

void handleAPILimpar() {
  limparPainel();
  handleAPIEstado();
}

void handleAPISincronizar() {
  publicarEstado();
  handleAPIEstado();
}

void handleNotFound() {
  server.send(404, "application/json", "{\"erro\":\"Nao encontrado\"}");
}

// ============================================================
// MQTT - PROCESSAR COMANDOS RECEBIDOS
// ============================================================
void processarComando(char* payload, unsigned int length) {
  StaticJsonDocument<256> doc;
  DeserializationError erro = deserializeJson(doc, payload, length);
  if (erro) {
    Serial.println("[MQTT] Comando JSON invalido.");
    return;
  }

  const char* acao = doc["acao"] | "";
  Serial.print("[MQTT] Comando: ");
  Serial.println(acao);

  if (strcmp(acao, "proxima") == 0) {
    proximaSenha();
  } else if (strcmp(acao, "chamar") == 0) {
    const char* numero = doc["senha"] | "";
    chamarSenha(String(numero));
  } else if (strcmp(acao, "limpar") == 0) {
    limparPainel();
  } else if (strcmp(acao, "sincronizar") == 0) {
    publicarEstado();
  }
}

void callbackMQTT(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, TOPICO_COMANDO) != 0) return;
  static char mensagem[257];
  if (length >= sizeof(mensagem)) return;
  memcpy(mensagem, payload, length);
  mensagem[length] = '\0';
  processarComando(mensagem, length);
}

// ============================================================
// CONEXÃO WI-FI
// ============================================================
void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConectado) {
      wifiConectado = true;
      Serial.print("[WiFi] Conectado! IP: ");
      Serial.println(WiFi.localIP());
      Serial.print("[Web] Painel disponivel em: http://");
      Serial.println(WiFi.localIP().toString());
      desenharTelaCompleta();
    }
    return;
  }

  wifiConectado = false;
  unsigned long agora = millis();
  if (agora - ultimaTentativaWiFi < 10000 && ultimaTentativaWiFi != 0) return;
  ultimaTentativaWiFi = agora;

  Serial.print("[WiFi] Conectando a ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);
}

// ============================================================
// CONEXÃO MQTT
// ============================================================
void conectarMQTT() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  unsigned long agora = millis();
  if (agora - ultimaTentativaMQTT < 5000 && ultimaTentativaMQTT != 0) return;
  ultimaTentativaMQTT = agora;

  String clientId = "ESP32-Lanchonete-" + String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFF), HEX);

  Serial.print("[MQTT] Conectando... ");
  if (mqtt.connect(clientId.c_str(), TOPICO_STATUS, 0, true, "offline")) {
    Serial.println("OK!");
    mqttConectado = true;
    mqtt.publish(TOPICO_STATUS, "online", true);
    mqtt.subscribe(TOPICO_COMANDO);
    publicarEstado();
    desenharTelaCompleta();
  } else {
    mqttConectado = false;
    Serial.print("Falhou, codigo=");
    Serial.println(mqtt.state());
  }
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n=== PAINEL DE SENHAS - ESP32 + ILI9341 + Web + MQTT ===");
  Serial.println("Teclas: [0-9] Digitar | [*] Apagar | [#] Confirmar");
  Serial.println("        [A] Proxima  | [B] Limpar Tudo");
  Serial.println("LEDs:   Verde = Sucesso | Vermelho = Limpar/Erro");

  pinMode(BUZZER, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  ledsApagar();
  noTone(BUZZER);

  // Inicializa TFT
  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  tela.begin();
  tela.setRotation(1);
  tela.fillScreen(COR_FUNDO);

  // Splash
  tela.setTextColor(COR_CIANO);
  tela.setTextSize(2);
  tela.setCursor(40, 100);
  tela.print("PAINEL DE SENHAS");
  tela.setTextColor(COR_DESTAQUE);
  tela.setTextSize(3);
  tela.setCursor(80, 140);
  tela.print("ILI9341 320x240");
  tela.setTextColor(COR_CINZA);
  tela.setTextSize(1);
  tela.setCursor(40, 200);
  tela.print("Iniciando WiFi + Web Server...");
  delay(1500);

  // Servidor Web Local
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/estado", HTTP_GET, handleAPIEstado);
  server.on("/api/chamar", HTTP_POST, handleAPIChamar);
  server.on("/api/proxima", HTTP_POST, handleAPIProxima);
  server.on("/api/limpar", HTTP_POST, handleAPILimpar);
  server.on("/api/sincronizar", HTTP_POST, handleAPISincronizar);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("[Web] Servidor iniciado na porta 80");

  // MQTT
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(callbackMQTT);
  mqtt.setBufferSize(1024);

  conectarWiFi();
}

// ============================================================
// LOOP PRINCIPAL
// ============================================================
void loop() {
  // Servidor Web (prioridade alta - resposta rápida)
  server.handleClient();

  // Reconexão WiFi
  conectarWiFi();

  // Reconexão MQTT
  if (wifiConectado) conectarMQTT();

  // Loop MQTT
  if (mqttConectado) {
    mqtt.loop();
    if (!mqtt.connected()) {
      mqttConectado = false;
      desenharTelaCompleta();
    }
  }

  // Atualiza status MQTT a cada 5s
  if (millis() - ultimoLoopMQTT > 5000) {
    ultimoLoopMQTT = millis();
    if (mqttConectado != mqtt.connected()) {
      mqttConectado = mqtt.connected();
      desenharTelaCompleta();
    }
  }

  // ========================================================
  // TECLADO - ENTRADA MANUAL DE SENHAS
  // ========================================================
  char tecla = teclado.getKey();
  if (tecla) {
    Serial.printf("[TECLA] %c\n", tecla);

    if (tecla >= '0' && tecla <= '9') {
      if (entrada.length() < 4) {
        entrada += tecla;
        bipTecla();
        desenharTelaCompleta();
      } else {
        bipErro();
      }
    }
    else if (tecla == '*') {
      apagarUltimoDigito();
    }
    else if (tecla == '#') {
      if (entrada.length()) chamarSenha(entrada);
      else { bipErro(); desenharTelaCompleta("Digite uma senha"); delay(800); desenharTelaCompleta(); }
    }
    else if (tecla == 'A') {
      proximaSenha();
    }
    else if (tecla == 'B') {
      limparPainel();
    }
    else if (tecla == 'D') {
      bipTecla();
      desenharTelaCompleta("Use # para confirmar");
      delay(800);
      desenharTelaCompleta();
    }
    else if (tecla == 'C') {
      bipTecla();
    }
    else {
      bipErro();
    }
  }
}