#include "IBme280.h" // Header file for BME280 I2C interface
#include <string.h> // Standard library for memory operations
#include "driver/i2c_master.h" // ESP-IDF driver for I2C master operations
#include "esp_log.h" // ESP-IDF logging library
#include "rom/ets_sys.h" // ESP-IDF library for delay functions

#define SEND_TIMEOUT_MS 1000 // Timeout for I2C operations in milliseconds

static const char* TAG = "IBme280"; // Tag used for logging messages

BME280_INTF_RET_TYPE ibme280_i2c_read(uint8_t reg_addr,
                                      uint8_t* reg_data,
                                      uint32_t len,
                                      void* intf_ptr) {

  i2c_master_dev_handle_t i2cDev = *(i2c_master_dev_handle_t*)intf_ptr; // Retrieve the I2C device handle

  // Local buffer to hold the register address
  uint8_t reg = reg_addr;

  // Perform an I2C read operation
  esp_err_t ret = i2c_master_transmit_receive(i2cDev,
                                              &reg,  // Pointer to the register address
                                              1,     // Size of the register address (1 byte)
                                              reg_data, // Buffer to store the read data
                                              len, // Length of data to read
                                              SEND_TIMEOUT_MS); // Timeout for the operation

  if (ret != ESP_OK) {
    ESP_LOGE(TAG,
             "I2C read failed: reg=0x%02X, len=%d, err=%d",
             reg_addr,
             len,
             ret); // Log error if the read operation fails
  }

  return ret; // Return the result of the I2C operation
}

BME280_INTF_RET_TYPE
ibme280_i2c_write(uint8_t reg_addr,
                  const uint8_t* reg_data,
                  uint32_t len,
                  void* intf_ptr) {

  i2c_master_dev_handle_t i2cDev = *(i2c_master_dev_handle_t*)intf_ptr; // Retrieve the I2C device handle

  // Local buffer to hold the register address and data
  uint8_t buffer[64];  // Sufficient size for BME280 operations

  // Check for buffer overflow
  if (len > sizeof(buffer) - 1) {
    ESP_LOGE(TAG,
             "Write data too long: %d bytes (max %d)",
             len,
             sizeof(buffer) - 1); // Log error if data length exceeds buffer size
    return BME280_E_INVALID_LEN; // Return error code for invalid length
  }

  buffer[0] = reg_addr; // Set the register address
  memcpy(&buffer[1], reg_data, len); // Copy the data to the buffer

  // Perform an I2C write operation
  esp_err_t ret =
      i2c_master_transmit(i2cDev, buffer, len + 1, SEND_TIMEOUT_MS);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG,
             "I2C write failed: reg=0x%02X, len=%d, err=%d",
             reg_addr,
             len,
             ret); // Log error if the write operation fails
  }

  return ret; // Return the result of the I2C operation
}

void ibme280_delay_us(uint32_t period, void* intf_ptr) {
  ets_delay_us(period); // Introduce a delay in microseconds
}