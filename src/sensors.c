#include "sensors.h"
#include <freertos/FreeRTOS.h>
#include "IBme280.h"
#include "bme280.h"
#include "esp_log.h"

struct {
  struct bme280_dev bmedev;
  i2c_master_dev_handle_t i2c_handle;  // Guardar el handle aquí
} sensors;

static const char* TAG = "SENSORS";

int8_t sensors_init(const sensorConfig* config) {

  // Copiar el handle a nuestra estructura (memoria persistente)
  sensors.i2c_handle = config->bmeDev;

  // Primero debemos completar el puntero bme280_dev -> bmedev
  sensors.bmedev.read = ibme280_i2c_read;
  sensors.bmedev.write = ibme280_i2c_write;
  sensors.bmedev.delay_us = ibme280_delay_us;

  // Interface Pointer: ahora apunta a nuestra copia persistente
  sensors.bmedev.intf_ptr = &sensors.i2c_handle;

  sensors.bmedev.intf = BME280_I2C_INTF;

  // Luego de completar el puntero bmedev, se lo pasamos al INIT
  int8_t result = bme280_init(&sensors.bmedev);
  if (result != BME280_OK) {
    ESP_LOGE(TAG, "BME280 init failed with error: %d", result);
    return result;
  }

  ESP_LOGI(TAG, "BME280 Initialized");

  ESP_LOGI(TAG, "Config BME280 settings");
  struct bme280_settings settingsBme;

  result = bme280_get_sensor_settings(&settingsBme, &sensors.bmedev);
  if (result != BME280_OK) {
    ESP_LOGE(TAG, "BME280 get settings failed: %d", result);
    return result;
  }

  settingsBme.osr_h = 5;
  settingsBme.osr_t = 5;
  settingsBme.osr_p = 5;

  result = bme280_set_sensor_settings(BME280_SEL_ALL_SETTINGS,
                                      &settingsBme,
                                      &sensors.bmedev);
  if (result != BME280_OK) {
    ESP_LOGE(TAG, "BME280 set settings failed: %d", result);
    return result;
  }

  result = bme280_set_sensor_mode(BME280_POWERMODE_NORMAL, &sensors.bmedev);
  if (result != BME280_OK) {
    ESP_LOGE(TAG, "BME280 set mode failed: %d", result);
    return result;
  }

  ESP_LOGI(TAG, "BME280 configured successfully");
  return BME280_OK;
}

void sensors_update(SensorData* data) {

  struct bme280_data bmeData;
  bme280_get_sensor_data(BME280_ALL, &bmeData, &sensors.bmedev);

  data->bme.pressure = bmeData.pressure;
  data->bme.humidity = bmeData.humidity;
  data->bme.airTemp = bmeData.temperature;
}
