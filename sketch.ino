/*
  SISTEMA DE SENHAS - LANCHONETE
  ESP32 + TFT ILI9341 320x240 SPI + Keypad 4x4 + Buzzer + LEDs

  PINOS TFT (VSPI):
  SCK  = GPIO 18 | MOSI = GPIO 23 | MISO = GPIO 19
  CS   = GPIO 5  | DC   = GPIO 2  | RST  = GPIO 15
  BL   = GPIO 21 (backlight PWM)

  PINOS KEYPAD 4x4:
  Linhas R1-R4: 13, 12, 14, 27
  Colunas C1-C4: 26, 25, 33, 32

  PINOS SAÍDA:
  Buzzer = GPIO 4  | LED Verde = GPIO 16 | LED Vermelho = GPIO 17

  TECLADO:
  D = entrar no modo manual / limpar todos os dígitos
  0-9 = digitar número da senha
  A = apagar último dígito / voltar da confirmação
  C = confirmar senha
  B = cancelar e sair do modo manual
  # (segurar 3s) = entrar no modo configuração WiFi

  Bibliotecas externas em libraries.txt:
  Keypad
  Adafruit ILI9341
  Adafruit GFX Library
  ArduinoJson
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Keypad.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <EEPROM.h>
#include <DNSServer.h>

// ==================== CONFIGURAÇÃO WIFI (EEPROM) ====================
const int EEPROM_SIZE = 1024;
const int ADDR_WIFI_SSID = 0;
const int ADDR_WIFI_PASS = 64;
const int ADDR_WIFI_CONFIGURED = 128;
const int ADDR_SENHA_ATUAL = 132;
const int ADDR_TOTAL_HIST = 136;
const int ADDR_HISTORICO = 140;

char wifi_ssid[64] = "Wokwi-GUEST";
char wifi_pass[64] = "";
bool wifi_configurado = false;

// ==================== WIFI E SERVIDOR ====================
WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;
bool modoAP = false;
unsigned long apInicio = 0;
const unsigned long AP_TIMEOUT = 300000; // 5 min

// ==================== TFT ILI9341 ====================
#define TFT_SCK   18
#define TFT_MOSI  23
#define TFT_MISO  19
#define TFT_CS    5
#define TFT_DC    2
#define TFT_RST   15
#define TFT_BL    21

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST, TFT_MISO);

// Cores personalizadas (tema escuro estilo painel)
#define COR_FUNDO       0x0861   // #0d1b2a
#define COR_PAINEL      0x2124   // #1b2a4a
#define COR_DESTAQUE    0xFFB0   // #ffd60a
#define COR_VERDE       0x07E0   // #00e676
#define COR_VERMELHO    0xF800   // #ff0000
#define COR_TEXTO       0xFFFF   // branco
#define COR_TEXTO_FRACO 0x8410   // #888888
#define COR_BORDA       0x4A49   // #2d4466
#define COR_LARANJA     0xFD20   // #ff9500

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
const int BUZZER_PIN = 4;
const int LED_VERDE  = 16;
const int LED_VERMELHO = 17;

// ==================== CONSTANTES ====================
const int MAX_SENHA = 9999;
const int MIN_SENHA = 1;
const int LIMITE_HISTORICO = 5;
const int MAX_DIGITOS = 4;

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
unsigned long teclaPressionadaInicio = 0;
char teclaSegurada = 0;

// ==================== HORÁRIO ====================
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
void tocarTom(int freq, int duracao) {
  tone(BUZZER_PIN, freq, duracao);
  delay(duracao + 10);
  noTone(BUZZER_PIN);
}

void somChamada() {
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

// ==================== LEDs ====================
void atualizarLeds(bool wifiOk) {
  digitalWrite(LED_VERDE, wifiOk ? HIGH : LOW);
  digitalWrite(LED_VERMELHO, wifiOk ? LOW : HIGH);
}

// ==================== EEPROM WIFI ====================
void carregarConfigWiFi() {
  EEPROM.begin(EEPROM_SIZE);
  wifi_configurado = EEPROM.readBool(ADDR_WIFI_CONFIGURED);

  if (wifi_configurado) {
    for (int i = 0; i < 63; i++) {
      char c = EEPROM.readChar(ADDR_WIFI_SSID + i);
      if (c == 0) break;
      wifi_ssid[i] = c;
    }
    for (int i = 0; i < 63; i++) {
      char c = EEPROM.readChar(ADDR_WIFI_PASS + i);
      if (c == 0) break;
      wifi_pass[i] = c;
    }
  }
}

void salvarConfigWiFi(const char* ssid, const char* pass) {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 64; i++) EEPROM.writeChar(ADDR_WIFI_SSID + i, 0);
  for (int i = 0; i < 64; i++) EEPROM.writeChar(ADDR_WIFI_PASS + i, 0);

  for (int i = 0; i < strlen(ssid) && i < 63; i++) EEPROM.writeChar(ADDR_WIFI_SSID + i, ssid[i]);
  for (int i = 0; i < strlen(pass) && i < 63; i++) EEPROM.writeChar(ADDR_WIFI_PASS + i, pass[i]);

  EEPROM.writeBool(ADDR_WIFI_CONFIGURED, true);
  EEPROM.commit();

  strcpy(wifi_ssid, ssid);
  strcpy(wifi_pass, pass);
  wifi_configurado = true;
}

void limparConfigWiFi() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.writeBool(ADDR_WIFI_CONFIGURED, false);
  EEPROM.commit();
  wifi_configurado = false;
  strcpy(wifi_ssid, "Wokwi-GUEST");
  strcpy(wifi_pass, "");
}

// ==================== EEPROM SENHAS ====================
void salvarEstado() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.writeInt(ADDR_SENHA_ATUAL, estado.senhaAtual);
  EEPROM.writeInt(ADDR_TOTAL_HIST, estado.totalHistorico);

  for (int i = 0; i < LIMITE_HISTORICO; i++) {
    EEPROM.put(ADDR_HISTORICO + i * sizeof(RegistroSenha),
               estado.historico[i]);
  }
  EEPROM.commit();
}

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

// ==================== TFT FUNÇÕES AUXILIARES ====================
void tftLimpar() {
  tft.fillScreen(COR_FUNDO);
}

void tftDesenharPainel(int x, int y, int w, int h, uint16_t corBorda = COR_BORDA) {
  tft.fillRoundRect(x, y, w, h, 12, COR_PAINEL);
  tft.drawRoundRect(x, y, w, h, 12, corBorda);
}

void tftTextoCentralizado(int x, int y, int w, const char* texto, uint16_t cor, uint8_t tamanho = 2) {
  int16_t x1, y1;
  uint16_t tw, th;
  tft.setTextSize(tamanho);
  tft.setTextColor(cor);
  tft.getTextBounds(texto, 0, 0, &x1, &y1, &tw, &th);
  int cx = x + (w - tw) / 2;
  tft.setCursor(cx, y);
  tft.print(texto);
}

void tftTexto(int x, int y, const char* texto, uint16_t cor, uint8_t tamanho = 2) {
  tft.setCursor(x, y);
  tft.setTextColor(cor);
  tft.setTextSize(tamanho);
  tft.print(texto);
}

void tftNumeroGrande(int x, int y, int w, int numero, uint16_t cor) {
  char buf[6];
  snprintf(buf, sizeof(buf), "%04d", numero);
  tft.setTextSize(5);
  tft.setTextColor(cor);
  int16_t x1, y1;
  uint16_t tw, th;
  tft.getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);
  int cx = x + (w - tw) / 2;
  tft.setCursor(cx, y);
  tft.print(buf);
}

void desenharCabecalho(const char* titulo, const char* subtitulo = "") {
  tft.fillRect(0, 0, 320, 50, COR_PAINEL);
  tft.drawFastHLine(0, 50, 320, COR_BORDA);
  tftTexto(15, 8, titulo, COR_DESTAQUE, 1);
  if (strlen(subtitulo) > 0) {
    tftTexto(15, 26, subtitulo, COR_TEXTO_FRACO, 1);
  }
  // Indicador WiFi no canto superior direito
  uint16_t corWifi = WiFi.status() == WL_CONNECTED ? COR_VERDE : (modoAP ? COR_LARANJA : COR_VERMELHO);
  tft.fillCircle(300, 25, 8, corWifi);
  tft.setTextSize(1);
  tft.setTextColor(COR_TEXTO_FRACO);
  tft.setCursor(250, 35);
  if (modoAP) tft.print("AP MODE");
  else tft.print(WiFi.status() == WL_CONNECTED ? "ONLINE" : "OFFLINE");
}

void desenharRodape(const char* infoExtra = "") {
  tft.fillRect(0, 210, 320, 30, COR_PAINEL);
  tft.drawFastHLine(0, 210, 320, COR_BORDA);
  if (strlen(infoExtra) > 0) {
    tftTextoCentralizado(0, 216, 320, infoExtra, COR_TEXTO_FRACO, 1);
  } else {
    tftTextoCentralizado(0, 216, 320, "Sistema de Senhas v1.1", COR_TEXTO_FRACO, 1);
  }
}

void desenharBarraProgresso(int x, int y, int w, int h, int progresso, int total, uint16_t corFundo, uint16_t corBarra) {
  tft.fillRoundRect(x, y, w, h, 4, corFundo);
  int largura = map(progresso, 0, total, 0, w - 4);
  if (largura > 2) tft.fillRoundRect(x + 2, y + 2, largura, h - 4, 2, corBarra);
}

// ==================== TELAS DO SISTEMA ====================
void mostrarEspera() {
  tftLimpar();
  desenharCabecalho("PAINEL DE ATENDIMENTO", "Sistema pronto");

  // Painel principal com senha atual
  tftDesenharPainel(20, 70, 280, 120, COR_VERDE);
  tftTextoCentralizado(20, 85, 280, "SENHA ATUAL", COR_TEXTO_FRACO, 2);

  if (estado.senhaAtual > 0) {
    tftNumeroGrande(20, 115, 280, estado.senhaAtual, COR_DESTAQUE);
  } else {
    tftTextoCentralizado(20, 125, 280, "---", COR_TEXTO_FRACO, 5);
  }

  // Instruções
  tftDesenharPainel(20, 195, 280, 30);
  tftTextoCentralizado(20, 200, 280, "Pressione [D] para Digite Nova Senha", COR_TEXTO_FRACO, 1);

  desenharRodape("Pressione [#] 3s = Config WiFi");
}

void mostrarDigitacao() {
  tftLimpar();
  desenharCabecalho("MODO MANUAL", "Digite Nova Senha");

  // Painel de digitação com título claro
  tftDesenharPainel(20, 70, 280, 100);
  tftTextoCentralizado(20, 80, 280, "DIGITE NOVA SENHA", COR_DESTAQUE, 2);

  // Mostra dígitos com placeholder
  char display[5] = "";
  for (int i = 0; i < MAX_DIGITOS; i++) {
    display[i] = (i < posDigita) ? bufferDigita[i] : '_';
  }
  display[MAX_DIGITOS] = '\0';

  tftNumeroGrande(20, 110, 280, (posDigita > 0) ? atoi(display) : 0, COR_DESTAQUE);

  // Placeholders visuais
  tft.setTextSize(3);
  tft.setTextColor(COR_TEXTO_FRACO);
  for (int i = 0; i < MAX_DIGITOS; i++) {
    int x = 70 + i * 45;
    int y = 155;
    if (i < posDigita) {
      tft.setCursor(x, y);
      tft.setTextColor(COR_DESTAQUE);
      tft.print(bufferDigita[i]);
    } else {
      tft.setCursor(x, y);
      tft.print("_");
    }
  }

  // Teclas de ajuda
  tftDesenharPainel(20, 185, 280, 30);
  tftTextoCentralizado(20, 190, 280, "[A]=Apaga  [C]=Confirma  [B]=Cancela", COR_TEXTO_FRACO, 1);

  desenharRodape("Senha: 1 a 4 digitos (1-9999)");
}

void mostrarConfirmacao() {
  tftLimpar();
  desenharCabecalho("CONFIRMACAO", "Confirmar Senha");

  tftDesenharPainel(20, 70, 280, 120, COR_LARANJA);
  tftTextoCentralizado(20, 85, 280, "CONFIRMAR SENHA", COR_FUNDO, 2);
  tftTextoCentralizado(20, 115, 280, "Digite a senha", COR_FUNDO, 2);
  tftTextoCentralizado(20, 145, 280, "abaixo:", COR_FUNDO, 2);

  tftNumeroGrande(20, 170, 280, atoi(bufferDigita), COR_DESTAQUE);

  // Botões de confirmação
  tftDesenharPainel(20, 195, 130, 30, COR_VERDE);
  tftTextoCentralizado(20, 200, 130, "[C] SIM", COR_FUNDO, 2);

  tftDesenharPainel(170, 195, 130, 30, COR_VERMELHO);
  tftTextoCentralizado(170, 200, 130, "[A/B] NAO", COR_FUNDO, 2);

  desenharRodape("Confirme para registrar a senha");
}

void mostrarSucesso(int senha) {
  tftLimpar();
  desenharCabecalho("SUCESSO", "Senha Registrada");

  tftDesenharPainel(20, 70, 280, 120, COR_VERDE);
  tftTextoCentralizado(20, 90, 280, "SENHA CHAMADA!", COR_FUNDO, 2);
  tftNumeroGrande(20, 115, 280, senha, COR_DESTAQUE);

  desenharRodape("Proxima chamada em 2 segundos...");
}

void mostrarErro(const char* msg) {
  tftLimpar();
  desenharCabecalho("ERRO", "");

  tftDesenharPainel(20, 80, 280, 100, COR_VERMELHO);
  tftTextoCentralizado(20, 105, 280, msg, COR_TEXTO, 2);
  tftTextoCentralizado(20, 140, 280, "Tente novamente", COR_TEXTO_FRACO, 1);

  desenharRodape("");
}

void mostrarIP() {
  tftLimpar();
  desenharCabecalho("CONEXAO WIFI", modoAP ? "Modo Access Point" : "Conectado");

  tftDesenharPainel(20, 80, 280, 100);
  tftTextoCentralizado(20, 100, 280, modoAP ? "IP do AP:" : "IP do ESP32:", COR_TEXTO_FRACO, 2);
  String ip = modoAP ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  tftTextoCentralizado(20, 130, 280, ip.c_str(), COR_DESTAQUE, 3);

  if (modoAP) {
    tftDesenharPainel(20, 190, 280, 30, COR_LARANJA);
    tftTextoCentralizado(20, 195, 280, "Conecte no WiFi: SENHA-AP", COR_FUNDO, 1);
  }

  desenharRodape("");
}

// ==================== TELA CONFIGURAÇÃO WIFI ====================
void mostrarConfigWiFi() {
  tftLimpar();
  desenharCabecalho("CONFIGURAR WIFI", "Portal Cativo Ativo");

  tftDesenharPainel(20, 70, 280, 120);
  tftTextoCentralizado(20, 85, 280, "Conecte no WiFi:", COR_TEXTO, 2);
  tftTextoCentralizado(20, 110, 280, "SENHA-CONFIG", COR_DESTAQUE, 3);
  tftTextoCentralizado(20, 145, 280, "Senha: 12345678", COR_TEXTO_FRACO, 1);
  tftTextoCentralizado(20, 165, 280, "Acesse: 192.168.4.1", COR_DESTAQUE, 2);

  // Barra de timeout
  int tempoRestante = (AP_TIMEOUT - (millis() - apInicio)) / 1000;
  desenharBarraProgresso(20, 195, 280, 15, tempoRestante, 300, COR_BORDA, COR_LARANJA);
  tftTextoCentralizado(20, 178, 280, ("Tempo: " + String(tempoRestante) + "s").c_str(), COR_TEXTO_FRACO, 1);

  desenharRodape("[B] Cancelar  [#] 3s = Sair");
}

// ==================== LÓGICA DE SENHAS ====================
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

  for (int i = 0; i < LIMITE_HISTORICO; i++) {
    estado.historico[i].numero = 0;
    estado.historico[i].horario[0] = '\0';
    estado.historico[i].valido = false;
  }

  salvarEstado();
  somConfirmacao();
  tftLimpar();
  desenharCabecalho("RESET", "");
  tftDesenharPainel(20, 100, 280, 60, COR_DESTAQUE);
  tftTextoCentralizado(20, 115, 280, "Contador resetado", COR_FUNDO, 2);
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
    mostrarErro("Senha invalida (1-9999)");
    delay(1500);
  }
  mostrarEspera();
}

// ==================== PORTAL CAPTIVO (CONFIG WIFI) ====================
const char HTML_CONFIG[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>Configurar WiFi - Painel de Senhas</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{min-height:100vh;background:#0d1b2a;color:#fff;font-family:sans-serif;display:flex;justify-content:center;align-items:center;padding:20px}
.container{width:100%;max-width:400px;background:#1b2a4a;border:1px solid #2d4466;border-radius:20px;padding:30px;box-shadow:0 15px 50px rgba(0,0,0,.4)}
h1{color:#ffd60a;font-size:24px;margin-bottom:10px;text-align:center}
.subtitle{color:#888;text-align:center;margin-bottom:30px;font-size:14px}
.form-group{margin-bottom:20px}
label{display:block;margin-bottom:8px;color:#ffd60a;font-weight:bold}
input[type="text"],input[type="password"]{width:100%;padding:15px;border:1px solid #2d4466;border-radius:10px;background:#14253d;color:#fff;font-size:16px;outline:none}
input:focus{border-color:#ffd60a}
button{width:100%;padding:15px;background:#ffd60a;color:#0d1b2a;border:0;border-radius:10px;font-size:16px;font-weight:bold;cursor:pointer}
button:hover{filter:brightness(1.1)}
.status{text-align:center;margin-top:20px;padding:15px;border-radius:10px;display:none}
.status.success{background:#00e676;color:#0d1b2a;display:block}
.status.error{background:#ff0000;color:#fff;display:block}
.info{background:#14253d;border:1px solid #2d4466;border-radius:10px;padding:15px;margin-bottom:20px;font-size:13px;color:#888}
</style>
</head>
<body>
<div class="container">
<h1>🔧 Configurar WiFi</h1>
<p class="subtitle">Painel de Senhas - Lanchonete</p>
<div class="info">
<strong>Modo Configuração Ativo</strong><br>
Conecte seu celular/PC no WiFi <b>SENHA-CONFIG</b> (senha: 12345678)<br>
Preencha abaixo os dados da sua rede WiFi.
</div>
<form id="wifiForm">
<div class="form-group">
<label>Nome da Rede (SSID)</label>
<input type="text" id="ssid" name="ssid" required placeholder="MinhaRedeWiFi">
</div>
<div class="form-group">
<label>Senha da Rede</label>
<input type="password" id="pass" name="pass" placeholder="Deixe vazio se aberta">
</div>
<button type="submit">Salvar e Conectar</button>
</form>
<div id="status" class="status"></div>
</div>
<script>
document.getElementById('wifiForm').addEventListener('submit',async(e)=>{
e.preventDefault();
const btn=e.target.querySelector('button');
btn.disabled=true;btn.textContent='Conectando...';
const formData=new FormData(e.target);
try{
const r=await fetch('/savewifi',{method:'POST',body:formData});
const d=await r.json();
const status=document.getElementById('status');
if(d.success){
status.className='status success';
status.textContent='✅ '+d.msg+' Reiniciando...';
setTimeout(()=>location.href='/',3000);
}else{
status.className='status error';
status.textContent='❌ '+d.msg;
btn.disabled=false;btn.textContent='Salvar e Conectar';
}
}catch(err){
document.getElementById('status').className='status error';
document.getElementById('status').textContent='❌ Erro de conexão';
btn.disabled=false;btn.textContent='Salvar e Conectar';
}
});
</script>
</body>
</html>
)rawliteral";

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
<div class="status"><span class="status-indicador"></span><span id="statusTexto">Conectado</span></div>
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
const statusTexto=document.getElementById('statusTexto');
let som=true, intervalo=null, ultimaSenha='---';

async function carregar(){
try{
const r=await fetch('/api/senha');
const d=await r.json();
atualizar(d);
statusTexto.textContent='Online';
statusTexto.style.color='#00e676';
}catch(e){
console.log('Erro ao carregar senha:',e);
statusTexto.textContent='Offline';
statusTexto.style.color='#ff0000';
}
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
void handleRoot() {
  if (modoAP) {
    server.send_P(200, "text/html; charset=utf-8", HTML_CONFIG);
  } else {
    server.send_P(200, "text/html; charset=utf-8", HTML_INDEX);
  }
}

void handleApiSenha() {
  StaticJsonDocument<256> doc;
  doc["senha"] = estado.senhaAtual > 0 ? String(estado.senhaAtual) : "---";
  doc["horario"] = obterHorario();
  doc["online"] = !modoAP && WiFi.status() == WL_CONNECTED;
  doc["historico_count"] = estado.totalHistorico;

  String resp;
  serializeJson(doc, resp);
  server.send(200, "application/json", resp);
}

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

void handleApiChamar() {
  chamarProxima();
  handleApiSenha();
}

void handleApiReset() {
  resetarContador();
  handleApiSenha();
}

void handleApiSet() {
  if (server.hasArg("senha")) {
    int num = server.arg("senha").toInt();
    definirSenhaManual(num);
  }
  handleApiSenha();
}

void handleSaveWiFi() {
  if (server.hasArg("ssid")) {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");

    StaticJsonDocument<256> doc;

    if (ssid.length() > 0) {
      salvarConfigWiFi(ssid.c_str(), pass.c_str());
      doc["success"] = true;
      doc["msg"] = "WiFi salvo! Conectando na rede '" + ssid + "'...";
      String resp;
      serializeJson(doc, resp);
      server.send(200, "application/json", resp);

      // Aguarda envio da resposta e reinicia
      delay(1000);
      ESP.restart();
    } else {
      doc["success"] = false;
      doc["msg"] = "SSID não pode ser vazio";
      String resp;
      serializeJson(doc, resp);
      server.send(400, "application/json", resp);
    }
  } else {
    server.send(400, "application/json", "{\"success\":false,\"msg\":\"Dados inválidos\"}");
  }
}

void handleNotFound() {
  if (modoAP) {
    // Portal cativo - redireciona tudo para a página de config
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Nao encontrado");
  }
}

// ==================== WIFI ====================
void iniciarModoAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("SENHA-CONFIG", "12345678");

  // DNS Server para portal cativo
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  modoAP = true;
  apInicio = millis();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/savewifi", HTTP_POST, handleSaveWiFi);
  server.onNotFound(handleNotFound);
  server.begin();

  atualizarLeds(false);
  mostrarConfigWiFi();

  Serial.println("=== MODO AP ATIVADO ===");
  Serial.println("SSID: SENHA-CONFIG");
  Serial.println("Senha: 12345678");
  Serial.println("IP: " + WiFi.softAPIP().toString());
  Serial.println("Acesse http://192.168.4.1 para configurar");
}

void conectarWiFi() {
  if (!wifi_configurado || strlen(wifi_ssid) == 0) {
    iniciarModoAP();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_pass);

  tftLimpar();
  desenharCabecalho("CONECTANDO WIFI", wifi_ssid);

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 30) {
    tftDesenharPainel(20, 100, 280, 60);
    tftTextoCentralizado(20, 115, 280, ("Tentativa " + String(tentativas + 1) + " / 30").c_str(), COR_TEXTO_FRACO, 2);
    delay(500);
    tentativas++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    modoAP = false;
    dnsServer.stop();

    tftLimpar();
    desenharCabecalho("WIFI CONECTADO", "");
    tftDesenharPainel(20, 100, 280, 60, COR_VERDE);
    tftTextoCentralizado(20, 115, 280, WiFi.localIP().toString().c_str(), COR_TEXTO, 3);
    atualizarLeds(true);
    Serial.print("WiFi conectado. IP: ");
    Serial.println(WiFi.localIP());
    delay(2000);

    // Configura rotas normais
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/senha", HTTP_GET, handleApiSenha);
    server.on("/api/historico", HTTP_GET, handleApiHistorico);
    server.on("/api/chamar", HTTP_POST, handleApiChamar);
    server.on("/api/reset", HTTP_POST, handleApiReset);
    server.on("/api/set", HTTP_POST, handleApiSet);
    server.onNotFound(handleNotFound);
  } else {
    // Falhou - volta para AP
    iniciarModoAP();
  }
}

// ==================== BUFFER DO TECLADO ====================
void limparBuffer() {
  memset(bufferDigita, 0, sizeof(bufferDigita));
  posDigita = 0;
}

// ==================== PROCESSAMENTO DO TECLADO ====================
void processarTecla(char tecla) {
  ultimaTecla = millis();
  somTecla();

  Serial.printf("\nTecla recebida: %c\n", tecla);

  // MODO DE CONFIRMAÇÃO: C confirma; A ou B volta à digitação.
  if (modoConfirmacao) {
    if (tecla == 'C') {
      int senha = atoi(bufferDigita);

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
    Serial.println("MODO MANUAL ATIVADO - Digite Nova Senha");
  } else if (tecla == '#') {
    // Verifica se é pressão longa (3s) para config WiFi
    teclaSegurada = '#';
    teclaPressionadaInicio = millis();
  } else {
    Serial.println("Pressione [D] para Digite Nova Senha");
  }

  Serial.printf("Estado: Manual=%d Confirmacao=%d Buffer=%s\n",
                modoManual, modoConfirmacao, bufferDigita);
}

void verificarTeclaSegurada() {
  if (teclaSegurada == '#' && millis() - teclaPressionadaInicio > 3000) {
    teclaSegurada = 0;
    somConfirmacao();
    if (!modoAP) {
      Serial.println("Entrando no modo configuracao WiFi...");
      iniciarModoAP();
    }
  }
  if (teclaSegurada == '#' && millis() - teclaPressionadaInicio < 3000) {
    // Verifica se soltou
    char tecla = keypad.getKey();
    if (tecla == NO_KEY) {
      // Tecla solta antes de 3s
      teclaSegurada = 0;
    }
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n=== Sistema de Senhas ESP32 + ILI9341 ===");
  Serial.println("[D] = Digite Nova Senha | [A] = Apaga | [C] = Confirma | [B] = Cancela");
  Serial.println("[#] segurar 3s = Configurar WiFi");

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, HIGH);
  digitalWrite(TFT_BL, HIGH);  // Backlight ON
  noTone(BUZZER_PIN);

  keypad.setDebounceTime(50);
  keypad.setHoldTime(500);

  // Inicializa TFT
  tft.begin();
  tft.setRotation(1);  // Landscape
  tft.fillScreen(COR_FUNDO);

  // Splash screen
  desenharCabecalho("SISTEMA DE SENHAS", "Iniciando v1.1");
  tftDesenharPainel(20, 100, 280, 60, COR_DESTAQUE);
  tftTextoCentralizado(20, 115, 280, "ILI9341 320x240", COR_FUNDO, 2);
  delay(1500);

  carregarConfigWiFi();
  carregarEstado();
  conectarWiFi();

  somConfirmacao();
  mostrarEspera();

  Serial.printf("Sistema pronto! %s\n",
                modoAP ? "Modo AP: http://192.168.4.1" : ("IP: " + WiFi.localIP().toString()).c_str());
}

// ==================== LOOP PRINCIPAL ====================
void loop() {
  // Servidor web + DNS (portal cativo)
  server.handleClient();
  if (modoAP) {
    dnsServer.processNextRequest();

    // Timeout do modo AP (5 min)
    if (millis() - apInicio > AP_TIMEOUT) {
      Serial.println("Timeout modo AP - reiniciando");
      ESP.restart();
    }
  }

  // Teclado
  char tecla = keypad.getKey();
  if (tecla != NO_KEY) {
    processarTecla(tecla);
  }

  // Verifica tecla # segurada
  verificarTeclaSegurada();

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
  if (!modoAP && millis() - ultimaVerificacao > 30000) {
    ultimaVerificacao = millis();

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi caiu. Tentando reconectar...");
      atualizarLeds(false);
      conectarWiFi();
    }
  }

  delay(10);
}