#include <stdint.h>
#include <string.h>
#include "nordic_common.h"
#include "nrf.h"
#include "ble_hci.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "ble_conn_params.h"
#include "softdevice_handler.h"
#include "app_timer.h"
#include "app_uart.h"
#include "app_util_platform.h"
#include "nrf_gpio.h"
#include "nrf_drv_clock.h"
#include "app_fifo.h"

#include "SEGGER_RTT.h"
#include "ble_barts.h"
#include "hardware_conf.h"
#include "led.h"


#ifdef DEBUG
#define DBG(...) printf(__VA_ARGS__)
#else
#define DBG(...)
#endif


#define APP_FEATURE_NOT_SUPPORTED       BLE_GATT_STATUS_ATTERR_APP_BEGIN + 2        /**< Reply when unsupported features are requested. */

#define CENTRAL_LINK_COUNT              0                                           /**< Number of central links used by the application. When changing this number remember to adjust the RAM settings*/
#define PERIPHERAL_LINK_COUNT           1                                           /**< Number of peripheral links used by the application. When changing this number remember to adjust the RAM settings*/

#define DEVICE_NAME                     "BART"                               /**< Name of device. Will be included in the advertising data. */
#define BARTS_SERVICE_UUID_TYPE           BLE_UUID_TYPE_VENDOR_BEGIN                  /**< UUID type for the Nordic UART Service (vendor specific). */

#define APP_ADV_INTERVAL                64                                          /**< The advertising interval (in units of 0.625 ms. This value corresponds to 100 ms). */
#define APP_ADV_TIMEOUT_IN_SECONDS      180                                         	/**< The advertising timeout (in units of seconds). */

#define MIN_CONN_INTERVAL               MSEC_TO_UNITS(7.5, UNIT_1_25_MS)             /**< Minimum acceptable connection interval (7.5 ms), Connection interval uses 1.25 ms units. */
#define MAX_CONN_INTERVAL               MSEC_TO_UNITS(20, UNIT_1_25_MS)             /**< Maximum acceptable connection interval (20 ms), Connection interval uses 1.25 ms units. */
#define SLAVE_LATENCY                   0                                           /**< Slave latency. */
#define CONN_SUP_TIMEOUT                MSEC_TO_UNITS(4000, UNIT_10_MS)             /**< Connection supervisory timeout (4 seconds), Supervision Timeout uses 10 ms units. */
#define FIRST_CONN_PARAMS_UPDATE_DELAY  APP_TIMER_TICKS(5000, APP_TIMER_PRESCALER)  /**< Time from initiating event (connect or start of notification) to first time sd_ble_gap_conn_param_update is called (5 seconds). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY   APP_TIMER_TICKS(30000, APP_TIMER_PRESCALER) /**< Time between each call to sd_ble_gap_conn_param_update after the first call (30 seconds). */
#define MAX_CONN_PARAMS_UPDATE_COUNT    3                                           /**< Number of attempts before giving up the connection parameter negotiation. */

static ble_barts_t					m_barts;
static uint16_t                         m_conn_handle = BLE_CONN_HANDLE_INVALID;    /**< Handle of the current connection. */

static ble_uuid_t                       m_adv_uuids[] = {{.uuid = BLE_UUID_BARTS_SERVICE, .type = BARTS_SERVICE_UUID_TYPE}};


extern uint8_t uart_rcv_buff[UART_RCV_BUF_SIZE];
extern app_fifo_t uart_rcv_fifo;

static uint8_t ble_send_buf[20];
static uint32_t ble_to_send_size;
static bool is_remain_ble_to_send = false;
static bool is_ble_sending = false;
static bool is_ble_send_req = false;

static void transfer_data_from_uart_buf_to_ble()
{
	uint32_t err_code;

	while (1) {
		if (!is_remain_ble_to_send) {
			ble_to_send_size = 20;
			app_fifo_read(&uart_rcv_fifo, ble_send_buf, &ble_to_send_size);
			if (ble_to_send_size == 0) break;
		}

		DBG("[BLE send]");
		for (int i=0;i<ble_to_send_size;i++) {
			DBG("%02x",ble_send_buf[i]);
		}
		DBG("\n");

		err_code = ble_barts_send(&m_barts, ble_send_buf, ble_to_send_size);

		if (err_code == NRF_SUCCESS) {
			DBG("[BLE send] success\n");
			is_remain_ble_to_send = false;
		}
		else if (err_code == BLE_ERROR_NO_TX_PACKETS || NRF_ERROR_BUSY) {
			DBG("[BLE send] buffer full\n");
			is_remain_ble_to_send = true;
			break;
		}
		else if (err_code != NRF_ERROR_INVALID_STATE) {
			is_remain_ble_to_send = true;
			APP_ERROR_CHECK(err_code);
			break;
		}
	}

}

static void gap_params_init(void)
{
	uint32_t                err_code;
	ble_gap_conn_params_t   gap_conn_params;
	ble_gap_conn_sec_mode_t sec_mode;

	BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

	err_code = sd_ble_gap_device_name_set(&sec_mode,
			(const uint8_t *) DEVICE_NAME,
			strlen(DEVICE_NAME));
	APP_ERROR_CHECK(err_code);

	memset(&gap_conn_params, 0, sizeof(gap_conn_params));

	gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
	gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
	gap_conn_params.slave_latency     = SLAVE_LATENCY;
	gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

	err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
	APP_ERROR_CHECK(err_code);
}


static void barts_data_handler(ble_barts_t * p_barts, uint8_t * p_data, uint16_t length)
{
	DBG("[UART send]");
	for (uint32_t i = 0; i < length; i++) {
		while (app_uart_put(p_data[i]) != NRF_SUCCESS);
		DBG("%c",p_data[i]);
	}
	DBG("\n");
}


static void services_init(uint8_t device_id)
{
	uint32_t       err_code;
	ble_barts_init_t barts_init;

	memset(&barts_init, 0, sizeof(barts_init));

	barts_init.data_handler = barts_data_handler;

	// device id の設定
	barts_init.device_id = device_id;
	DBG("[barts] Device ID : %u\n", barts_init.device_id);

	err_code = ble_barts_init(&m_barts, &barts_init);
	APP_ERROR_CHECK(err_code);
}


static void on_conn_params_evt(ble_conn_params_evt_t * p_evt)
{
	uint32_t err_code;

	if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED) {
		err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
		APP_ERROR_CHECK(err_code);
	}
}


static void conn_params_error_handler(uint32_t nrf_error)
{
	APP_ERROR_HANDLER(nrf_error);
}


static void conn_params_init(void)
{
	uint32_t               err_code;
	ble_conn_params_init_t cp_init;

	memset(&cp_init, 0, sizeof(cp_init));

	cp_init.p_conn_params                  = NULL;
	cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
	cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
	cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
	cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
	cp_init.disconnect_on_fail             = false;
	cp_init.evt_handler                    = on_conn_params_evt;
	cp_init.error_handler                  = conn_params_error_handler;

	err_code = ble_conn_params_init(&cp_init);
	APP_ERROR_CHECK(err_code);
}


static void on_adv_evt(ble_adv_evt_t ble_adv_evt)
{
	switch (ble_adv_evt) {
	case BLE_ADV_EVT_FAST:
		DBG("[ADV event] BLE_ADV_EVT_FAST\n");
		break;
	case BLE_ADV_EVT_IDLE:
		DBG("[ADV event] BLE_ADV_EVT_IDLE\n");
		uint32_t err_code = ble_advertising_start(BLE_ADV_MODE_FAST);
		APP_ERROR_CHECK(err_code);
		break;
	default:
		break;
	}
}


static void on_ble_evt(ble_evt_t * p_ble_evt)
{
	uint32_t                         err_code;
	uint8_t dummy;

	switch (p_ble_evt->header.evt_id) {
	case BLE_GATTS_EVT_WRITE:
		led_blink(RX_LED);
		DBG("[BLE event] BLE_GATTS_EVT_WRITE\n");
		break;

	case BLE_GATTS_EVT_HVC:
		DBG("[BLE event] BLE_GATTS_EVT_HVC\n");
		break;

	case BLE_GAP_EVT_CONNECTED:
		DBG("[BLE event] BLE_GAP_EVT_CONNECTED\n");
		led_blink(TX_LED);
		led_blink(RX_LED);
		m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
		break; // BLE_GAP_EVT_CONNECTED

	case BLE_GAP_EVT_DISCONNECTED:
		DBG("[BLE event] BLE_GAP_EVT_DISCONNECTED\n");
		led_on(TX_LED);
		led_off(RX_LED);
		m_conn_handle = BLE_CONN_HANDLE_INVALID;
		break; // BLE_GAP_EVT_DISCONNECTED

	case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
		DBG("[BLE event] BLE_GAP_EVT_SEC_PARAMS_REQUEST\n");
		// Pairing not supported
		err_code = sd_ble_gap_sec_params_reply(m_conn_handle, BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP, NULL, NULL);
		APP_ERROR_CHECK(err_code);
		break; // BLE_GAP_EVT_SEC_PARAMS_REQUEST

	case BLE_GATTS_EVT_SYS_ATTR_MISSING:
		DBG("[BLE event] BLE_GATTS_EVT_SYS_ATTR_MISSING\n");
		// No system attributes have been stored.
		err_code = sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0, 0);
		APP_ERROR_CHECK(err_code);
		break; // BLE_GATTS_EVT_SYS_ATTR_MISSING

	case BLE_GATTC_EVT_TIMEOUT:
		DBG("[BLE event] BLE_GATTC_EVT_TIMEOUT\n");
		// Disconnect on GATT Client timeout event.
		err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
				BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
		APP_ERROR_CHECK(err_code);
		break; // BLE_GATTC_EVT_TIMEOUT

	case BLE_GATTS_EVT_TIMEOUT:
		DBG("[BLE event] BLE_GATTS_EVT_TIMEOUT\n");
		// Disconnect on GATT Server timeout event.
		err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
				BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
		APP_ERROR_CHECK(err_code);
		break; // BLE_GATTS_EVT_TIMEOUT

	case BLE_EVT_USER_MEM_REQUEST:
		DBG("[BLE event] BLE_EVT_USER_MEM_REQUEST\n");
		err_code = sd_ble_user_mem_reply(p_ble_evt->evt.gattc_evt.conn_handle, NULL);
		APP_ERROR_CHECK(err_code);
		break; // BLE_EVT_USER_MEM_REQUEST

	case BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST:
		DBG("[BLE event] BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST\n");
		{
			ble_gatts_evt_rw_authorize_request_t  req;
			ble_gatts_rw_authorize_reply_params_t auth_reply;

			req = p_ble_evt->evt.gatts_evt.params.authorize_request;

			if (req.type != BLE_GATTS_AUTHORIZE_TYPE_INVALID)
			{
				if ((req.request.write.op == BLE_GATTS_OP_PREP_WRITE_REQ)     ||
						(req.request.write.op == BLE_GATTS_OP_EXEC_WRITE_REQ_NOW) ||
						(req.request.write.op == BLE_GATTS_OP_EXEC_WRITE_REQ_CANCEL))
				{
					if (req.type == BLE_GATTS_AUTHORIZE_TYPE_WRITE)
					{
						auth_reply.type = BLE_GATTS_AUTHORIZE_TYPE_WRITE;
					}
					else
					{
						auth_reply.type = BLE_GATTS_AUTHORIZE_TYPE_READ;
					}
					auth_reply.params.write.gatt_status = APP_FEATURE_NOT_SUPPORTED;
					err_code = sd_ble_gatts_rw_authorize_reply(p_ble_evt->evt.gatts_evt.conn_handle,
							&auth_reply);
					APP_ERROR_CHECK(err_code);
				}
			}
		} break; // BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST

	case BLE_EVT_TX_COMPLETE:
		led_blink(TX_LED);
		// fifoにデータが残っている
		if (app_fifo_peek(&uart_rcv_fifo, 0, &dummy) == NRF_SUCCESS) {
			transfer_data_from_uart_buf_to_ble();
		}
		else {
			is_ble_sending = false;
		}
		break;

	default:
		// No implementation needed.
		break;
	}
}


static void ble_evt_dispatch(ble_evt_t * p_ble_evt)
{
	ble_conn_params_on_ble_evt(p_ble_evt);
	on_ble_evt(p_ble_evt);
	ble_barts_on_ble_evt(&m_barts, p_ble_evt);
	ble_advertising_on_ble_evt(p_ble_evt);

}


static void ble_stack_init(void)
{
	uint32_t err_code;

	nrf_clock_lf_cfg_t clock_lf_cfg = NRF_CLOCK_LFCLKSRC;

	// Initialize SoftDevice.
	SOFTDEVICE_HANDLER_INIT(&clock_lf_cfg, NULL);

	ble_enable_params_t ble_enable_params;
	err_code = softdevice_enable_get_default_config(CENTRAL_LINK_COUNT,
			PERIPHERAL_LINK_COUNT,
			&ble_enable_params);
	APP_ERROR_CHECK(err_code);

	//Check the ram settings against the used number of links
	CHECK_RAM_START_ADDR(CENTRAL_LINK_COUNT,PERIPHERAL_LINK_COUNT);

	// 各Bandwidthの許容する接続数を設定
	ble_conn_bw_counts_t bw_counts = {
			.tx_counts = {.high_count=1, .mid_count=0, .low_count=0},
			.rx_counts = {.high_count=1, .mid_count=0, .low_count=0}
	};
	ble_enable_params.common_enable_params.p_conn_bw_counts = &bw_counts;

	// Enable BLE stack.
	err_code = softdevice_enable(&ble_enable_params);
	APP_ERROR_CHECK(err_code);

	// Bandwidthをhihgに設定
	ble_opt_t ble_opt;
	memset(&ble_opt, 0x00, sizeof(ble_opt));
	ble_opt.common_opt.conn_bw.conn_bw.conn_bw_tx = BLE_CONN_BW_HIGH;
	ble_opt.common_opt.conn_bw.conn_bw.conn_bw_rx = BLE_CONN_BW_HIGH;
	ble_opt.common_opt.conn_bw.role = BLE_GAP_ROLE_PERIPH;

	err_code = sd_ble_opt_set(BLE_COMMON_OPT_CONN_BW, &ble_opt);
	APP_ERROR_CHECK(err_code);

	// Subscribe for BLE events.
	err_code = softdevice_ble_evt_handler_set(ble_evt_dispatch);
	APP_ERROR_CHECK(err_code);
}


void server_uart_event_handle(app_uart_evt_t * p_event)
{
	uint8_t rcv_data;

	switch (p_event->evt_type) {
	case APP_UART_DATA_READY:
		app_uart_get(&rcv_data);

		// 未接続時はデータを無視
		if (m_conn_handle == BLE_CONN_HANDLE_INVALID) break;

		app_fifo_put(&uart_rcv_fifo, rcv_data);

		if (!is_ble_sending) {
			is_ble_send_req = true;
		}

		break;

	case APP_UART_COMMUNICATION_ERROR:
		DBG("APP_UART_COMMUNICATION_ERROR\n");
		APP_ERROR_HANDLER(p_event->data.error_communication);
		break;

	case APP_UART_FIFO_ERROR:
		DBG("APP_UART_FIFO_ERROR\n");
		APP_ERROR_HANDLER(p_event->data.error_code);
		break;

	default:
		break;
	}
}





static void advertising_init(void)
{
	uint32_t               err_code;
	ble_advdata_t          advdata;
	ble_advdata_t          scanrsp;
	ble_adv_modes_config_t options;

	// Build advertising data struct to pass into @ref ble_advertising_init.
	memset(&advdata, 0, sizeof(advdata));
	advdata.name_type          = BLE_ADVDATA_FULL_NAME;
	advdata.include_appearance = false;
	advdata.flags              = BLE_GAP_ADV_FLAGS_LE_ONLY_LIMITED_DISC_MODE;

	memset(&scanrsp, 0, sizeof(scanrsp));
	scanrsp.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
	scanrsp.uuids_complete.p_uuids  = m_adv_uuids;

	memset(&options, 0, sizeof(options));
	options.ble_adv_fast_enabled  = true;
	options.ble_adv_fast_interval = APP_ADV_INTERVAL;
	options.ble_adv_fast_timeout  = APP_ADV_TIMEOUT_IN_SECONDS;

	err_code = ble_advertising_init(&advdata, &scanrsp, &options, on_adv_evt, NULL);
	APP_ERROR_CHECK(err_code);
}

static void clock_init()
{
	// 外部のXTALを起動
	uint32_t err_code = nrf_drv_clock_init();
	APP_ERROR_CHECK(err_code);

	nrf_clock_hfclk_t clk_src = nrf_clock_hf_src_get();
	if (clk_src == NRF_CLOCK_HFCLK_LOW_ACCURACY) {
		DBG("NRF_CLOCK_HFCLK_LOW_ACCURACY\n");
	}
	else {
		DBG("NRF_CLOCK_HFCLK_HIGH_ACCURACY\n");
	}

	if (nrf_clock_hf_is_running(NRF_CLOCK_HFCLK_LOW_ACCURACY)) {
		DBG("LOW RUNNING\n");
	}

	if (nrf_clock_hf_is_running(NRF_CLOCK_HFCLK_HIGH_ACCURACY)) {
		DBG("HIGH RUNNING\n");
	}
}

void server_main(uint8_t device_id)
{
	uint32_t err_code;

	app_fifo_init(&uart_rcv_fifo, uart_rcv_buff, UART_RCV_BUF_SIZE);

	ble_stack_init();
	clock_init();
	gap_params_init();
	services_init(device_id);
	advertising_init();
	conn_params_init();

	//送信強度を4dB(最大)に設定
	err_code = sd_ble_gap_tx_power_set(4);
	APP_ERROR_CHECK(err_code);

	DBG("ble2uart Service Start\n");

	err_code = ble_advertising_start(BLE_ADV_MODE_FAST);
	APP_ERROR_CHECK(err_code);

	led_on(TX_LED);
	led_off(RX_LED);

	// Enter main loop.
	while (1) {
		if (is_ble_send_req) {
			is_ble_send_req = false;
			transfer_data_from_uart_buf_to_ble();
			is_ble_sending = true;
		}
		uint32_t err_code = sd_app_evt_wait();
		APP_ERROR_CHECK(err_code);

	}
}

