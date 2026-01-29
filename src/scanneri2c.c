#include "scanneri2c.h"
#include <driver/i2c_master.h>
#include <esp_log.h>

const char* TAG = "SCANNER";

void scanneri2c_scan(void) {
  ESP_LOGE(TAG, "--Starting scan--");

  i2c_master_bus_handle_t busHandle;
  i2c_master_bus_config_t busConfig;

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

  printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
  printf("00:          ");
  for (uint8_t i = 0x03; i < 0x78; i++) {
    if (i % 16 == 0) {
      printf("\n%.2x:", i);
    }

    esp_err_t ret = i2c_master_probe(busHandle, i, 100);

    if (ret == ESP_OK) {
      printf(" %.2x", i);
    } else {
      printf(" --");
    }
  }
  printf("\n\n");
  i2c_del_master_bus(busHandle);
}
