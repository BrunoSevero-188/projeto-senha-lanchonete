# Referência Rápida - Keypad 4x4

## Layout Físico (wokwi-membrane-keypad)

```
┌───┬───┬───┬───┐
│ 1 │ 2 │ 3 │ A │  ← Linha 0 (R1=GPIO13)
├───┼───┼───┼───┤
│ 4 │ 5 │ 6 │ B │  ← Linha 1 (R2=GPIO12)
├───┼───┼───┼───┤
│ 7 │ 8 │ 9 │ C │  ← Linha 2 (R3=GPIO14)
├───┼───┼───┼───┤
│ * │ 0 │ # │ D │  ← Linha 3 (R4=GPIO27)
└───┴───┴───┴───┘
  ↑   ↑   ↑   ↑
  C1  C2  C3  C4
 (26) (25) (33) (32)
```

## Funções das Teclas

| Tecla | Função | Contexto |
|-------|--------|----------|
| **0-9** | Digitar números | Modo digitação manual |
| **A** | Apagar (backspace) | Modo digitação / Cancelar confirmação |
| **B** | Cancelar / Voltar | Modo digitação / Cancelar confirmação |
| **C** | Confirmar (OK) | Modo digitação → confirmação / Confirma senha |
| **D** | Modo manual / Limpar | Modo espera → digitação / Limpar buffer |
| **\*** | Chamar próxima senha | **Global** (qualquer modo) |
| **#** | Mostrar IP do ESP32 | **Global** (qualquer modo) |

## Fluxo de Estados

```
┌─────────────┐
│   ESPERA    │ ← Inicial
│  (D:Manual) │
└──────┬──────┘
       │ D
       ▼
┌─────────────────┐
│  DIGITAÇÃO      │ ← Digita 0-9, A=apaga, D=limpa
│  MANUAL         │
└────────┬────────┘
         │ C (se buffer não vazio)
         ▼
┌─────────────────┐
│  CONFIRMAÇÃO    │ ← C=Sim, A/B=Não
└────────┬────────┘
         │ C (confirmado)
         ▼
    ┌─────────┐
    │ SUCESSO │ → Volta para ESPERA
    └─────────┘
```

## Atalhos Globais (funcionam sempre)

- **\*** = Chama próxima senha imediatamente (incrementa contador)
- **#** = Mostra IP no LCD por 3 segundos

## Timeouts

- **Modo Manual**: 30 segundos sem tecla → volta para ESPERA
- **Confirmação**: 10 segundos sem tecla → cancela confirmação

## LEDs

| LED | Pino | Significado |
|-----|------|-------------|
| Verde | GPIO19 | WiFi Conectado |
| Vermelho | GPIO21 | WiFi Desconectado |

## Buzzer (GPIO18)

| Som | Padrão |
|-----|--------|
| Tecla | 1 bip curto (1200Hz, 30ms) |
| Confirmação | 3 tons ascendentes (600→800→1000Hz) |
| Chamada | 3 bips (880Hz, 100ms cada) |
| Erro | 1 bip grave longo (200Hz, 300ms) |

## API Web

| Endpoint | Método | Descrição |
|----------|--------|-----------|
| `/` | GET | Painel HTML |
| `/api/senha` | GET | Senha atual + horário |
| `/api/historico` | GET | Últimas 5 senhas |
| `/api/chamar` | POST | Chama próxima senha |
| `/api/reset` | POST | Reseta contador |
| `/api/set` | POST | Define senha manual (`?senha=123`) |

## WebSocket

- Porta: **81**
- Endpoint: `ws://<IP>:81`
- Envia atualizações em tempo real para todos os clientes conectados