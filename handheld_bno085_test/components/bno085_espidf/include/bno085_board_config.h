#pragma once

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#define BNO085_I2C_PORT          I2C_NUM_0
#define BNO085_I2C_ADDRESS       CONFIG_BNO085_I2C_ADDRESS
#define BNO085_I2C_SPEED_HZ      CONFIG_BNO085_I2C_SPEED_HZ
#define BNO085_I2C_SDA_GPIO      GPIO_NUM_39
#define BNO085_I2C_SCL_GPIO      GPIO_NUM_40
#define BNO085_INT_GPIO          GPIO_NUM_41
#define BNO085_RESET_GPIO        GPIO_NUM_42
#define BNO085_REPORT_US         CONFIG_BNO085_REPORT_INTERVAL_US
#define BNO085_LOG_DIVIDER       5U
#define BNO085_I2C_TIMEOUT_MS    100
#define BNO085_RESET_LOW_MS      20
#define BNO085_BOOT_WAIT_MS      1000
#define BNO085_MAX_CONSECUTIVE_ERRORS 5U
