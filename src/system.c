#include "system.h"
#include <driver/i2c_master.h>
#include <string.h>

#define BME280_ADDR 0x76

i2c_master_dev_handle_t bmeDev;
i2c_master_bus_config_t busConfig;

SystemDev globalDevs;

SystemDev* system_init(void) {

  i2c_master_bus_handle_t busHandle;

  memset(&busConfig, 0, sizeof(i2c_master_bus_config_t));
  busConfig.i2c_port = I2C_NUM_0;
  busConfig.scl_io_num = GPIO_NUM_9;
  busConfig.sda_io_num = GPIO_NUM_8;
  busConfig.intr_priority = 0;
  busConfig.flags.enable_internal_pullup = 1;
  busConfig.flags.allow_pd = 0;
  busConfig.glitch_ignore_cnt = 7;
  busConfig.clk_source = I2C_CLK_SRC_DEFAULT;

  ESP_ERROR_CHECK(i2c_new_master_bus(&busConfig, &busHandle));

  i2c_device_config_t devConfig;
  memset(&devConfig, 0, sizeof(i2c_device_config_t));

  devConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  devConfig.device_address = BME280_ADDR;
  devConfig.scl_speed_hz = 100000;
  devConfig.scl_wait_us = 0;
  devConfig.flags.disable_ack_check = 0;
  ESP_ERROR_CHECK(
      i2c_master_bus_add_device(busHandle, &devConfig, &globalDevs.bme));

  return &globalDevs;
}

void system_sleep(void) {}
