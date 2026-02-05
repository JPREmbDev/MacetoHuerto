#ifndef SYSTEM_H__
#define SYSTEM_H__
#include <driver/i2c_master.h>

typedef struct system {
  /* data */
  i2c_master_dev_handle_t bme;

} SystemDev;

SystemDev* system_init(void);

void system_sleep(void);

#endif  //SYSTEM_H__