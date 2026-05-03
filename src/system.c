#include "system.h"  // Header file for system initialization and management
#include <driver/i2c_master.h>  // ESP-IDF driver for I2C master operations
#include <string.h>             // Standard library for memory operations

#define BME280_ADDR 0x76  // I2C address of the BME280 sensor

i2c_master_dev_handle_t bmeDev;     // Handle for the BME280 I2C device
i2c_master_bus_config_t busConfig;  // Configuration structure for the I2C bus

SystemDev globalDevs;  // Global structure to hold system devices

SystemDev* system_init(void) {

  i2c_master_bus_handle_t busHandle;  // Handle for the I2C bus

  // Initialize the I2C bus configuration structure with zeros
  memset(&busConfig, 0, sizeof(i2c_master_bus_config_t));
  busConfig.i2c_port = I2C_NUM_0;     // Use I2C port 0
  busConfig.scl_io_num = GPIO_NUM_9;  // GPIO pin for SCL (clock line)
  busConfig.sda_io_num = GPIO_NUM_8;  // GPIO pin for SDA (data line)
  busConfig.intr_priority = 0;  // Interrupt priority (0 = lowest priority)
  busConfig.flags.enable_internal_pullup =
      1;                         // Enable internal pull-up resistors
  busConfig.flags.allow_pd = 0;  // Disable pull-down resistors
  busConfig.glitch_ignore_cnt =
      7;  // Ignore glitches shorter than 7 clock cycles
  busConfig.clk_source = I2C_CLK_SRC_DEFAULT;  // Default clock source for I2C

  // Create a new I2C master bus with the specified configuration
  ESP_ERROR_CHECK(i2c_new_master_bus(&busConfig, &busHandle));

  i2c_device_config_t devConfig;  // Configuration structure for the I2C device
  memset(&devConfig,
         0,
         sizeof(i2c_device_config_t));  // Initialize the structure with zeros

  devConfig.dev_addr_length =
      I2C_ADDR_BIT_LEN_7;  // Use 7-bit addressing for the device
  devConfig.device_address =
      BME280_ADDR;                  // Set the I2C address of the BME280 sensor
  devConfig.scl_speed_hz = 100000;  // Set the clock speed to 100 kHz
  devConfig.scl_wait_us = 0;        // No additional wait time for SCL
  devConfig.flags.disable_ack_check = 0;  // Enable acknowledgment checking

  // Add the BME280 device to the I2C bus
  ESP_ERROR_CHECK(
      i2c_master_bus_add_device(busHandle, &devConfig, &globalDevs.bme));

  return &globalDevs;  // Return a pointer to the global device structure
}

void system_sleep(void) {
}  // Placeholder function for system sleep functionality
