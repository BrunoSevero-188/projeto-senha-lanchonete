#include <WiFi.h>
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
// CONFIGURAÇÃO MQTT
// ============================================================
const char* MQTT_HOST = "broker.emqx.io";
const uint16_t MQTT_PORT = 1883;

// Tópicos únicos para este dispositivo (mude o sufixo se compartilhar)
const char* TOPICO_ESTADO   = "lanchonete/senhas/brunosevero-7f3a9c/estado";
const char* TOPICO_COMANDO  = "lanchonete/senhas/brunosevero-7f3a9c/comando";
const char* TOPICO_STATUS   = "lanchonete/senhas/brunosevero-7f3a9c/status";

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
unsigned long ultimaTentativaWiFi = 0;
unsigned long ultimaTentativaMQTT = 0;

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
// FUNÇÕES AUXILIARES
// ============================================================
String horarioAtual() {
  // Tempo relativo desde boot. Para horário real, adicione NTP/RTC.
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

  // Título
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

  // IP ou modo AP
  tela.setCursor(10, 30);
  tela.setTextColor(COR_CINZA);
  tela.setTextSize(1);
  if (wifiConectado) {
    tela.print("IP: ");
    tela.print(WiFi.localIP().toString());
  } else {
    tela.print("Aguardando conexao...");
  }
}

void desenharSenhaAtual() {
  // Painel da senha atual
  tela.fillRoundRect(10, 60, 300, 90, 8, COR_PAINEL);
  tela.drawRoundRect(10, 60, 300, 90, 8, COR_CINZA);

  tela.setTextColor(COR_CINZA);
  tela.setTextSize(2);
  tela.setCursor(20, 70);
  tela.print("SENHA ATUAL");

  // Número grande centralizado
  tela.setTextColor(COR_DESTAQUE);
  tela.setTextSize(5);
  String exibir = senhaAtual.length() ? senhaAtual : "----";
  int16_t x1, y1;
  uint16_t w, h;
  tela.getTextBounds(exibir.c_str(), 0, 0, &x1, &y1, &w, &h);
  int x = 160 - w / 2;
  tela.setCursor(x, 95);
  tela.print(exibir);

  // Horário
  if (horarioSenhaAtual.length()) {
    tela.setTextColor(COR_CINZA);
    tela.setTextSize(1);
    tela.setCursor(20, 138);
    tela.print("Chamada: ");
    tela.print(horarioSenhaAtual);
  }
}

void desenharEntrada() {
  // Painel de digitação manual
  tela.fillRoundRect(10, 160, 300, 55, 8, COR_PAINEL);
  tela.drawRoundRect(10, 160, 300, 55, 8, COR_CINZA);

  tela.setTextColor(COR_BRANCO);
  tela.setTextSize(2);
  tela.setCursor(20, 170);
  tela.print("DIGITE SENHA: ");

  // Mostra dígitos com placeholder _
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
  // Painel do histórico (últimas 3 na tela)
  tela.fillRoundRect(10, 225, 300, 95, 8, COR_PAINEL);
  tela.drawRoundRect(10, 225, 300, 95, 8, COR_CINZA);

  tela.setTextColor(COR_CINZA);
  tela.setTextSize(1);
  tela.setCursor(20, 232);
  tela.print("ULTIMAS 5 CHAMADAS:");

  int y = 248;
  for (int i = 0; i < 3; i++) {  // Mostra 3 na tela (cabe 5 no array)
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

  // Indica se há mais no histórico
  if (historico[3].length() || historico[4].length()) {
    tela.setTextColor(COR_CINZA);
    tela.setTextSize(1);
    tela.setCursor(20, y + 2);
    tela.print("+ mais no painel web");
  }
}

void desenharBotoes() {
  // Legenda dos botões na parte inferior
  tela.fillRect(0, 280, 320, 40, COR_PAINEL);
  tela.drawFastHLine(0, 280, 320, COR_CINZA);

  tela.setTextColor(COR_CINZA);
  tela.setTextSize(1);
  tela.setCursor(10, 286);
  tela.print("[A] Proxima   [B] Limpar Tudo");
  tela.setCursor(10, 300);
  tela.print("[*] Apagar    [#] Confirmar");

  // Status da entrada
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

  // Mensagem temporária centralizada
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
// MQTT - PUBLICAR ESTADO
// ============================================================
void publicarEstado() {
  if (!mqttConectado) return;

  StaticJsonDocument<768> doc;
  doc["tipo"] = "estado";
  doc["senha"] = senhaAtual;
  doc["horario"] = horarioSenhaAtual;
  doc["wifi"] = wifiConectado;
  doc["mqtt"] = mqttConectado;

  JsonArray lista = doc.createNestedArray("historico");
  for (int i = 0; i < 5; i++) {
    if (historico[i].length()) {
      JsonObject item = lista.createNestedObject();
      item["senha"] = historico[i];
      item["horario"] = horariosHistorico[i];
    }
  }

  char payload[768];
  size_t tamanho = serializeJson(doc, payload, sizeof(payload));
  mqtt.publish(TOPICO_ESTADO, reinterpret_cast<const uint8_t*>(payload), tamanho, true);

  Serial.print("[MQTT] Estado publicado: ");
  Serial.println(payload);
}

// ============================================================
// LÓGICA DE SENHAS
// ============================================================
void chamarSenha(String numero, bool publicar = true) {
  numero.trim();
  if (numero.length() == 0) {
    bipErro();
    desenharTelaCompleta("Digite uma senha");
    delay(800);
    desenharTelaCompleta();
    return;
  }

  int valor = numero.toInt();
  if (valor < 1 || valor > 9999) {
    ledErro(true);
    bipErro();
    desenharTelaSimples("ERRO", "Senha invalida\n(1-9999)", COR_VERMELHO);
    delay(1200);
    ledErro(false);
    desenharTelaCompleta();
    return;
  }

  char formatada[5];
  snprintf(formatada, sizeof(formatada), "%04d", valor);
  String novaSenha = String(formatada);

  if (novaSenha == senhaAtual) {
    entrada = "";
    bipTecla();
    desenharTelaCompleta("Senha ja chamada");
    delay(800);
    desenharTelaCompleta();
    return;
  }

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
  ledSucesso(false);  // Pisca e apaga

  if (publicar) publicarEstado();
  desenharTelaCompleta();
}

void proximaSenha(bool publicar = true) {
  int proxima = senhaAtual.length() ? senhaAtual.toInt() + 1 : 1;
  if (proxima > 9999) proxima = 1;
  chamarSenha(String(proxima), publicar);
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
// MQTT - PROCESSAR COMANDOS RECEBIDOS (do painel web)
// ============================================================
void processarComando(char* payload, unsigned int length) {
  StaticJsonDocument<256> doc;
  DeserializationError erro = deserializeJson(doc, payload, length);
  if (erro) {
    Serial.println("[MQTT] Comando JSON invalido.");
    return;
  }

  const char* acao = doc["acao"] | "";
  Serial.print("[MQTT] Comando recebido: ");
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

  Serial.println("\n=== PAINEL DE SENHAS - ESP32 + ILI9341 + MQTT ===");
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
  tela.setRotation(1);  // Landscape
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
  tela.setCursor(60, 200);
  tela.print("Conectando WiFi/MQTT...");
  delay(1500);

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
  // Reconexão WiFi
  conectarWiFi();

  // Reconexão MQTT
  if (wifiConectado) conectarMQTT();

  // Loop MQTT (processa mensagens recebidas)
  if (mqttConectado) {
    mqtt.loop();
    // Verifica se desconectou
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

    // Dígitos 0-9: adiciona à entrada (máx 4)
    if (tecla >= '0' && tecla <= '9') {
      if (entrada.length() < 4) {
        entrada += tecla;
        bipTecla();
        desenharTelaCompleta();
      } else {
        bipErro();  // Limite atingido
      }
    }
    // * : Apaga último dígito
    else if (tecla == '*') {
      apagarUltimoDigito();
    }
    // # : Confirma/Chama a senha digitada
    else if (tecla == '#') {
      chamarSenha(entrada);
    }
    // A : Próxima senha automática (incrementa)
    else if (tecla == 'A') {
      proximaSenha();
    }
    // B : Limpa tudo (painel + histórico)
    else if (tecla == 'B') {
      limparPainel();
    }
    // D : (reservado) - pode usar para modo config WiFi
    else if (tecla == 'D') {
      bipTecla();
      desenharTelaCompleta("Use # para confirmar");
      delay(800);
      desenharTelaCompleta();
    }
    // C : (reservado)
    else if (tecla == 'C') {
      bipTecla();
    }
    else {
      bipErro();
    }
  }
}