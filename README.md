# Painel de Senhas da Lanchonete — MQTT

Projeto com ESP32, teclado 4x4, display ILI9341, LEDs, buzzer e painel web. A comunicação entre o ESP32 e o navegador é feita por MQTT.

## Arquitetura

- ESP32 (Wokwi ou placa física): MQTT TCP em `broker.emqx.io:1883`.
- Navegador: MQTT sobre WebSocket seguro em `wss://broker.emqx.io:8084/mqtt`.
- Os dois clientes usam o mesmo broker e os mesmos tópicos.

## Tópicos MQTT

O prefixo configurado é `lanchonete/senhas/brunosevero-7f3a9c`.

| Tópico | Direção | Uso |
|---|---|---|
| `.../estado` | ESP32 → navegador | Senha atual e histórico em JSON, mensagem retained |
| `.../comando` | navegador → ESP32 | `proxima`, `chamar`, `limpar` ou `sincronizar` |
| `.../status` | ESP32 → navegador | Estado online/offline do dispositivo |

**Atenção:** o prefixo deve ser idêntico no `sketch.ino` e no `script.js`. Se várias pessoas forem testar, altere `brunosevero-7f3a9c` nos dois arquivos para um identificador exclusivo.

## 1. Executar no Wokwi

1. Abra o projeto ESP32 no Wokwi.
2. Substitua o conteúdo do `sketch.ino` pelo arquivo deste pacote.
3. Em `libraries.txt`, mantenha `PubSubClient` e `ArduinoJson` junto às bibliotecas do display e teclado.
4. Inicie a simulação.
5. Abra o Serial Monitor em 115200 baud.
6. Aguarde as mensagens `[WiFi]` e `[MQTT] ... conectado!`.

No Wokwi, o sketch usa `Wokwi-GUEST` sem senha e canal 6. O simulador oferece acesso de saída à internet para conectar ao broker público.

## 2. Executar o painel web

1. Deixe a simulação ESP32 em execução.
2. Na pasta deste projeto, execute `iniciar_web.bat` ou `python servidor_web.py`.
3. Abra `http://localhost:8080` no navegador.
4. Aguarde o status `Broker MQTT conectado`.
5. Quando o ESP32 estiver conectado, o painel receberá o estado publicado pelo dispositivo.

O navegador precisa ter acesso à internet para carregar MQTT.js e abrir a conexão WSS.

## 3. Teste de comunicação

- Digite `25` no teclado e pressione `#`: o ESP32 chama `0025`, publica o estado e o navegador atualiza.
- Pressione `A`: o ESP32 chama a próxima senha e sincroniza o painel.
- No navegador, pressione **Chamar próxima senha**: o comando chega ao ESP32, que chama a senha, aciona os LEDs/buzzer e publica o novo estado.
- No navegador, pressione **Limpar histórico**: o ESP32 limpa a senha e o histórico.

## 4. Usar uma placa ESP32 física

No `sketch.ino`, altere `WIFI_SSID` e `WIFI_PASSWORD` para sua rede Wi-Fi. Mantenha o broker e os tópicos iguais aos do `script.js`. O computador/celular e o ESP32 não precisam estar na mesma rede para usar um broker público na internet, mas ambos precisam de acesso à internet.

## Segurança e limitações

- `broker.emqx.io` é um broker público de testes. As mensagens publicadas em tópicos públicos podem ser vistas por terceiros. Não envie dados pessoais, senhas de usuários ou informações privadas.
- O identificador do tópico é apenas uma forma de reduzir colisões acidentais, não é autenticação nem controle de acesso. Para uso real, configure um broker privado com autenticação e TLS.
- A biblioteca PubSubClient usa MQTT sem TLS neste exemplo no ESP32 (porta 1883).
- O horário exibido pelo ESP32 é o tempo desde que a placa iniciou (`millis()`), não a hora civil. O painel web exibe esse valor no histórico recebido.
- A comunicação depende da disponibilidade do broker e da internet. O estado mais recente é publicado como retained para facilitar a sincronização de novos navegadores.

## Arquivos

- `sketch.ino`: lógica do ESP32 e cliente MQTT.
- `diagram.json`: circuito Wokwi.
- `index.html`, `style.css`, `script.js`: painel web.
- `servidor_web.py` e `iniciar_web.bat`: servidor local para servir os arquivos web.
- `libraries.txt`: bibliotecas Arduino necessárias.
