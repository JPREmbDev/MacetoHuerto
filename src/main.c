#include <freertos/FreeRTOS.h>
#include "bme280.h"
#include "comms.h"
#include "esp_log.h"
#include "pump.h"
#include "scanneri2c.h"
#include "sensors.h"
#include "system.h"

static const char* TAG = "MAIN";

void app_main() {

  // Esperamos 2 seg
  vTaskDelay(pdMS_TO_TICKS(3000));

  // AÑADE ESTO ANTES DE system_init()
  ESP_LOGI(TAG, "Scanning I2C bus...");
  scanneri2c_scan();  // ← Escanear el bus
  vTaskDelay(pdMS_TO_TICKS(2000));

  ESP_LOGI(TAG, "INITIALIZE MAIN APP");
  ESP_LOGI(TAG, "START SYSTEM INIT");
  SystemDev* sysDevs = system_init();

  // Verificar que system_init no falló
  if (sysDevs == NULL) {
    ESP_LOGE(TAG, "System init failed!");
    return;
  }

  ESP_LOGI(TAG, "SYSTEM INIT INITIALIZED");

  // Esperamos 2 seg
  vTaskDelay(pdMS_TO_TICKS(2000));

  sensorConfig sensorConfig = {.bmeDev = sysDevs->bme};
  ESP_LOGI(TAG, "START SENSOR INIT");

  int8_t sensor_result = sensors_init(&sensorConfig);
  if (sensor_result != BME280_OK) {
    ESP_LOGE(TAG, "Sensor init failed with error: %d", sensor_result);
    return;
  }

  SensorData data = {0};  // Inicializar a cero

  for (;;) {

    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "Read data:");
    sensors_update(&data);
    printf("Data: %.2f, %.2f, %.2f\n",
           data.bme.pressure,
           data.bme.humidity,
           data.bme.airTemp);
    vTaskDelay(pdMS_TO_TICKS(1000));
    /*
    sensor_update();

    // Procesar los datos:

    comms_sendData();

    pump_actuate();

    system_slepp();
    */
  }
}