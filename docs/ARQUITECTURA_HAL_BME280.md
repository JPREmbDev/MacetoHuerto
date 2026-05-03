# Documento Técnico: Arquitectura HAL y Configuración I2C para BME280

## Tabla de Contenidos
1. [Introducción](#introducción)
2. [Arquitectura Actual](#arquitectura-actual)
3. [Componentes del Sistema](#componentes-del-sistema)
4. [Configuración I2C Detallada](#configuración-i2c-detallada)
5. [Diseño de la Librería HAL](#diseño-de-la-librería-hal)
6. [Implementación de la HAL](#implementación-de-la-hal)
7. [Patrones de Diseño](#patrones-de-diseño)
8. [Mejores Prácticas](#mejores-prácticas)
9. [Troubleshooting Avanzado](#troubleshooting-avanzado)
10. [Referencias Técnicas](#referencias-técnicas)

---

## Introducción

Este documento detalla la arquitectura del sistema de sensores basado en el BME280 (sensor de presión,
humedad y temperatura) en un ESP32-C3, así como el proceso completo para abstraer esta funcionalidad
mediante una librería HAL (Hardware Abstraction Layer).

### Objetivos de este Documento

- **Para usuarios**: Entender cómo funciona el sistema actual
- **Para desarrolladores**: Comprender cómo abstraer hardware complejo
- **Para ingenieros senior**: Explorar patrones de diseño, escalabilidad y mejores prácticas

### Requisitos Previos

- Conocimiento básico de C/embedded systems
- Familiaridad con protocolos I2C
- Comprensión de arquitectura de drivers
- Experiencia con ESP-IDF

---

## Arquitectura Actual

### Diagrama de Alto Nivel

```
┌─────────────────────────────────────────────────────────────────┐
│               APLICACIÓN (main.c)                               │
│                  - Inicialización del sistema                   │
│                  - Bucle de lectura de sensores                 │
│                  - Procesamiento de datos                       │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────────────┐
│               CAPA DE SENSORES (sensors.c)                      │
│                   - Interface abstracta con el BME280           │
│                   - Conversión de datos                         │
│                   - Gestión de configuración                    │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────────────┐
│               INTERFAZ I2C HAL (IBme280.c)                      │
│                   - Lectura/escritura de registros              │
│                   - Manejo de timeouts                          │
│                   - Manejo de errores de I2C                    │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────────────┐
│               DRIVER I2C ESP-IDF (i2c_master.h)                 │
│                   - Driver nativo del ESP32-C3                  │
│                   - Control de pines SCL/SDA                    │
│                   - Manejo de clock y baudrate                  │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────────────┐
│              HARDWARE: SENSOR BME280 (0x76)                     │
│                   - Sensor físico conectado a pins GPIO8/GPIO9  │
│                   - Registros internos del sensor               │
└─────────────────────────────────────────────────────────────────┘
```

### Stack de Capas

```
NIVEL 5 (Aplicación)     | main() - Loop principal
NIVEL 4 (Negocio)        | sensors_update(), sensors_init()
NIVEL 3 (Abstracción I2C)| ibme280_i2c_read(), ibme280_i2c_write()
NIVEL 2 (Sistema)        | system_init(), bus configuration
NIVEL 1 (Driver)         | ESP-IDF I2C driver
NIVEL 0 (Hardware)       | Pins GPIO, BME280 sensor
```

---

## Componentes del Sistema

### 1. **system.c** - Inicialización del Bus I2C

**Responsabilidad**: Configurar el bus I2C maestro y agregar dispositivos

```c
SystemDev* system_init(void) {
    // Paso 1: Crear estructura de configuración del bus
    i2c_master_bus_config_t busConfig = {
        .i2c_port = I2C_NUM_0,           // Puerto I2C a usar
        .scl_io_num = GPIO_NUM_9,        // Pin del reloj (SCL)
        .sda_io_num = GPIO_NUM_8,        // Pin de datos (SDA)
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,          // Filtro anti-ruido
        .flags.enable_internal_pullup = 1 // Pull-ups internos
    };
    
    // Paso 2: Crear el bus maestro
    ESP_ERROR_CHECK(i2c_new_master_bus(&busConfig, &busHandle));
    
    // Paso 3: Configurar el dispositivo esclavo (BME280)
    i2c_device_config_t devConfig = {
        .device_address = BME280_ADDR,   // 0x76
        .scl_speed_hz = 100000,          // 100 kHz
        .dev_addr_length = I2C_ADDR_BIT_LEN_7 // Dirección de 7 bits
    };
    
    // Paso 4: Registrar el dispositivo en el bus
    ESP_ERROR_CHECK(i2c_master_bus_add_device(busHandle, &devConfig, &globalDevs.bme));
    
    return &globalDevs;
}
```

**¿Por qué esta estructura?**

- **Separación de responsabilidades**: El sistema solo configura, no interpreta datos
- **Reutilización**: Otros dispositivos I2C pueden agregarse fácilmente
- **Persistencia**: Mantiene handles globales para acceso desde otras capas

---

### 2. **IBme280.c** - Capa de Abstracción de Bajo Nivel

**Responsabilidad**: Abstracción del protocolo I2C específico

#### Función de Lectura I2C

```c
BME280_INTF_RET_TYPE ibme280_i2c_read(uint8_t reg_addr,
                                      uint8_t* reg_data,
                                      uint32_t len,
                                      void* intf_ptr) {
    // Obtener el handle del dispositivo I2C
    i2c_master_dev_handle_t i2cDev = *(i2c_master_dev_handle_t*)intf_ptr;
    
    // Buffer local para la dirección del registro
    uint8_t reg = reg_addr;
    
    // Operación I2C: Write-Read (write register addr, read data)
    esp_err_t ret = i2c_master_transmit_receive(
        i2cDev,          // Device handle
        &reg,            // Buffer de escritura (dirección)
        1,               // Tamaño: 1 byte
        reg_data,        // Buffer de lectura
        len,             // Cantidad de bytes a leer
        SEND_TIMEOUT_MS  // Timeout: 1 segundo
    );
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: reg=0x%02X", reg_addr);
    }
    
    return ret;
}
```

**¿Qué es `transmit_receive`?**

Esta es una operación compuesta I2C (comúnmente llamada "Repeated START"):

```
I2C Bus Timeline:
┌─────────────────────────────────────────────┐
│ START │ Addr+W │ RegAddr │ RESTART │ Addr+R │ Data │ STOP │
└─────────────────────────────────────────────┘
        └─ WRITE │        └─ READ ─┘
```

1. Envía dirección con bit WRITE (0)
2. Envía dirección del registro
3. Genera RESTART (sin soltar el bus)
4. Envía dirección con bit READ (1)
5. Lee N bytes de datos

#### Función de Escritura I2C

```c
BME280_INTF_RET_TYPE ibme280_i2c_write(uint8_t reg_addr,
                                       const uint8_t* reg_data,
                                       uint32_t len,
                                       void* intf_ptr) {
    i2c_master_dev_handle_t i2cDev = *(i2c_master_dev_handle_t*)intf_ptr;
    
    // Buffer: [RegAddr][Data...]
    uint8_t buffer[64];
    
    // Validar que no haya overflow
    if (len > sizeof(buffer) - 1) {
        ESP_LOGE(TAG, "Write data too long: %d bytes", len);
        return BME280_E_INVALID_LEN;
    }
    
    buffer[0] = reg_addr;
    memcpy(&buffer[1], reg_data, len);
    
    // Escribir todo de una vez
    esp_err_t ret = i2c_master_transmit(
        i2cDev,
        buffer,
        len + 1,
        SEND_TIMEOUT_MS
    );
    
    return ret;
}
```

**Detalle importante**: La escritura requiere:
- Dirección del registro + datos en un solo mensaje
- No se puede usar Repeated START aquí

---

### 3. **sensors.c** - Capa Lógica de Sensores

**Responsabilidad**: Inicializar el sensor y leer datos

```c
int8_t sensors_init(const sensorConfig* config) {
    // Paso 1: Copiar el handle I2C a estructura persistente
    sensors.i2c_handle = config->bmeDev;
    
    // Paso 2: Configurar callbacks I2C para el driver BME280
    sensors.bmedev.read = ibme280_i2c_read;      // Función de lectura
    sensors.bmedev.write = ibme280_i2c_write;    // Función de escritura
    sensors.bmedev.delay_us = ibme280_delay_us;  // Función de delay
    
    // Paso 3: Establecer el puntero a la interfaz
    sensors.bmedev.intf_ptr = &sensors.i2c_handle;
    sensors.bmedev.intf = BME280_I2C_INTF;
    
    // Paso 4: Inicializar el driver BME280
    // Esto lee calibration data del sensor
    int8_t result = bme280_init(&sensors.bmedev);
    if (result != BME280_OK) {
        return result;
    }
    
    // Paso 5: Configurar los oversampling settings
    struct bme280_settings settings;
    bme280_get_sensor_settings(&settings, &sensors.bmedev);
    
    // Oversampling 16x para máxima precisión (pero más lento)
    settings.osr_h = 5;  // 16x oversampling: humedad
    settings.osr_t = 5;  // 16x oversampling: temperatura
    settings.osr_p = 5;  // 16x oversampling: presión
    
    bme280_set_sensor_settings(BME280_SEL_ALL_SETTINGS, &settings, &sensors.bmedev);
    
    // Paso 6: Establecer modo normal (mediciones continuas)
    bme280_set_sensor_mode(BME280_POWERMODE_NORMAL, &sensors.bmedev);
    
    return BME280_OK;
}
```

**¿Por qué estos pasos?**

1. **Persistencia**: El handle debe persistir fuera de la función
2. **Callbacks**: El driver BME280 necesita funciones para comunicarse con I2C
3. **Calibración**: BME280 lee sus valores de calibración internos
4. **Configuración**: El oversampling afecta precisión vs velocidad
5. **Modo**: El modo NORMAL permite lecturas continuas

---

## Configuración I2C Detallada

### Parámetros Críticos

#### 1. **Velocidad del Reloj (scl_speed_hz = 100000)**

```
Estándar I2C:
┌─────────────┬─────────────────────┐
│ Estándar    │ Velocidad (kHz)     │
├─────────────┼─────────────────────┤
│ Standard    │ 100                 │ ← Usamos este
│ Fast        │ 400                 │
│ Fast Plus   │ 1000                │
│ High-Speed  │ 3400                │
└─────────────┴─────────────────────┘
```

**¿Por qué 100 kHz?**
- BME280 soporta hasta 400 kHz
- 100 kHz es más estable y menos susceptible a ruido
- Distancias cortas (placa) no requieren velocidad máxima

#### 2. **Pull-ups (enable_internal_pullup = true)**

```
Circuito I2C:
VCC
 │
 ├─── [Pull-up Resistor] ───┬─── SCL
 │                          │
 ├─── [Pull-up Resistor] ───┼─── SDA
 │                          │
 └──────────────────────────┤
                            │
                       (Bus abierto)
                            │
                      ESP32-C3
                      (Master)
                            │
                         BME280
                        (Slave)
```

**¿Por qué son necesarios?**
- I2C es un bus de colector abierto (open-drain)
- Sin pull-ups, el bus estaría "flotando"
- Las resistencias devuelven el bus a VCC cuando nadie transmite

**Valores típicos**: 4.7kΩ @ 3.3V (calculado para minimizar delay pero suficiente corriente)

#### 3. **Glitch Ignore Count (glitch_ignore_cnt = 7)**

```
Ruido en el bus I2C:
     ▄▄▄▄▄▄▄
    │       │
════╣       └─────────
    └─  Glitch
   (ruido rechazado)

Con glitch_ignore_cnt = 7:
Ignora pulsos más cortos que 7 ciclos de reloj
```

**Cálculo**:
- A 100 kHz, un ciclo = 10 µs
- 7 ciclos = 70 ns de filtrado
- Elimina ruido de líneas y EMI

---

## Diseño de la Librería HAL

### ¿Qué es HAL?

Una **Hardware Abstraction Layer** (HAL) es una capa software que:

```
NIVEL ALTO: Código Independiente del Hardware
    ↑
    │ (implementación generada)
    │
┌───┴─────────────────────────────────┐
│     HARDWARE ABSTRACTION LAYER       │
│  (Define interfaz, NO implementación)│
└───┬─────────────────────────────────┘
    │ (implementaciones concretas)
    ↓
NIVEL BAJO: Drivers Específicos (I2C ESP-IDF, GPIO, SPI, etc)
```

### Beneficios de una HAL

| Beneficio | Descripción |
|-----------|------------|
| **Portabilidad** | Cambiar ESP32 → STM32 sin cambiar lógica |
| **Testabilidad** | Mock de la HAL para unit tests |
| **Mantenibilidad** | Cambios en hardware no afectan aplicación |
| **Reutilización** | Mismo código en múltiples plataformas |
| **Escalabilidad** | Agregar hardware sin refactorizar |

### Arquitectura Propuesta

```
┌───────────────────────────────────────────────────┐
│   APLICACIÓN: macetohuerto_app.c                  │
│   - Control de riego                              │
│   - Lógica de negocio                             │
└───────────────┬─────────────────────────────────┘
                │
                ▼
┌───────────────────────────────────────────────────┐
│   HAL PÚBLICA: hal_sensors.h (interfaz)           │
│   - hal_sensor_init()                             │
│   - hal_sensor_read()                             │
│   - hal_sensor_deinit()                           │
└───────────────┬─────────────────────────────────┘
                │
    ┌───────────┴────────────┐
    ▼                        ▼
┌──────────────────┐  ┌──────────────────┐
│ hal_sensors_esp32│  │ hal_sensors_stm32│
│   (implementación)│  │ (implementación) │
│  Usa IBme280.c   │  │ Usa SPI driver   │
│  Usa I2C ESP-IDF │  │ Usa I2C STM32    │
└──────────────────┘  └──────────────────┘
```

---

## Implementación de la HAL

### Paso 1: Definir la Interfaz Pública

Crear archivo: `include/hal_sensors.h`

```c
#ifndef __HAL_SENSORS_H__
#define __HAL_SENSORS_H__

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Estructura para almacenar datos de sensores
 * @note Independiente de la implementación
 */
typedef struct {
    float temperature;  // Temperatura en °C
    float humidity;     // Humedad relativa en %
    float pressure;     // Presión en hPa
    uint32_t timestamp; // Timestamp de la lectura (ms)
} hal_sensor_data_t;

/**
 * @brief Estados posibles del sensor
 */
typedef enum {
    HAL_SENSOR_OK = 0,
    HAL_SENSOR_ERROR = -1,
    HAL_SENSOR_TIMEOUT = -2,
    HAL_SENSOR_NOT_FOUND = -3,
    HAL_SENSOR_CALIBRATION_ERROR = -4
} hal_sensor_status_t;

/**
 * @brief Configuración inicial del sensor
 */
typedef struct {
    uint32_t i2c_speed_hz;  // Velocidad I2C en Hz (ej: 100000)
    uint8_t scl_pin;        // Pin del reloj
    uint8_t sda_pin;        // Pin de datos
    uint8_t i2c_port;       // Puerto I2C (0, 1, etc)
    uint8_t device_addr;    // Dirección I2C del dispositivo
} hal_sensor_config_t;

/**
 * @brief Inicializar el subsistema de sensores
 * 
 * @param config Configuración del sensor
 * @return HAL_SENSOR_OK si éxito, otro valor en error
 * 
 * @note Debe llamarse una sola vez al inicio
 */
hal_sensor_status_t hal_sensor_init(const hal_sensor_config_t* config);

/**
 * @brief Leer datos del sensor
 * 
 * @param data Puntero donde guardar los datos
 * @return HAL_SENSOR_OK si éxito, otro valor en error
 * 
 * @note No bloquea (lectura asíncrona del BME280)
 */
hal_sensor_status_t hal_sensor_read(hal_sensor_data_t* data);

/**
 * @brief Obtener último error del sensor
 * 
 * @return Código de error interno
 */
int32_t hal_sensor_get_last_error(void);

/**
 * @brief Deinicializar el subsistema de sensores
 * 
 * @return HAL_SENSOR_OK si éxito
 */
hal_sensor_status_t hal_sensor_deinit(void);

#endif // __HAL_SENSORS_H__
```

### Paso 2: Implementación para ESP32-C3

Crear archivo: `src/hal_sensors_esp32c3.c`

```c
#include "hal_sensors.h"
#include "IBme280.h"
#include "sensors.h"
#include "system.h"
#include <esp_log.h>
#include <esp_timer.h>

static const char* TAG = "HAL_SENSORS";

// Variables estáticas (ocultas en esta unidad de compilación)
static struct {
    bool initialized;
    SensorData last_data;
    int32_t last_error;
    uint64_t last_read_time_us;
} hal_state = {0};

hal_sensor_status_t hal_sensor_init(const hal_sensor_config_t* config) {
    if (hal_state.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return HAL_SENSOR_OK;
    }
    
    if (config == NULL) {
        return HAL_SENSOR_ERROR;
    }
    
    // Inicializar sistema (bus I2C)
    SystemDev* sysDevs = system_init();
    if (sysDevs == NULL) {
        hal_state.last_error = -1;
        return HAL_SENSOR_ERROR;
    }
    
    // Inicializar sensor
    sensorConfig sensor_cfg = {.bmeDev = sysDevs->bme};
    int8_t result = sensors_init(&sensor_cfg);
    
    if (result != BME280_OK) {
        hal_state.last_error = result;
        ESP_LOGE(TAG, "Sensor init failed: %d", result);
        return HAL_SENSOR_CALIBRATION_ERROR;
    }
    
    hal_state.initialized = true;
    hal_state.last_error = 0;
    ESP_LOGI(TAG, "HAL Sensor initialized successfully");
    
    return HAL_SENSOR_OK;
}

hal_sensor_status_t hal_sensor_read(hal_sensor_data_t* data) {
    if (!hal_state.initialized) {
        return HAL_SENSOR_ERROR;
    }
    
    if (data == NULL) {
        return HAL_SENSOR_ERROR;
    }
    
    // Leer datos del sensor
    SensorData sensor_data = {0};
    sensors_update(&sensor_data);
    
    // Convertir y almacenar
    data->temperature = sensor_data.bme.airTemp;
    data->humidity = sensor_data.bme.humidity;
    data->pressure = sensor_data.bme.pressure;
    data->timestamp = esp_timer_get_time() / 1000; // Convertir a ms
    
    hal_state.last_data = sensor_data;
    hal_state.last_read_time_us = esp_timer_get_time();
    
    return HAL_SENSOR_OK;
}

int32_t hal_sensor_get_last_error(void) {
    return hal_state.last_error;
}

hal_sensor_status_t hal_sensor_deinit(void) {
    hal_state.initialized = false;
    return HAL_SENSOR_OK;
}
```

### Paso 3: Usar la HAL en main.c

```c
#include "hal_sensors.h"
#include "esp_log.h"

static const char* TAG = "MAIN";

void app_main() {
    // Configurar HAL
    hal_sensor_config_t sensor_config = {
        .i2c_speed_hz = 100000,
        .scl_pin = 9,
        .sda_pin = 8,
        .i2c_port = 0,
        .device_addr = 0x76
    };
    
    // Inicializar
    hal_sensor_status_t status = hal_sensor_init(&sensor_config);
    if (status != HAL_SENSOR_OK) {
        ESP_LOGE(TAG, "Failed to init sensor: %d", status);
        return;
    }
    
    // Loop principal
    for (;;) {
        hal_sensor_data_t data;
        status = hal_sensor_read(&data);
        
        if (status == HAL_SENSOR_OK) {
            printf("T: %.2f°C | H: %.2f%% | P: %.2f hPa | TS: %u ms\n",
                   data.temperature,
                   data.humidity,
                   data.pressure,
                   data.timestamp);
        } else {
            ESP_LOGE(TAG, "Read failed: %d (error code: %d)",
                     status, hal_sensor_get_last_error());
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

---

## Patrones de Diseño

### 1. Patrón Strategy (Estrategia de Lectura)

Permite diferentes modos de lectura:

```c
// Definir estrategias
typedef struct {
    hal_sensor_status_t (*init)(const hal_sensor_config_t*);
    hal_sensor_status_t (*read)(hal_sensor_data_t*);
    hal_sensor_status_t (*deinit)(void);
} hal_sensor_ops_t;

// Implementación para ESP32
const hal_sensor_ops_t esp32_ops = {
    .init = hal_sensor_esp32_init,
    .read = hal_sensor_esp32_read,
    .deinit = hal_sensor_esp32_deinit
};

// Implementación para STM32
const hal_sensor_ops_t stm32_ops = {
    .init = hal_sensor_stm32_init,
    .read = hal_sensor_stm32_read,
    .deinit = hal_sensor_stm32_deinit
};
```

### 2. Patrón Adapter (Adaptador)

Convertir una interfaz existente a otra:

```c
/**
 * Adaptador de bme280_dev a hal_sensor_dev
 * Permite usar la estructura BME280 con la interfaz HAL
 */
typedef struct {
    struct bme280_dev bme_device;
    hal_sensor_interface_t interface;
} hal_adapter_t;

hal_adapter_t adapter;

// Inicializar adapter
void adapter_init(const hal_sensor_config_t* config) {
    // Configurar estructura BME280
    adapter.bme_device.intf = BME280_I2C_INTF;
    adapter.bme_device.read = ibme280_i2c_read;
    adapter.bme_device.write = ibme280_i2c_write;
    
    // Asignar a interfaz HAL
    adapter.interface.read = (hal_read_fn)bme280_get_sensor_data;
    adapter.interface.init = (hal_init_fn)bme280_init;
}
```

### 3. Patrón Builder (Constructor)

Construcción flexible de configuraciones:

```c
/**
 * Builder para configuración de sensores
 * Ejemplo: fluent interface
 */
hal_sensor_config_builder_t* hal_sensor_config_new(void) {
    hal_sensor_config_builder_t* builder = malloc(sizeof(*builder));
    // Valores por defecto
    builder->speed = 100000;
    builder->scl_pin = 9;
    builder->sda_pin = 8;
    return builder;
}

hal_sensor_config_builder_t* with_speed(hal_sensor_config_builder_t* b, uint32_t speed) {
    b->speed = speed;
    return b; // Retornar para encadenamiento
}

hal_sensor_config_builder_t* with_pins(hal_sensor_config_builder_t* b, uint8_t scl, uint8_t sda) {
    b->scl_pin = scl;
    b->sda_pin = sda;
    return b;
}

hal_sensor_config_t build(hal_sensor_config_builder_t* b) {
    hal_sensor_config_t config = {
        .i2c_speed_hz = b->speed,
        .scl_pin = b->scl_pin,
        .sda_pin = b->sda_pin
    };
    free(b);
    return config;
}

// Uso
hal_sensor_config_t cfg = build(
    with_pins(
        with_speed(
            hal_sensor_config_new(),
            400000
        ),
        10, 11
    )
);
```

---

## Mejores Prácticas

### 1. **Manejo de Errores Robusto**

```c
/**
 * NO HACER:
 */
int8_t bad_init() {
    bme280_init(&dev);  // Ignorar retorno
    bme280_set_sensor_settings(...);  // Sin validar
    return 0;
}

/**
 * HACER:
 */
int8_t good_init() {
    int8_t ret = bme280_init(&dev);
    if (ret != BME280_OK) {
        ESP_LOGE(TAG, "Init failed: %d", ret);
        return ret;
    }
    
    ret = bme280_set_sensor_settings(...);
    if (ret != BME280_OK) {
        ESP_LOGE(TAG, "Settings failed: %d", ret);
        return ret;
    }
    
    return BME280_OK;
}
```

### 2. **Validación de Parámetros**

```c
/**
 * Validar entrada temprano y devolver errores descriptivos
 */
hal_sensor_status_t hal_sensor_init(const hal_sensor_config_t* config) {
    // Validar puntero NULL
    if (config == NULL) {
        ESP_LOGE(TAG, "Config cannot be NULL");
        return HAL_SENSOR_ERROR;
    }
    
    // Validar rangos válidos
    if (config->i2c_speed_hz < 10000 || config->i2c_speed_hz > 1000000) {
        ESP_LOGE(TAG, "Invalid I2C speed: %u Hz", config->i2c_speed_hz);
        return HAL_SENSOR_ERROR;
    }
    
    // Validar pines válidos
    if (config->scl_pin >= GPIO_NUM_MAX || config->sda_pin >= GPIO_NUM_MAX) {
        ESP_LOGE(TAG, "Invalid GPIO pins");
        return HAL_SENSOR_ERROR;
    }
    
    // Validar que SCL ≠ SDA
    if (config->scl_pin == config->sda_pin) {
        ESP_LOGE(TAG, "SCL and SDA cannot be the same pin");
        return HAL_SENSOR_ERROR;
    }
    
    // Continuar con inicialización...
}
```

### 3. **Logging Estratégico**

```c
/**
 * NIVEL CRÍTICO: Errores que detienen ejecución
 */
if (bme280_init(&dev) != BME280_OK) {
    ESP_LOGE(TAG, "CRITICAL: Sensor not responding");
    return;
}

/**
 * NIVEL ADVERTENCIA: Problemas pero continúa
 */
if (hal_state.last_read_time_us > 5000000) {
    ESP_LOGW(TAG, "WARNING: Sensor read timeout");
}

/**
 * NIVEL INFO: Hitos importantes
 */
ESP_LOGI(TAG, "Sensor initialized: temp=%0.2f°C, id=0x%02x", 
         data.temperature, chip_id);

/**
 * NIVEL DEBUG: Información detallada
 */
ESP_LOGD(TAG, "DEBUG: I2C transmitted %d bytes", bytes_sent);
```

### 4. **Gestión de Recursos**

```c
/**
 * Patrón de inicialización/finalización
 */
typedef struct {
    bool i2c_initialized;
    bool sensor_initialized;
    i2c_master_bus_handle_t i2c_handle;
} hal_resources_t;

hal_resources_t resources = {0};

hal_sensor_status_t hal_sensor_init(const hal_sensor_config_t* config) {
    // Inicializar I2C
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &resources.i2c_handle));
    resources.i2c_initialized = true;
    
    // Inicializar sensor
    if (bme280_init(&dev) != BME280_OK) {
        // Rollback: limpiar recursos asignados
        i2c_del_master_bus(resources.i2c_handle);
        resources.i2c_initialized = false;
        return HAL_SENSOR_ERROR;
    }
    resources.sensor_initialized = true;
    
    return HAL_SENSOR_OK;
}

hal_sensor_status_t hal_sensor_deinit(void) {
    // Finalizar en orden inverso a inicialización
    if (resources.sensor_initialized) {
        bme280_soft_reset(&dev);
        resources.sensor_initialized = false;
    }
    
    if (resources.i2c_initialized) {
        i2c_del_master_bus(resources.i2c_handle);
        resources.i2c_initialized = false;
    }
    
    return HAL_SENSOR_OK;
}
```

### 5. **Testing y Mocks**

```c
/**
 * Interfaz que permite inyección de dependencias
 */
typedef struct {
    hal_sensor_status_t (*i2c_read)(uint8_t, uint8_t*, uint32_t, void*);
    hal_sensor_status_t (*i2c_write)(uint8_t, const uint8_t*, uint32_t, void*);
    void (*delay_us)(uint32_t, void*);
} hal_i2c_ops_t;

// Implementación real
const hal_i2c_ops_t real_i2c_ops = {
    .i2c_read = ibme280_i2c_read,
    .i2c_write = ibme280_i2c_write,
    .delay_us = ibme280_delay_us
};

// Mock para testing
const hal_i2c_ops_t mock_i2c_ops = {
    .i2c_read = mock_i2c_read,    // Simula datos
    .i2c_write = mock_i2c_write,  // Registra escrituras
    .delay_us = mock_delay_us     // No-op
};

// Test unitario
void test_sensor_read() {
    // Usar mock
    hal_set_i2c_ops(&mock_i2c_ops);
    
    hal_sensor_config_t cfg = {...};
    hal_sensor_init(&cfg);
    
    hal_sensor_data_t data;
    hal_sensor_read(&data);
    
    // Verificar
    assert(data.temperature == MOCK_TEMPERATURE);
}
```

---

## Troubleshooting Avanzado

### Problema 1: Sensor No Responde (0x76 no encontrado)

**Síntomas**:
```
[SCANNER] I2C scanning...
[SCANNER] No devices found
```

**Checklist de Diagnóstico**:

```c
// 1. Verificar conexión física
// - ¿Están SCL (GPIO9) y SDA (GPIO8) conectados correctamente?
// - ¿Hay pull-ups de 4.7kΩ en ambas líneas?
// - ¿Está el sensor con power (3.3V) y GND conectados?

// 2. Verificar dirección I2C
uint8_t addr = 0x76;  // Dirección primaria
// uint8_t addr = 0x77;  // Dirección secundaria (si se cambió SDIO)

// 3. Debug: Medir voltajes
ESP_LOGI(TAG, "Escaneando todo el rango de direcciones...");
for (uint8_t i = 0; i < 127; i++) {
    esp_err_t ret = i2c_master_probe(handle, i, 100);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Dispositivo encontrado en 0x%02X", i);
    }
}

// 4. Verificar clock
// Si está a 100kHz y aún no funciona:
// - Intentar con 50kHz (más estable pero lento)
// - O acelerar a 400kHz (si hay buena conexión)

// 5. Verificar glitch_ignore_cnt
// Si hay mucho ruido:
busConfig.glitch_ignore_cnt = 10;  // Aumentar filtro
```

**Solución Típica**:

```c
// Problema común: Pull-ups débiles o faltantes
// Síntoma: Sensor responde esporádicamente

// Solución: Reducir velocidad
busConfig.scl_speed_hz = 50000;  // De 100k a 50k

// O aumentar timeout
esp_err_t ret = i2c_master_probe(handle, 0x76, 500);  // De 50ms a 500ms
```

### Problema 2: Lecturas Inconsistentes

**Síntomas**:
```
Data: 25.50, 45.32, 1023.45
Data: 0.00, 0.00, 0.00      ← Valores inválidos
Data: 26.12, 44.98, 1023.22
```

**Causa Raíz**: Mode transición del sensor

```
Ciclo de medición del BME280:
┌──────────────────────────────────────────┐
│ Modo NORMAL (continuo)                   │
├──────────────────────────────────────────┤
│ [Medición] → [Espera] → [Medición] → ... │
│    ↑                                     │
│   100-200ms                              │
└──────────────────────────────────────────┘
```

**Solución**:

```c
// Esperar a que la medición esté lista
void sensors_update_safe(SensorData* data) {
    uint8_t status;
    int max_retries = 10;
    
    // Esperar a que la medición esté completa
    do {
        bme280_get_regs(BME280_REG_STATUS, &status, 1, &dev);
        if (!(status & BME280_STATUS_MEAS_DONE)) {
            break;  // Medición completa
        }
        vTaskDelay(pdMS_TO_TICKS(20));  // Esperar 20ms
    } while (max_retries-- > 0);
    
    if (max_retries <= 0) {
        ESP_LOGW(TAG, "Measurement timeout");
        return;
    }
    
    // Ahora leer datos seguros
    bme280_get_sensor_data(BME280_ALL, &bmeData, &dev);
    // ...
}
```

### Problema 3: Memoria Insuficiente

**Síntomas**:
```
E (xxx) esp_image: Image too large
E (xxx) heap_caps: Allocate failed
```

**Optimización**:

```c
// 1. Reducir tamaño de buffers
// ANTES:
uint8_t buffer[256];  // Innecesariamente grande

// DESPUÉS:
uint8_t buffer[64];   // BME280 necesita máximo 32 bytes

// 2. Usar stack en lugar de heap
// ANTES:
uint8_t* data = malloc(32);  // Asignación dinámica
// ...
free(data);

// DESPUÉS:
uint8_t data[32];  // Stack (más rápido, predecible)

// 3. Eliminar logging redundante
#ifndef NDEBUG
ESP_LOGD(TAG, "Debug info: %d", value);
#endif

// 4. Usar PSRAM si disponible
esp_spiram_is_initialized() ? ESP_SPIRAM_SIZE : ESP_INTERNAL_RAM_SIZE;
```

### Problema 4: I2C Deadlock

**Síntomas**:
```
[Sistema se cuelga indefinidamente]
[watchdog timeout]
```

**Causa**: Sensor en estado inconsistente

```c
/**
 * Solución: I2C Bus Recovery (IEEE 1149.1)
 */
void i2c_bus_recovery(i2c_master_bus_handle_t handle) {
    // Procedimiento de recuperación:
    // 1. Generar 9 pulsos en SCL mientras SDA está bajo
    // 2. Generar START condition
    // 3. Generar STOP condition
    
    ESP_LOGW(TAG, "I2C Bus Recovery initiated");
    
    // Nota: ESP-IDF puede no exponer esta función
    // Alternativa: reset del bus
    i2c_del_master_bus(handle);
    
    // Reinicializar
    ESP_ERROR_CHECK(i2c_new_master_bus(&busConfig, &handle));
    ESP_LOGI(TAG, "I2C Bus reset complete");
}
```

---

## Referencias Técnicas

### Documentación Oficial

- [ESP-IDF I2C Master Driver](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2c.html)
- [BME280 Datasheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf)
- [I2C Bus Specification](https://www.i2c-bus.org/)

### Fórmulas y Cálculos

#### Resistencias Pull-up
```
Rp = (Vcc - Vol) / Iol

Donde:
- Vcc = 3.3V
- Vol = 0.4V (típico)
- Iol = 3mA (típico)

Rp = (3.3 - 0.4) / 0.003 = 966 Ω

Recomendación comercial: 4.7kΩ (más estable, menos consumo)
```

#### Tiempo de Subida SCL
```
tr = 1000 * Cb * Rp / (Vdd - Vol)

Donde:
- Cb = Capacitancia del bus (~100pF)
- Rp = Resistencia pull-up
- tr < 1000ns recomendado

Con 4.7kΩ: tr ≈ 470ns ✓
```

### Glosario de Términos

| Término | Significado |
|---------|-----------|
| **SCL** | Serial Clock Line (línea de reloj) |
| **SDA** | Serial Data Line (línea de datos) |
| **Master** | Dispositivo que controla el bus (ESP32) |
| **Slave** | Dispositivo controlado (BME280) |
| **HAL** | Hardware Abstraction Layer |
| **Oversampling** | Múltiples mediciones para reducir ruido |
| **Glitch** | Pulso espurio corto en la línea |
| **Repeated START** | START sin STOP previo |
| **Open Drain** | Salida que solo puede tirar a GND |
| **Pull-up** | Resistencia que tira la línea a VCC |

---

## Conclusión

Este documento proporciona una base sólida para entender y extender el sistema de sensores del MacetoHuerto. La arquitectura propuesta es:

✅ **Modular**: Cada componente tiene una responsabilidad clara
✅ **Portable**: Fácil de adaptar a otros microcontroladores
✅ **Mantenible**: Cambios localizados no afectan otros módulos
✅ **Testeable**: Interfaces claras permiten mocking
✅ **Escalable**: Agregar sensores es directo

Para consultas o mejoras, contactar al equipo de desarrollo.

---

**Versión**: 1.0
**Última actualización**: 3 de Mayo de 2026
**Autor**: Equipo de Desarrollo MacetoHuerto
