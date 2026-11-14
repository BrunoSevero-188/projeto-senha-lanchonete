# Sistema de Senhas - Lanchonete (Wokwi Simulation)

Projeto ESP32 para sistema de senhas de atendimento em lanchonete, pronto para simulação no [Wokwi](https://wokwi.com).

## 🎯 Funcionalidades

- **LCD 16x2 I2C** - Exibe mensagens e senhas
- **Keypad 4x4** - Entrada de senhas e comandos
- **Buzzer** - Sons de confirmação, erro e chamada
- **LEDs** - Verde (WiFi OK) / Vermelho (WiFi falhou)
- **WebServer** - Painel web acessível via navegador
- **WebSocket** - Atualização em tempo real
- **EEPROM** - Persistência de senhas entre reinícios

## 🔌 Conexões (conforme diagram.json)

| Componente | Pino ESP32 |
|------------|------------|
| LCD SDA    | GPIO 22    |
| LCD SCL    | GPIO 23    |
| Keypad R1  | GPIO 13    |
| Keypad R2  | GPIO 12    |
| Keypad R3  | GPIO 14    |
| Keypad R4  | GPIO 27    |
| Keypad C1  | GPIO 26    |
| Keypad C2  | GPIO 25    |
| Keypad C3  | GPIO 33    |
| Keypad C4  | GPIO 32    |
| Buzzer     | GPIO 18    |
| LED Verde  | GPIO 19    |
| LED Vermelho | GPIO 21  |

## ⌨️ Layout do Keypad

```
1  2  3  A     <- A = APAGAR (backspace)
4  5  6  B     <- B = CANCELAR / VOLTAR
7  8  9  C     <- C = CONFIRMAR (enter/ok)
*  0  #  D     <- * = CHAMAR PRÓXIMA, # = MOSTRAR IP, D = MODO MANUAL
```

## 🚀 Como usar no Wokwi

### Opção 1: Wokwi Online (Recomendado)
1. Acesse [wokwi.com](https://wokwi.com)
2. Clique em "New Project" → "ESP32"
3. Copie o conteúdo de `diagram.json` para o diagrama
4. Copie `sketch.ino` para o editor de código
5. Copie `libraries.txt` para a aba "libraries"
6. Clique no botão "Play" para simular

### Opção 2: Wokwi CLI
```bash
npm install -g @wokwi/cli
cd projeto-senha-lanchonete-wokwi
wokwi
```

### Opção 3: VS Code com Extensão Wokwi
1. Instale a extensão "Wokwi Simulator" no VS Code
2. Abra a pasta `projeto-senha-lanchonete-wokwi`
3. Pressione `F1` → "Wokwi: Start Simulator"

## 🌐 Acesso ao Painel Web

Após a simulação iniciar e o ESP32 conectar ao WiFi:
1. O IP aparecerá no LCD e no Serial Monitor
2. Acesse `http://<IP_DO_ESP32>` no navegador
3. WebSocket disponível em `ws://<IP_DO_ESP32>:81`

## 📋 Funcionamento

### Modo Espera (inicial)
- Pressione **D** para entrar no modo digitação manual
- Pressione **\*** para chamar próxima senha automaticamente
- Pressione **#** para ver o IP do ESP32

### Modo Digitação Manual
- Digite números **0-9** (máx 4 dígitos)
- **A** = Apagar último dígito
- **D** = Limpar tudo
- **C** = Confirmar (vai para modo confirmação)
- **B** = Cancelar e voltar ao modo espera

### Modo Confirmação
- **C** = Sim, confirmar senha digitada
- **A** ou **B** = Não, voltar para digitação

## 🔧 Configuração WiFi

As credenciais estão no topo do `sketch.ino` (linhas 31-32):

```cpp
const char* WIFI_SSID = "ESP-PROJETO";
const char* WIFI_PASSWORD = "";
```

- **Para hardware real**: Edite `WIFI_SSID` e `WIFI_PASSWORD` com suas credenciais
- **Para Wokwi online**: Mude temporariamente para `"Wokwi-GUEST"` (rede simulada aberta)

## 📦 Bibliotecas Necessárias

- `LiquidCrystal_I2C` - Display LCD
- `Keypad` - Teclado matricial
- `ArduinoJson` - JSON para API/WebSocket
- `WebSockets` - Servidor WebSocket

Todas disponíveis no Library Manager do Arduino IDE / PlatformIO.

## 📝 Licença

Projeto educacional para IFMS - Instituto Federal de Mato Grosso do Sul.