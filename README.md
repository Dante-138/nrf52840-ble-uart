<div align="center">

# nRF52840 BLE UART (Nordic UART Service)

**Firmware embarcado para nRF52840 DK — Ponte bidirecional UART ↔ Bluetooth Low Energy**

![Zephyr](https://img.shields.io/badge/Zephyr_RTOS-v3.7-blue?logo=zephyr&logoColor=white)
![nRF Connect SDK](https://img.shields.io/badge/nRF_Connect_SDK-v3.0.2-00A9CE?logo=nordicsemiconductor&logoColor=white)
![License](https://img.shields.io/badge/License-MIT-green)
![Platform](https://img.shields.io/badge/Platform-nRF52840_DK-orange)
![BLE](https://img.shields.io/badge/Bluetooth-5.0_LE-0082FC?logo=bluetooth&logoColor=white)

*Firmware que transforma o nRF52840 DK em uma ponte wireless entre um terminal serial (PuTTY) e um smartphone via Bluetooth Low Energy, usando o Nordic UART Service (NUS).*

</div>

---

## Visão Geral

Comunicação bidirecional em tempo real entre um **PC (terminal serial)** e um **celular (app BLE)**:

```
+------+          +-------------+          +-----------+
| PC   |--UART--->| nRF52840 DK |---BLE--->| Celular   |
|PuTTY |<--UART---| (Firmware)  |<--BLE----| nRF App   |
+------+          +-------------+          +-----------+
```

- **Digite no PuTTY** → mensagem aparece no celular
- **Digite no celular** → mensagem aparece no PuTTY
- Comunicação wireless sem fios via **Bluetooth Low Energy**

### LEDs de Status

| LED | Significado |
|-----|-------------|
| LED1 piscando | Firmware rodando normalmente |
| LED2 aceso | Dispositivo BLE conectado |
| LED2 apagado | Aguardando conexão BLE |

## Demonstração

### Terminal Serial (PuTTY)

```
==================================
  nRF52840 BLE UART (NUS)
==================================
Aguardando conexão BLE...
Nome: nRF52840_BLE_UART
```

Após conectar pelo celular, tudo que você digita no PuTTY é enviado via BLE, e tudo que o celular envia aparece no PuTTY.

### App nRF Connect (Celular)

1. Scan → Encontra **"nRF52840_BLE_UART"**
2. Connect → LED2 acende na placa
3. Nordic UART Service → Habilita notificações na TX
4. Envia mensagem pela RX → Aparece no PuTTY

## Arquitetura do Firmware

```
+-----------------------------------------------------------+
|                 UART -> BLE (Thread dedicada)              |
|                                                           |
|  PuTTY [tecla]                                            |
|    -> UART RX (Async API + DMA)                           |
|      -> Buffer acumula até \r ou \n                       |
|        -> K_FIFO (fifo_uart_rx_data)                      |
|          -> ble_write_thread                              |
|            -> bt_nus_send() -> BLE -> Celular             |
+-----------------------------------------------------------+

+-----------------------------------------------------------+
|                 BLE -> UART (Callback direto)              |
|                                                           |
|  Celular [mensagem]                                       |
|    -> BLE RX                                              |
|      -> bt_receive_cb()                                   |
|        -> uart_tx() -> PuTTY                              |
+-----------------------------------------------------------+
```

**Por que dois caminhos diferentes?**
- **UART→BLE**: Precisa de thread dedicada porque `bt_nus_send()` pode bloquear. Usa FIFO para desacoplar a ISR da UART do envio BLE.
- **BLE→UART**: O callback `bt_receive_cb` é chamado em contexto de thread BLE, então pode chamar `uart_tx` diretamente sem problemas.

## Hardware

| Componente | Descrição |
|------------|-----------|
| **Placa** | nRF52840 DK (PCA10056) |
| **MCU** | nRF52840 — ARM Cortex-M4F, 64 MHz, 1 MB Flash, 256 KB RAM |
| **Radio** | Bluetooth 5.0 / BLE integrado |
| **Interface UART** | J-Link VCOM via USB onboard (115200 baud) |

## Pré-requisitos

- [nRF Connect SDK](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-SDK) v3.x (testado com v3.0.2)
- [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Desktop) com Toolchain Manager
- Terminal serial: [PuTTY](https://www.putty.org/), minicom ou Tera Term
- nRF52840 DK (PCA10056) conectada via USB
- Celular com app **nRF Connect**:
  - [Android](https://play.google.com/store/apps/details?id=no.nordicsemi.android.mcp)
  - [iOS](https://apps.apple.com/app/nrf-connect-for-mobile/id1054362403)

## Estrutura do Projeto

```
nrf52840_ble_uart/
├── CMakeLists.txt          # Configuração de build (CMake/Zephyr)
├── prj.conf                # Configuração do kernel (Kconfig)
├── app.overlay             # Overlay do devicetree (mapeamento UART)
├── src/
│   └── main.c              # Código fonte do firmware (~440 linhas)
├── .gitignore
├── LICENSE                  # MIT
└── README.md
```

## Build & Flash

### 1. Clone o repositório

```bash
git clone https://github.com/Dante-138/nrf52840-ble-uart.git
```

### 2. Copie para um caminho sem espaços (necessário para CMake/GCC)

```bash
xcopy /E /I nrf52840-ble-uart C:\ncs\projects\nrf52840_ble_uart
```

### 3. Compile (no terminal do nRF Connect SDK Toolchain)

```bash
west build -b nrf52840dk/nrf52840 C:\ncs\projects\nrf52840_ble_uart ^
  --no-sysbuild --build-dir C:\ncs\projects\nrf52840_ble_uart\build --pristine
```

> **Nota:** O `--no-sysbuild` é necessário no nRF Connect SDK v3.x para projetos standalone.

### 4. Grave na placa

```bash
west flash --runner jlink --build-dir C:\ncs\projects\nrf52840_ble_uart\build
```

## Como Testar

### 1. Configurar o PuTTY

| Parâmetro | Valor |
|-----------|-------|
| **Connection type** | Serial |
| **Porta** | COMx (ver Gerenciador de Dispositivos) |
| **Baud Rate** | 115200 |
| **Data Bits** | 8 |
| **Stop Bits** | 1 |
| **Parity** | None |
| **Flow Control** | None |

### 2. Conectar pelo celular

1. Abra o app **nRF Connect** no celular
2. Toque em **Scan**
3. Encontre **"nRF52840_BLE_UART"** e toque em **Connect**
4. O **LED2** da DK acenderá (conexão estabelecida)
5. Role até **Nordic UART Service** (UUID `6E400001-...`)
6. Na **TX Characteristic** (UUID `6E400003-...`), habilite **notificações** (ícone de setas)
7. Na **RX Characteristic** (UUID `6E400002-...`), toque na seta para enviar texto

### 3. Testar comunicação

| Direção | Como testar |
|---------|-------------|
| **PuTTY → Celular** | Digite texto no PuTTY + **Enter**. Mensagem aparece na TX characteristic |
| **Celular → PuTTY** | Escreva na RX characteristic e envie. Mensagem aparece no PuTTY |

## Detalhes Técnicos

### Nordic UART Service (NUS)

| Characteristic | UUID | Função |
|---------------|------|--------|
| **RX** | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | Celular → nRF52840 (escrita) |
| **TX** | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | nRF52840 → Celular (notificação) |

### Thread Model

| Thread | Função | Stack | Prioridade |
|--------|--------|-------|------------|
| **main** | Init BLE/UART, pisca LED1 | default | 0 |
| **ble_write_thread** | Lê FIFO e envia via BLE | 1024 B | 7 |

### Stack Tecnológico

| Camada | Tecnologia |
|--------|------------|
| **RTOS** | Zephyr RTOS v3.7 |
| **SDK** | nRF Connect SDK v3.0.2 |
| **BLE Stack** | Zephyr Bluetooth (SoftDevice Controller) |
| **BLE Service** | Nordic UART Service (NUS) |
| **UART** | Async API com DMA |
| **Concorrência** | K_FIFO + K_SEM + K_THREAD |
| **Build** | CMake + Ninja via West |

### Configurações Kconfig (prj.conf)

```ini
CONFIG_BT=y                    # Habilita stack BLE
CONFIG_BT_PERIPHERAL=y         # Modo periférico (aceita conexões)
CONFIG_BT_NUS=y                # Nordic UART Service
CONFIG_BT_DEVICE_NAME="nRF52840_BLE_UART"

CONFIG_SERIAL=y                # UART para comunicação com PuTTY
CONFIG_UART_ASYNC_API=y        # API assíncrona com DMA
CONFIG_DK_LIBRARY=y            # Abstrai LEDs e botões do DK

CONFIG_LOG=y                   # Logging via RTT (não interfere na UART)
CONFIG_USE_SEGGER_RTT=y
CONFIG_UART_CONSOLE=n          # UART reservada para dados NUS
```

## Limitações Conhecidas

| Limitação | Motivo |
|-----------|--------|
| Sem echo local no PuTTY | Firmware não ecoa caracteres. Digitação é "às cegas", mas envia ao pressionar Enter |
| ~20 caracteres por mensagem | Limitação do MTU padrão BLE. Mensagens maiores são fragmentadas |
| Envio apenas com Enter | Firmware acumula caracteres e só envia via BLE ao receber `\r` ou `\n` |

## Troubleshooting

| Problema | Solução |
|----------|---------|
| Dispositivo não aparece no scan | Verifique se o firmware foi gravado. Resete a DK (botão RESET) |
| Conexão BLE falha | Apague bonding: `nrfjprog --eraseall` + reflash. No celular, esqueça o dispositivo |
| PuTTY não mostra nada | Verifique porta COM e baud rate 115200. Flow Control = None |
| Mensagens não chegam no celular | Habilite **notificações** na TX Characteristic no nRF Connect |
| Erro de build com espaços no path | Copie para `C:\ncs\projects\` e use `--build-dir` |
| `west: command not found` | Use o terminal do Toolchain Manager |

## Licença

MIT License — veja [LICENSE](LICENSE)

---

<div align="center">

**Desenvolvido com** Nordic nRF52840 DK **+** Zephyr RTOS **+** nRF Connect SDK

</div>
