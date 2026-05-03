#include "sensors.h" // Header file for sensor-related functionality
#include <freertos/FreeRTOS.h> // FreeRTOS library for task management
#include "IBme280.h" // Interface for BME280 sensor communication
#include "bme280.h" // BME280 sensor driver
#include "esp_log.h" // ESP-IDF logging library

struct {
  struct bme280_dev bmedev; // Structure to hold BME280 device configuration
  i2c_master_dev_handle_t i2c_handle;  // Handle for the I2C device
} sensors;

static const char* TAG = "SENSORS"; // Tag used for logging messages

int8_t sensors_init(const sensorConfig* config) {

  // Copy the I2C handle from the configuration to the persistent structure
  sensors.i2c_handle = config->bmeDev;

  // Initialize the BME280 device structure with function pointers
  sensors.bmedev.read = ibme280_i2c_read; // Function to read data from the sensor
  sensors.bmedev.write = ibme280_i2c_write; // Function to write data to the sensor
  sensors.bmedev.delay_us = ibme280_delay_us; // Function to introduce delays

  // Set the interface pointer to the persistent I2C handle
  sensors.bmedev.intf_ptr = &sensors.i2c_handle;

  sensors.bmedev.intf = BME280_I2C_INTF; // Specify the interface type as I2C

  // Initialize the BME280 sensor with the configured structure
  int8_t result = bme280_init(&sensors.bmedev);
  if (result != BME280_OK) {
    ESP_LOGE(TAG, "BME280 init failed with error: %d", result); // Log error if initialization fails
    return result;
  }

  ESP_LOGI(TAG, "BME280 Initialized"); // Log success message

  ESP_LOGI(TAG, "Config BME280 settings"); // Log message for configuring settings
  struct bme280_settings settingsBme; // Structure to hold sensor settings

  // Retrieve the current sensor settings
  result = bme280_get_sensor_settings(&settingsBme, &sensors.bmedev);
  if (result != BME280_OK) {
    ESP_LOGE(TAG, "BME280 get settings failed: %d", result); // Log error if retrieval fails
    return result;
  }

  // Configure oversampling settings for humidity, temperature, and pressure
  settingsBme.osr_h = 5;
  settingsBme.osr_t = 5;
  settingsBme.osr_p = 5;

  // Apply the configured settings to the sensor
  result = bme280_set_sensor_settings(BME280_SEL_ALL_SETTINGS,
                                      &settingsBme,
                                      &sensors.bmedev);
  if (result != BME280_OK) {
    ESP_LOGE(TAG, "BME280 set settings failed: %d", result); // Log error if settings application fails
    return result;
  }

  // Set the sensor to normal power mode
  result = bme280_set_sensor_mode(BME280_POWERMODE_NORMAL, &sensors.bmedev);
  if (result != BME280_OK) {
    ESP_LOGE(TAG, "BME280 set mode failed: %d", result); // Log error if mode setting fails
    return result;
  }

  ESP_LOGI(TAG, "BME280 configured successfully"); // Log success message
  return BME280_OK; // Return success code
}

void sensors_update(SensorData* data) {

  struct bme280_data bmeData; // Structure to hold sensor data
  // Retrieve sensor data for pressure, humidity, and temperature
  bme280_get_sensor_data(BME280_ALL, &bmeData, &sensors.bmedev);

  // Store the retrieved data in the provided structure
  data->bme.pressure = bmeData.pressure;
  data->bme.humidity = bmeData.humidity;
  data->bme.airTemp = bmeData.temperature;
}
