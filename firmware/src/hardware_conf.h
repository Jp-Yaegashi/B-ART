/*
 * hardware_conf.h
 *
 *  Created on: 2016/11/29
 *      Author: idt12312
 */

#ifndef SRC_HARDWARE_CONF_H_
#define SRC_HARDWARE_CONF_H_

#define TX_LED_PIN_NUMBER	21
#define RX_LED_PIN_NUMBER	19

#define	TX_PIN_NUMBER	12
#define	RX_PIN_NUMBER	11
#define	RTS_PIN_NUMBER	0
#define	CTS_PIN_NUMBER	0

#define DEVICEID_BIT0_PIN_NUMBER 3
#define DEVICEID_BIT1_PIN_NUMBER 2
#define DEVICEID_BIT2_PIN_NUMBER 1
#define DEVICE_MODE_PIN_NUMBER 0


#define UART_TX_BUF_SIZE                128                                         /**< UART TX buffer size. */
#define UART_RX_BUF_SIZE                64                                         /**< UART RX buffer size. */
#define UART_RCV_BUF_SIZE				128

/**< Value of the RTC1 PRESCALER register. */
#define APP_TIMER_PRESCALER 0

#define APP_TIMER_OP_QUEUE_SIZE         5                                           /**< Size of timer operation queues. */


#define NRF_CLOCK_LFCLKSRC      {.source        = NRF_CLOCK_LF_SRC_RC,            \
                                 .rc_ctiv       = 32,                                \
                                 .rc_temp_ctiv  = 1,                                \
                                 .xtal_accuracy = NRF_CLOCK_LF_XTAL_ACCURACY_250_PPM}



#endif /* SRC_HARDWARE_CONF_H_ */
