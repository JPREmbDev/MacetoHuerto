#include <freertos/FreeRTOS.h>
#include "comms.h"
#include "pump.h"
#include "scanneri2c.h"
#include "sensors.h"
#include "system.h"

void app_main() {
  /*
  system_init();

  sensor_init();
  */
  for (;;) {

    scanneri2c_scan();
    vTaskDelay(100);

    /*
    sensor_update();

    // Procesar los datos:

    comms_sendData();

    pump_actuate();

    system_slepp();
    */
  }
}