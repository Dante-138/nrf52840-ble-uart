/*
 * Firmware nRF52840 DK - BLE UART (Nordic UART Service)
 *
 * Ponte bidirecional UART <-> BLE:
 *   PuTTY -> UART -> nRF52840 -> BLE -> Celular (nRF Connect app)
 *   Celular -> BLE -> nRF52840 -> UART -> PuTTY
 *
 * LEDs:
 *   LED1 piscando = sistema rodando
 *   LED2 aceso    = dispositivo BLE conectado
 */

#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>

#include <bluetooth/services/nus.h>
#include <dk_buttons_and_leds.h>
#include <zephyr/settings/settings.h>

#include <stdio.h>
#include <string.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ble_uart);

/* ---------- Definicoes ---------- */
#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define RUN_STATUS_LED     DK_LED1
#define CON_STATUS_LED     DK_LED2
#define LED_BLINK_INTERVAL 1000

#define UART_BUF_SIZE 160
#define UART_RX_TIMEOUT_MS 100

#define BLE_WRITE_THREAD_STACK 1024
#define BLE_WRITE_THREAD_PRIO  7

/* ---------- Variaveis globais ---------- */
static const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(nordic_nus_uart));
static struct bt_conn *current_conn;
static struct k_work_delayable uart_work;
static K_SEM_DEFINE(ble_init_ok, 0, 1);

/* Estrutura para dados UART */
struct uart_data_t {
	void *fifo_reserved;
	uint8_t data[UART_BUF_SIZE];
	uint16_t len;
};

/* FIFOs para comunicacao entre UART e BLE */
static K_FIFO_DEFINE(fifo_uart_tx_data);  /* BLE -> UART */
static K_FIFO_DEFINE(fifo_uart_rx_data);  /* UART -> BLE */

/* ---------- Advertising BLE ---------- */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

/* ---------- UART Callbacks ---------- */
static void uart_cb(const struct device *dev, struct uart_event *evt,
		    void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	static size_t aborted_len;
	static uint8_t *aborted_buf;
	static bool disable_req;
	struct uart_data_t *buf;

	switch (evt->type) {
	case UART_TX_DONE:
		LOG_DBG("UART_TX_DONE");
		if (!evt->data.tx.buf || evt->data.tx.len == 0) {
			return;
		}

		if (aborted_buf) {
			buf = CONTAINER_OF(aborted_buf, struct uart_data_t, data[0]);
			aborted_buf = NULL;
			aborted_len = 0;
		} else {
			buf = CONTAINER_OF(evt->data.tx.buf, struct uart_data_t, data[0]);
		}

		k_free(buf);

		/* Envia proximo buffer da fila, se houver */
		buf = k_fifo_get(&fifo_uart_tx_data, K_NO_WAIT);
		if (buf) {
			uart_tx(uart, buf->data, buf->len, SYS_FOREVER_MS);
		}
		break;

	case UART_RX_RDY:
		LOG_DBG("UART_RX_RDY: %d bytes", evt->data.rx.len);
		buf = CONTAINER_OF(evt->data.rx.buf, struct uart_data_t, data[0]);
		buf->len += evt->data.rx.len;

		if (disable_req) {
			return;
		}

		/* Quando recebe \r ou \n, desabilita RX para processar */
		if ((evt->data.rx.buf[buf->len - 1] == '\n') ||
		    (evt->data.rx.buf[buf->len - 1] == '\r')) {
			disable_req = true;
			uart_rx_disable(uart);
		}
		break;

	case UART_RX_DISABLED:
		LOG_DBG("UART_RX_DISABLED");
		disable_req = false;

		buf = k_malloc(sizeof(*buf));
		if (buf) {
			buf->len = 0;
		} else {
			LOG_WRN("Sem memoria para buffer UART RX");
			k_work_reschedule(&uart_work, K_MSEC(50));
			return;
		}

		uart_rx_enable(uart, buf->data, sizeof(buf->data),
			       UART_RX_TIMEOUT_MS);
		break;

	case UART_RX_BUF_REQUEST:
		LOG_DBG("UART_RX_BUF_REQUEST");
		buf = k_malloc(sizeof(*buf));
		if (buf) {
			buf->len = 0;
			uart_rx_buf_rsp(uart, buf->data, sizeof(buf->data));
		} else {
			LOG_WRN("Sem memoria para buffer UART RX (buf_request)");
		}
		break;

	case UART_RX_BUF_RELEASED:
		LOG_DBG("UART_RX_BUF_RELEASED: len=%d",
			CONTAINER_OF(evt->data.rx_buf.buf, struct uart_data_t, data[0])->len);
		buf = CONTAINER_OF(evt->data.rx_buf.buf, struct uart_data_t, data[0]);
		if (buf->len > 0) {
			k_fifo_put(&fifo_uart_rx_data, buf);
		} else {
			k_free(buf);
		}
		break;

	case UART_TX_ABORTED:
		LOG_DBG("UART_TX_ABORTED");
		if (!aborted_buf) {
			aborted_buf = (uint8_t *)evt->data.tx.buf;
		}
		aborted_len += evt->data.tx.len;
		buf = CONTAINER_OF((void *)aborted_buf, struct uart_data_t, data);
		uart_tx(uart, &buf->data[aborted_len],
			buf->len - aborted_len, SYS_FOREVER_MS);
		break;

	default:
		break;
	}
}

static void uart_work_handler(struct k_work *item)
{
	struct uart_data_t *buf = k_malloc(sizeof(*buf));

	if (buf) {
		buf->len = 0;
		uart_rx_enable(uart, buf->data, sizeof(buf->data),
			       UART_RX_TIMEOUT_MS);
	} else {
		LOG_WRN("Sem memoria para buffer UART RX");
		k_work_reschedule(&uart_work, K_MSEC(50));
	}
}

static int uart_init(void)
{
	int err;
	struct uart_data_t *rx;
	struct uart_data_t *tx;

	if (!device_is_ready(uart)) {
		LOG_ERR("UART nao esta pronta");
		return -ENODEV;
	}

	rx = k_malloc(sizeof(*rx));
	if (!rx) {
		return -ENOMEM;
	}
	rx->len = 0;

	k_work_init_delayable(&uart_work, uart_work_handler);

	err = uart_callback_set(uart, uart_cb, NULL);
	if (err) {
		k_free(rx);
		LOG_ERR("Erro ao configurar callback UART: %d", err);
		return err;
	}

	/* Mensagem de boas-vindas */
	tx = k_malloc(sizeof(*tx));
	if (tx) {
		tx->len = snprintf(tx->data, sizeof(tx->data),
			"\r\n==================================\r\n"
			"  nRF52840 BLE UART (NUS)\r\n"
			"==================================\r\n"
			"Aguardando conexao BLE...\r\n"
			"Nome: %s\r\n\r\n", DEVICE_NAME);

		err = uart_tx(uart, tx->data, tx->len, SYS_FOREVER_MS);
		if (err) {
			k_free(tx);
		}
	}

	/* Habilita recepcao UART */
	err = uart_rx_enable(uart, rx->data, sizeof(rx->data), UART_RX_TIMEOUT_MS);
	if (err) {
		LOG_ERR("Erro ao habilitar RX UART: %d", err);
		k_free(rx);
	}

	return err;
}

/* ---------- BLE Callbacks ---------- */
static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (err) {
		LOG_ERR("Falha na conexao: 0x%02x", err);
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Conectado: %s", addr);

	current_conn = bt_conn_ref(conn);
	dk_set_led_on(CON_STATUS_LED);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Desconectado: %s (reason 0x%02x)", addr, reason);

	if (current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}

	dk_set_led_off(CON_STATUS_LED);
}

static void recycled_cb(void)
{
	/* Reconecta automaticamente apos desconexao */
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad),
				  sd, ARRAY_SIZE(sd));
	if (err) {
		LOG_ERR("Erro ao reiniciar advertising: %d", err);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected    = connected,
	.disconnected = disconnected,
	.recycled     = recycled_cb,
};

/* ---------- NUS Callback (BLE -> UART) ---------- */
static void bt_receive_cb(struct bt_conn *conn, const uint8_t *const data,
			   uint16_t len)
{
	char addr[BT_ADDR_LE_STR_LEN] = {0};

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, ARRAY_SIZE(addr));
	LOG_INF("Dados recebidos do celular (%s): %d bytes", addr, len);

	for (uint16_t pos = 0; pos != len;) {
		struct uart_data_t *tx = k_malloc(sizeof(*tx));

		if (!tx) {
			LOG_WRN("Sem memoria para TX UART");
			return;
		}

		/* Reserva 1 byte para possivel \n */
		size_t tx_data_size = sizeof(tx->data) - 1;
		tx->len = MIN(tx_data_size, len - pos);

		memcpy(tx->data, &data[pos], tx->len);
		pos += tx->len;

		/* Adiciona \n se o ultimo caractere for \r */
		if (pos == len && data[len - 1] == '\r') {
			tx->data[tx->len] = '\n';
			tx->len++;
		}

		int err = uart_tx(uart, tx->data, tx->len, SYS_FOREVER_MS);
		if (err) {
			k_fifo_put(&fifo_uart_tx_data, tx);
		}
	}
}

static struct bt_nus_cb nus_cb = {
	.received = bt_receive_cb,
};

/* ---------- Thread BLE Write (UART -> BLE) ---------- */
void ble_write_thread(void)
{
	/* Espera BLE inicializar */
	k_sem_take(&ble_init_ok, K_FOREVER);

	struct uart_data_t nus_data = { .len = 0 };

	for (;;) {
		struct uart_data_t *buf = k_fifo_get(&fifo_uart_rx_data,
						     K_FOREVER);

		int loc = 0;
		int plen = MIN(sizeof(nus_data.data) - nus_data.len, buf->len);

		while (plen > 0) {
			memcpy(&nus_data.data[nus_data.len], &buf->data[loc], plen);
			nus_data.len += plen;
			loc += plen;

			/* Envia quando buffer cheio ou recebe \r/\n */
			if (nus_data.len >= sizeof(nus_data.data) ||
			    nus_data.data[nus_data.len - 1] == '\n' ||
			    nus_data.data[nus_data.len - 1] == '\r') {
				if (bt_nus_send(NULL, nus_data.data, nus_data.len)) {
					LOG_WRN("Falha ao enviar via BLE");
				}
				nus_data.len = 0;
			}

			plen = MIN(sizeof(nus_data.data), buf->len - loc);
		}

		k_free(buf);
	}
}

K_THREAD_DEFINE(ble_write_thread_id, BLE_WRITE_THREAD_STACK,
		ble_write_thread, NULL, NULL, NULL,
		BLE_WRITE_THREAD_PRIO, 0, 0);

/* ---------- Main ---------- */
int main(void)
{
	int err;
	int blink_status = 0;

	/* Inicializa LEDs */
	err = dk_leds_init();
	if (err) {
		LOG_ERR("Erro ao inicializar LEDs: %d", err);
	}

	/* Inicializa UART */
	err = uart_init();
	if (err) {
		LOG_ERR("Erro ao inicializar UART: %d", err);
		return -1;
	}

	/* Inicializa Bluetooth */
	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Erro ao inicializar Bluetooth: %d", err);
		return -1;
	}
	LOG_INF("Bluetooth inicializado");

	/* Libera a thread de escrita BLE */
	k_sem_give(&ble_init_ok);

	/* Carrega settings (bonding, etc) */
	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	/* Inicializa NUS */
	err = bt_nus_init(&nus_cb);
	if (err) {
		LOG_ERR("Erro ao inicializar NUS: %d", err);
		return -1;
	}

	/* Inicia advertising */
	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad),
			      sd, ARRAY_SIZE(sd));
	if (err) {
		LOG_ERR("Erro ao iniciar advertising: %d", err);
		return -1;
	}
	LOG_INF("Advertising iniciado como \"%s\"", DEVICE_NAME);

	/* Loop principal - pisca LED1 */
	for (;;) {
		dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
		k_sleep(K_MSEC(LED_BLINK_INTERVAL));
	}

	return 0;
}
