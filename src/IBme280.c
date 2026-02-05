#include "IBme280.h"
#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "rom/ets_sys.h"

#define SEND_TIMEOUT_MS 1000

static const char* TAG = "IBme280";

BME280_INTF_RET_TYPE ibme280_i2c_read(uint8_t reg_addr,
                                      uint8_t* reg_data,
                                      uint32_t len,
                                      void* intf_ptr) {

  i2c_master_dev_handle_t i2cDev = *(i2c_master_dev_handle_t*)intf_ptr;

  // Buffer local de 1 byte para la dirección del registro
  uint8_t reg = reg_addr;

  esp_err_t ret = i2c_master_transmit_receive(i2cDev,
                                              &reg,  // Solo 1 byte
                                              1,     // Tamaño: 1 byte
                                              reg_data,
                                              len,
                                              SEND_TIMEOUT_MS);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG,
             "I2C read failed: reg=0x%02X, len=%d, err=%d",
             reg_addr,
             len,
             ret);
  }

  return ret;
}

BME280_INTF_RET_TYPE
ibme280_i2c_write(uint8_t reg_addr,
                  const uint8_t* reg_data,
                  uint32_t len,
                  void* intf_ptr) {

  i2c_master_dev_handle_t i2cDev = *(i2c_master_dev_handle_t*)intf_ptr;

  // Buffer local: [reg_addr][data...]
  uint8_t buffer[64];  // Suficiente para operaciones BME280

  // Verificar overflow
  if (len > sizeof(buffer) - 1) {
    ESP_LOGE(TAG,
             "Write data too long: %d bytes (max %d)",
             len,
             sizeof(buffer) - 1);
    return BME280_E_INVALID_LEN;
  }

  buffer[0] = reg_addr;
  memcpy(&buffer[1], reg_data, len);

  esp_err_t ret =
      i2c_master_transmit(i2cDev, buffer, len + 1, SEND_TIMEOUT_MS);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG,
             "I2C write failed: reg=0x%02X, len=%d, err=%d",
             reg_addr,
             len,
             ret);
  }

  return ret;
}

void ibme280_delay_us(uint32_t period, void* intf_ptr) {
  ets_delay_us(period);
}