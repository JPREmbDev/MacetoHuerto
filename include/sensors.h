#ifndef SENSORS_H__
#define SENSORS_H__

#include <driver/i2c_master.h>
#include "bme280.h"

typedef struct {
  /* data */
  i2c_master_dev_handle_t bmeDev;
} sensorConfig;

typedef struct {
  struct {
    /* data */
    float pressure;
    float humidity;
    float airTemp;
  } bme;

} SensorData;

int8_t sensors_init(const sensorConfig* config);

void sensors_update(SensorData* data);

#endif  //SENSORS_H__