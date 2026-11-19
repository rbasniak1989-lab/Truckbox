#pragma once

#include "sdkconfig.h"

#if CONFIG_TB_BOARD_REVC
#define TB_BOARD_IS_REVC 1
/* LILYGO H707 V1.1, schematic sheets 2–3. Header U5 numbers are pad numbers. */
#define TB_J1708_UART UART_NUM_2
#define TB_J1708_RX 6
#define TB_J1708_DE 7
#define TB_J1708_UNUSED_TX UART_PIN_NO_CHANGE
#define TB_SD_SCK 21
#define TB_SD_MISO 47
#define TB_SD_MOSI 14
#define TB_SD_CS 13
#define TB_MODEM_RESET 17
#define TB_MODEM_PWRKEY 18
#define TB_MODEM_DTR 9
#else
#define TB_BOARD_IS_REVC 0
#define TB_J1708_UART UART_NUM_1
#define TB_J1708_RX 20
#define TB_J1708_DE 21
#define TB_J1708_UNUSED_TX 5
#define TB_SD_SCK 6
#define TB_SD_MISO 8
#define TB_SD_MOSI 7
#define TB_SD_CS 3
#define TB_MODEM_PWRKEY 0
#endif

#if CONFIG_TB_BOARD_REVC && !CONFIG_TB_REVC_J1708_TX
#define TB_J1708_TX_ALLOWED 0
#else
#define TB_J1708_TX_ALLOWED 1
#endif
