#include "sensors.h"
#include "system.h"

void app_main() {

  system_init();

  sensor_init();

  for (;;) {
    sensor_update();

    // Procesar los datos:

    comms_sendData();

    pump_actuate();

    system_slepp();
  }
}