#include "scanneri2c.h"  // Header file for the I2C scanner functionality
#include <driver/i2c_master.h>  // ESP-IDF driver for I2C master operations
#include <esp_log.h>            // ESP-IDF logging library
#include <freertos/FreeRTOS.h>  // FreeRTOS library for task management
#include <string.h>             // Standard library for string operations

static const char* TAG = "SCANNER";  // Tag used for logging messages

void scanneri2c_scan(void) {
  ESP_LOGI(TAG,
           "-- Iniciando escaneo I2C --");  // Log the start of the I2C scan
  vTaskDelay(
      pdMS_TO_TICKS(1000));  // Delay for 1 second to allow initialization

  i2c_master_bus_handle_t bus_handle;  // Handle for the I2C master bus
  i2c_master_bus_config_t bus_config = {
      .clk_source = I2C_CLK_SRC_DEFAULT,  // Default clock source for I2C
      .i2c_port = I2C_NUM_0,              // Use I2C port 0
      .scl_io_num = GPIO_NUM_9,           // GPIO pin for SCL (clock line)
      .sda_io_num = GPIO_NUM_8,           // GPIO pin for SDA (data line)
      .glitch_ignore_cnt = 7,  // Ignore glitches shorter than 7 clock cycles
      .flags.enable_internal_pullup =
          true,  // Enable internal pull-up resistors if external ones are not available
  };

  // Initialize the I2C master bus with the specified configuration
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

  bool device_found = false;  // Flag to indicate if any I2C device is found

  while (!device_found) {
    printf(
        "\nEscaneando bus I2C...\n");  // Print message indicating the start of scanning
    printf(
        "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");  // Print column headers
    printf("00:          ");  // Print initial spacing for the first row

    for (uint8_t i = 0x03; i < 0x78;
         i++) {  // Iterate through all possible I2C addresses
      if (i % 16 == 0) {
        printf("\n%.2x:", i);  // Print row header for every 16 addresses
      }

      // Probe the I2C address to check if a device responds
      esp_err_t ret =
          i2c_master_probe(bus_handle, i, 50);  // Timeout of 50ms for probing

      if (ret == ESP_OK) {
        printf(" %.2x", i);   // Print the address of the detected device
        device_found = true;  // Set the flag to true if a device is found
      } else {
        printf(" --");  // Print placeholder for addresses with no response
      }
    }

    if (!device_found) {
      printf(
          "\n\n[!] No se detectó nada. Reintando en 2 segundos...");  // Print message if no devices are found
      vTaskDelay(pdMS_TO_TICKS(2000));  // Delay for 2 seconds before retrying
    }
  }

  printf(
      "\n\n[+] Escaneo finalizado con éxito.\n\n");  // Print success message when scanning is complete

  // Delete the I2C master bus to free resources
  i2c_del_master_bus(bus_handle);
}