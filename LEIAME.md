# 🎯 Sistema de Senhas - Lanchonete (ESP32 + ILI9341)

Projeto completo para painel de senhas com **TFT ILI9341 320x240**, **Keypad 4x4**, **Buzzer**, **LEDs** e **Painel Web** acessível pelo navegador.

---

## 📋 Funcionalidades

| Recurso | Descrição |
|---------|-----------|
| **TFT Colorido** | Interface visual rica no ILI9341 320x240 SPI |
| **Keypad 4x4** | Entrada de senhas e navegação por menus |
| **Painel Web** | Acesso via navegador (celular, tablet, PC) |
| **WiFi Configurável** | Portal cativo - configure pelo celular sem recompilar |
| **Histórico** | Últimas 5 senhas salvas na EEPROM |
| **Persistência** | Contador e histórico sobrevivem a reinícios |
| **Buzzer** | Sons distintos: chamada, confirmação, erro, tecla |
| **LEDs Status** | 🟢 Verde = Online | 🔴 Vermelho = Offline | 🟠 Laranja = Modo Config |

---

## 🔌 Ligações (Hardware Físico)

### TFT ILI9341 (VSPI)
| Pino TFT | ESP32 GPIO |
|----------|------------|
| SCK      | 18         |
| MOSI     | 23         |
| MISO     | 19         |
| CS       | 5          |
| DC       | 2          |
| RST      | 15         |
| BL       | 21 (PWM)   |
| VCC      | 3.3V       |
| GND      | GND        |

### Keypad 4x4 (Membrana)
| Linha/Coluna | GPIO |
|--------------|------|
| R1           | 13   |
| R2           | 12   |
| R3           | 14   |
| R4           | 27   |
| C1           | 26   |
| C2           | 25   |
| C3           | 33   |
| C4           | 32   |

### Saídas
| Componente | GPIO |
|------------|------|
| Buzzer     | 4    |
| LED Verde  | 16   |
| LED Vermelho | 17 |

---

## ⌨️ Controles do Teclado

| Tecla | Função |
|-------|--------|
| **D** | **Digite Nova Senha** - entra no modo manual |
| **0-9** | Digita número da senha (1 a 4 dígitos) |
| **A** | Apaga último dígito / **Não** na confirmação |
| **C** | **Confirma** a senha digitada |
| **B** | **Cancela** e volta à tela de espera |
| **#** (segurar 3s) | **Configurar WiFi** - abre portal cativo |

---

## 🌐 Painel Web

### Acesso Local (mesma rede WiFi)
```
http://<IP_DO_ESP32>/
```
Exemplo: `http://192.168.1.100/`

### Funcionalidades Web
- 📺 **Senha atual** em destaque (tamanho responsivo)
- 📜 **Histórico** das últimas 5 senhas com horário
- 🔔 **Botão "Chamar (Teste)"** - simula chamada
- 🔄 **Auto-atualização** a cada 2 segundos
- 🔊 **Som de notificação** no navegador
- 🗑️ **Reset** do contador
- ⚙️ **Configurações** de som e auto-update

---

## ⚙️ Configuração WiFi (Primeira vez / Trocar rede)

### Método 1: Portal Cativo (Recomendado)
1. Ligue o ESP32
2. Segure a tecla **`#` por 3 segundos** até ouvir bip de confirmação
3. No TFT aparecerá: **"Conecte no WiFi: SENHA-CONFIG"**
3. No celular/PC, conecte na rede WiFi:
   - **SSID:** `SENHA-CONFIG`
   - **Senha:** `12345678`
4. Abra o navegador em `http://192.168.4.1`
5. Preencha sua rede WiFi e senha → **Salvar e Conectar**
6. ESP32 reinicia e conecta na sua rede

### Método 2: Wokwi (Simulação)
No `sketch.ino`, linhas 41-42:
```cpp
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";
```
O Wokwi conecta automaticamente.

---

## 🚀 Como Executar

### 1. Wokwi Online (Sem hardware)
1. Abra: https://wokwi.com/projects/480614326867239937
2. Clique **▶ Start Simulation**
3. Use o keypad virtual na lateral direita

### 2. VS Code + Extensão Wokwi
```bash
code "E:\Projetos de Programação\AtividadesIFMS\projeto-senha-lanchonete-wokwi"
# F1 → "Wokwi: Start Simulator"
```

### 3. Hardware Real (Arduino IDE)
1. Abra `sketch.ino` no Arduino IDE
2. Instale as bibliotecas (Gerenciador de Bibliotecas):
   - `Keypad` (Mark Stanley)
   - `Adafruit ILI9341` (Adafruit)
   - `Adafruit GFX Library` (Adafruit)
   - `ArduinoJson` (Benoit Blanchon)
3. Placa: **ESP32 Dev Module**
4. Compile e Upload

### 4. Hardware Real (PlatformIO)
```bash
cd "E:\Projetos de Programação\AtividadesIFMS\projeto-senha-lanchonete-wokwi"
pio run -t upload
```

---

## 📁 Estrutura do Projeto

```
projeto-senha-lanchonete-wokwi/
├── sketch.ino          # Código principal (ESP32)
├── diagram.json        # Esquema Wokwi (ligações)
├── libraries.txt       # Bibliotecas para Wokwi
├── wokwi-project.txt   # Link do projeto Wokwi
├── servidor_web.py     # Servidor local para painel standalone
├── iniciar_web.bat     # Inicia servidor web (Windows)
└── LEIAME.md           # Este arquivo
```

---

## 🎨 Telas do TFT

| Tela | Descrição Visual |
|------|------------------|
| **Espera** | Senha atual grande + "Pressione [D] para Digite Nova Senha" |
| **Digite Nova Senha** | Título amarelo + 4 dígitos com `_` placeholder |
| **Confirmar Senha** | Fundo laranja + senha gigante + botões [C] SIM / [A/B] NÃO |
| **Sucesso** | Fundo verde + "SENHA CHAMADA!" + número enorme |
| **Erro** | Fundo vermelho + mensagem de erro |
| **Config WiFi** | Instruções para portal cativo + barra de timeout (5 min) |
| **WiFi Conectado** | IP grande + status ONLINE no cabeçalho |

---

## 🔧 Personalização

### Alterar Senha Inicial
No `setup()`, após `carregarEstado()`:
```cpp
if (estado.senhaAtual == 0) {
    estado.senhaAtual = 100;  // Começa no 100
    salvarEstado();
}
```

### Alterar Limite de Histórico
```cpp
const int LIMITE_HISTORICO = 10;  // Era 5
```

### Cores do TFT
Modifique os `#define` no topo:
```cpp
#define COR_FUNDO       0x0861   // Azul escuro
#define COR_DESTAQUE    0xFFB0   // Amarelo
#define COR_VERDE       0x07E0   // Verde brilhante
```

---

## 🐛 Solução de Problemas

| Problema | Solução |
|----------|---------|
| TFT branco | Verifique ligações SPI (SCK, MOSI, CS, DC, RST) |
| Touch não funciona | ILI9341 básico não tem touch; use XPT2046 separado |
| WiFi não conecta | Segure `#` 3s → configure pelo portal cativo |
| Painel web não abre | Verifique IP no Serial Monitor (115200 baud) |
| Keypad não responde | Confira pinos R1-R4 e C1-C4; testte com multímetro |
| Buzzer mudo | Verifique GPIO 4; teste com `tone(4, 1000, 500)` no setup |

---

## 📄 Licença

Projeto educacional para IFMS - Livre para uso e modificação.

---

## 🤝 Créditos

- **Wokwi** - Simulador online
- **Adafruit** - Bibliotecas GFX e ILI9341
- **ArduinoJson** - Parsing JSON
- **Keypad** - Biblioteca de teclado matricial

---

> **Dica:** No hardware real, use capacitores de 0.1µF nos pinos de alimentação do TTF e resistores de pull-up (4.7kΩ) nas linhas I2C se houver ruído.