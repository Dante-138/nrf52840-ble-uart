# nRF52840 DK - BLE UART (Nordic UART Service)

Firmware para a **nRF52840 DK** que cria uma ponte bidirecional entre **UART (PuTTY)** e **Bluetooth Low Energy (BLE)**, utilizando o **Nordic UART Service (NUS)**.

## O que faz

```
PuTTY (PC) ──UART──> nRF52840 ──BLE──> Celular (nRF Connect app)
Celular    ──BLE───> nRF52840 ──UART──> PuTTY (PC)
```

- Digite no **PuTTY** e a mensagem aparece no **celular**
- Digite no **celular** e a mensagem aparece no **PuTTY**

### LEDs de Status

| LED | Significado |
|---|---|
| LED1 piscando | Firmware rodando normalmente |
| LED2 aceso | Dispositivo BLE conectado |
| LED2 apagado | Nenhuma conexao BLE ativa |

## Demonstracao

Ao gravar o firmware e abrir o PuTTY, voce vera:

```
==================================
  nRF52840 BLE UART (NUS)
==================================
Aguardando conexao BLE...
Nome: nRF52840_BLE_UART
```

Apos conectar pelo celular, as mensagens trafegam nos dois sentidos.

## Hardware

| Componente | Descricao |
|---|---|
| **Placa** | nRF52840 DK (PCA10056) |
| **MCU** | nRF52840 (ARM Cortex-M4F, 64 MHz, 1 MB Flash, 256 KB RAM) |
| **Radio** | Bluetooth 5.0 / BLE integrado |
| **Interface UART** | VCOM via J-Link USB onboard |

## Pre-requisitos

### Software

- [nRF Connect SDK](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-SDK) v3.x (testado com v3.0.2)
- [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Desktop) com Toolchain Manager
- Terminal serial: [PuTTY](https://www.putty.org/), minicom ou Tera Term

### Hardware

- nRF52840 DK (PCA10056) conectada via USB
- Celular com Bluetooth 4.0+ e app **nRF Connect**:
  - [Android](https://play.google.com/store/apps/details?id=no.nordicsemi.android.mcp)
  - [iOS](https://apps.apple.com/app/nrf-connect-for-mobile/id1054362403)

## Estrutura do Projeto

```
nrf52840_ble_uart/
├── CMakeLists.txt      # Configuracao de build CMake/Zephyr
├── prj.conf            # Configuracao do kernel (Kconfig)
├── app.overlay         # Overlay do devicetree (define UART do NUS)
├── src/
│   └── main.c          # Codigo fonte principal (~420 linhas)
├── .gitignore
├── LICENSE
└── README.md
```

## Como Compilar e Gravar

### 1. Abra o terminal do nRF Connect SDK

Use o terminal do **Toolchain Manager** ou a extensao **nRF Connect** no VS Code / Windsurf. O `west` precisa estar no PATH.

### 2. Clone e compile

```bash
git clone https://github.com/Dante-138/nrf52840-ble-uart.git
cd nrf52840-ble-uart
west build -b nrf52840dk/nrf52840 . --no-sysbuild
```

> **Nota:** O `--no-sysbuild` e necessario no nRF Connect SDK v3.x para projetos standalone.

> **Nota:** O projeto deve estar em um caminho **sem espacos** (ex: `C:\ncs\projects\`). Caminhos como `C:\Users\Meu Nome\...` causam erro no CMake.

### 3. Grave na placa

```bash
west flash --runner jlink
```

## Como Usar

### 1. Configurar o PuTTY

Abra o Gerenciador de Dispositivos e encontre a porta COM da DK (ex: COM3, COM5).

| Parametro | Valor |
|---|---|
| **Connection type** | Serial |
| **Porta** | COMx (varia por PC) |
| **Baud Rate** | 115200 |
| **Data Bits** | 8 |
| **Stop Bits** | 1 |
| **Parity** | None |
| **Flow Control** | None |

### 2. Conectar pelo celular

1. Abra o app **nRF Connect** no celular
2. Toque em **Scan**
3. Encontre **"nRF52840_BLE_UART"** e toque em **Connect**
4. O **LED2** da DK acendera (conexao estabelecida)
5. Role ate encontrar o servico **Nordic UART Service** (UUID `6E400001-...`)
6. Na **TX Characteristic** (UUID `6E400003-...`), toque no icone de setas/sino para **habilitar notificacoes** — isso e obrigatorio para receber mensagens do PuTTY
7. Na **RX Characteristic** (UUID `6E400002-...`), toque na seta para cima para enviar mensagens para o PuTTY

### 3. Testar a comunicacao

| Direcao | Como testar |
|---|---|
| **PuTTY -> Celular** | Digite texto no PuTTY + **Enter**. A mensagem aparece na TX characteristic do app |
| **Celular -> PuTTY** | Escreva na RX characteristic do app e envie. A mensagem aparece no PuTTY |

## Limitacoes Conhecidas

| Limitacao | Motivo |
|---|---|
| **Sem echo local no PuTTY** | O firmware nao ecoa os caracteres digitados de volta. A digitacao e "as cegas", mas a mensagem e enviada ao pressionar Enter |
| **Limite de ~20 caracteres por mensagem** | Limitacao do MTU padrao do BLE. Mensagens maiores sao fragmentadas ou truncadas |
| **Envio apenas com Enter** | O firmware acumula caracteres e so envia via BLE ao receber `\r` ou `\n` |

## Detalhes Tecnicos

### Arquitetura

O firmware usa dois caminhos de dados independentes:

**UART -> BLE (Thread dedicada)**
```
Tecla no PuTTY
  -> UART RX (async API com DMA)
    -> Buffer acumula ate \r ou \n
      -> FIFO (fifo_uart_rx_data)
        -> ble_write_thread
          -> bt_nus_send()
            -> BLE -> Celular
```

**BLE -> UART (Callback direto)**
```
Mensagem no app
  -> BLE RX
    -> bt_receive_cb()
      -> uart_tx()
        -> PuTTY
```

### Thread Model

| Thread | Funcao | Stack | Prioridade |
|---|---|---|---|
| **main** | Init BLE/UART, pisca LED1 | default | 0 |
| **ble_write_thread** | Le FIFO e envia via BLE | 1024 B | 7 |

### Configuracoes Kconfig (prj.conf)

```ini
# Bluetooth
CONFIG_BT=y                    # Habilita stack BLE
CONFIG_BT_PERIPHERAL=y         # Modo periferico (aceita conexoes)
CONFIG_BT_NUS=y                # Nordic UART Service
CONFIG_BT_DEVICE_NAME="nRF52840_BLE_UART"

# UART
CONFIG_SERIAL=y                # Driver serial
CONFIG_UART_ASYNC_API=y        # API assincrona (eventos + DMA)
CONFIG_NRFX_UARTE0=y           # Driver nativo UARTE0
CONFIG_UART_0_ASYNC=y           # Modo async para UART0

# DK
CONFIG_DK_LIBRARY=y            # Biblioteca de LEDs/botoes

# Logging via RTT (nao UART, para nao conflitar com NUS)
CONFIG_LOG=y
CONFIG_USE_SEGGER_RTT=y
CONFIG_LOG_BACKEND_RTT=y
CONFIG_UART_CONSOLE=n
```

### Overlay (app.overlay)

```dts
/ {
    chosen {
        nordic,nus-uart = &uart0;
    };
};
```

Define que o NUS usara a **UART0**, que no nRF52840 DK e roteada pela interface J-Link USB (VCOM), ou seja, a mesma porta que aparece no PuTTY.

### Memoria Utilizada

```
Memory region    Used Size  Region Size  %age Used
       FLASH:     ~169 KB       1 MB     ~16%
         RAM:      ~37 KB     256 KB     ~14%
```

## Troubleshooting

| Problema | Solucao |
|---|---|
| Dispositivo nao aparece no scan | Verifique se o firmware foi gravado corretamente. Resete a DK (botao RESET) |
| Conexao BLE falha | Apague bonding antigo: `nrfjprog --eraseall` + `west flash --runner jlink`. No celular, esqueca o dispositivo nas configuracoes de Bluetooth |
| PuTTY nao mostra nada | Verifique a porta COM no Gerenciador de Dispositivos. Confirme baud rate 115200 |
| Mensagens nao chegam no celular | Habilite **notificacoes** na TX Characteristic do Nordic UART Service no app nRF Connect |
| Erro de build com espacos no path | Mova o projeto para um caminho sem espacos, ex: `C:\ncs\projects\` |
| `west: command not found` | Use o terminal do Toolchain Manager ou a extensao nRF Connect |

## Licenca

MIT License - veja [LICENSE](LICENSE)
