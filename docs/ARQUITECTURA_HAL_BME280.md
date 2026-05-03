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

**Concepto**: Permite cambiar el comportamiento de lectura en tiempo de ejecución sin cambiar el código de la aplicación.

**Caso de Uso**: Tu ESP32 puede usar I2C, pero podrías querer cambiar a SPI o UART sin recompilar la aplicación.

#### Paso 1: Definir la Interfaz de Estrategias

Archivo: `include/hal_sensor_strategy.h`

```c
#ifndef __HAL_SENSOR_STRATEGY_H__
#define __HAL_SENSOR_STRATEGY_H__

#include "hal_sensors.h"

/**
 * @brief Estructura que define las operaciones que puede hacer cualquier estrategia
 */
typedef struct {
    const char* name;                                              // Nombre de la estrategia
    hal_sensor_status_t (*init)(const hal_sensor_config_t* cfg);  // Inicializar
    hal_sensor_status_t (*read)(hal_sensor_data_t* data);         // Leer datos
    hal_sensor_status_t (*deinit)(void);                          // Finalizar
} hal_sensor_strategy_t;

// Declarar estrategias disponibles
extern const hal_sensor_strategy_t sensor_strategy_i2c;   // Estrategia I2C (actual)
extern const hal_sensor_strategy_t sensor_strategy_spi;   // Estrategia SPI (futura)
extern const hal_sensor_strategy_t sensor_strategy_uart;  // Estrategia UART (futura)

/**
 * @brief Contexto que usa una estrategia
 */
typedef struct {
    const hal_sensor_strategy_t* strategy;  // Estrategia actual
    void* private_data;                      // Datos privados de la estrategia
} hal_sensor_context_t;

// Inicializar contexto con una estrategia
hal_sensor_context_t* hal_sensor_context_create(const hal_sensor_strategy_t* strategy);

// Ejecutar operación con la estrategia actual
hal_sensor_status_t hal_sensor_context_init(hal_sensor_context_t* ctx, const hal_sensor_config_t* cfg);
hal_sensor_status_t hal_sensor_context_read(hal_sensor_context_t* ctx, hal_sensor_data_t* data);
hal_sensor_status_t hal_sensor_context_deinit(hal_sensor_context_t* ctx);

// Cambiar estrategia en tiempo de ejecución
void hal_sensor_context_set_strategy(hal_sensor_context_t* ctx, const hal_sensor_strategy_t* strategy);

#endif
```

#### Paso 2: Implementar Estrategia I2C

Archivo: `src/hal_sensor_strategy_i2c.c`

```c
#include "hal_sensor_strategy.h"
#include "sensors.h"
#include "system.h"
#include <esp_log.h>

static const char* TAG = "STRATEGY_I2C";

/**
 * @brief Datos privados de la estrategia I2C
 */
typedef struct {
    SystemDev* sys_devs;
    bool initialized;
} i2c_strategy_data_t;

// Variable estática para almacenar datos de la estrategia
static i2c_strategy_data_t i2c_data = {0};

// Funciones de la estrategia I2C
static hal_sensor_status_t i2c_init(const hal_sensor_config_t* cfg) {
    ESP_LOGI(TAG, "Initializing I2C strategy");
    
    if (i2c_data.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return HAL_SENSOR_OK;
    }
    
    // Inicializar sistema I2C
    i2c_data.sys_devs = system_init();
    if (i2c_data.sys_devs == NULL) {
        ESP_LOGE(TAG, "System init failed");
        return HAL_SENSOR_ERROR;
    }
    
    // Inicializar sensor
    sensorConfig sensor_cfg = {.bmeDev = i2c_data.sys_devs->bme};
    int8_t result = sensors_init(&sensor_cfg);
    
    if (result != BME280_OK) {
        ESP_LOGE(TAG, "Sensor init failed: %d", result);
        return HAL_SENSOR_CALIBRATION_ERROR;
    }
    
    i2c_data.initialized = true;
    ESP_LOGI(TAG, "I2C strategy initialized successfully");
    return HAL_SENSOR_OK;
}

static hal_sensor_status_t i2c_read(hal_sensor_data_t* data) {
    if (!i2c_data.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return HAL_SENSOR_ERROR;
    }
    
    if (data == NULL) {
        return HAL_SENSOR_ERROR;
    }
    
    SensorData sensor_data = {0};
    sensors_update(&sensor_data);
    
    data->temperature = sensor_data.bme.airTemp;
    data->humidity = sensor_data.bme.humidity;
    data->pressure = sensor_data.bme.pressure;
    data->timestamp = esp_timer_get_time() / 1000;
    
    return HAL_SENSOR_OK;
}

static hal_sensor_status_t i2c_deinit(void) {
    i2c_data.initialized = false;
    ESP_LOGI(TAG, "I2C strategy deinitialized");
    return HAL_SENSOR_OK;
}

// Definir la estrategia I2C
const hal_sensor_strategy_t sensor_strategy_i2c = {
    .name = "I2C",
    .init = i2c_init,
    .read = i2c_read,
    .deinit = i2c_deinit
};
```

#### Paso 3: Implementar Estrategia SPI (Futura)

Archivo: `src/hal_sensor_strategy_spi.c`

```c
#include "hal_sensor_strategy.h"
#include <esp_log.h>

static const char* TAG = "STRATEGY_SPI";

typedef struct {
    // Datos específicos de SPI
    spi_device_handle_t spi_handle;
    bool initialized;
} spi_strategy_data_t;

static spi_strategy_data_t spi_data = {0};

static hal_sensor_status_t spi_init(const hal_sensor_config_t* cfg) {
    ESP_LOGI(TAG, "Initializing SPI strategy");
    
    // TODO: Implementar inicialización SPI
    // - Configurar SPI bus
    // - Registrar dispositivo BME280 en SPI
    // - Inicializar sensor con interfaz SPI
    
    spi_data.initialized = true;
    return HAL_SENSOR_OK;
}

static hal_sensor_status_t spi_read(hal_sensor_data_t* data) {
    if (!spi_data.initialized) {
        return HAL_SENSOR_ERROR;
    }
    
    // TODO: Leer datos por SPI
    // El código sería similar a I2C pero usando primitivas SPI
    
    return HAL_SENSOR_OK;
}

static hal_sensor_status_t spi_deinit(void) {
    spi_data.initialized = false;
    ESP_LOGI(TAG, "SPI strategy deinitialized");
    return HAL_SENSOR_OK;
}

const hal_sensor_strategy_t sensor_strategy_spi = {
    .name = "SPI",
    .init = spi_init,
    .read = spi_read,
    .deinit = spi_deinit
};
```

#### Paso 4: Implementar el Contexto

Archivo: `src/hal_sensor_context.c`

```c
#include "hal_sensor_strategy.h"
#include <stdlib.h>
#include <esp_log.h>

static const char* TAG = "SENSOR_CONTEXT";

hal_sensor_context_t* hal_sensor_context_create(const hal_sensor_strategy_t* strategy) {
    if (strategy == NULL) {
        ESP_LOGE(TAG, "Strategy cannot be NULL");
        return NULL;
    }
    
    hal_sensor_context_t* ctx = malloc(sizeof(hal_sensor_context_t));
    if (ctx == NULL) {
        ESP_LOGE(TAG, "Failed to allocate context");
        return NULL;
    }
    
    ctx->strategy = strategy;
    ctx->private_data = NULL;
    
    ESP_LOGI(TAG, "Context created with strategy: %s", strategy->name);
    return ctx;
}

hal_sensor_status_t hal_sensor_context_init(hal_sensor_context_t* ctx, const hal_sensor_config_t* cfg) {
    if (ctx == NULL || ctx->strategy == NULL) {
        return HAL_SENSOR_ERROR;
    }
    
    ESP_LOGI(TAG, "Initializing with strategy: %s", ctx->strategy->name);
    return ctx->strategy->init(cfg);
}

hal_sensor_status_t hal_sensor_context_read(hal_sensor_context_t* ctx, hal_sensor_data_t* data) {
    if (ctx == NULL || ctx->strategy == NULL) {
        return HAL_SENSOR_ERROR;
    }
    
    return ctx->strategy->read(data);
}

hal_sensor_status_t hal_sensor_context_deinit(hal_sensor_context_t* ctx) {
    if (ctx == NULL || ctx->strategy == NULL) {
        return HAL_SENSOR_ERROR;
    }
    
    hal_sensor_status_t status = ctx->strategy->deinit();
    free(ctx);
    return status;
}

void hal_sensor_context_set_strategy(hal_sensor_context_t* ctx, const hal_sensor_strategy_t* strategy) {
    if (ctx != NULL && strategy != NULL) {
        ctx->strategy = strategy;
        ESP_LOGI(TAG, "Strategy changed to: %s", strategy->name);
    }
}
```

#### Paso 5: Usar Strategy Pattern en main.c

```c
#include "hal_sensor_strategy.h"
#include "esp_log.h"

static const char* TAG = "MAIN";

void app_main() {
    hal_sensor_config_t sensor_config = {
        .i2c_speed_hz = 100000,
        .scl_pin = 9,
        .sda_pin = 8,
        .i2c_port = 0,
        .device_addr = 0x76
    };
    
    // Crear contexto con estrategia I2C
    hal_sensor_context_t* sensor_ctx = hal_sensor_context_create(&sensor_strategy_i2c);
    if (sensor_ctx == NULL) {
        ESP_LOGE(TAG, "Failed to create sensor context");
        return;
    }
    
    // Inicializar con la estrategia
    hal_sensor_status_t status = hal_sensor_context_init(sensor_ctx, &sensor_config);
    if (status != HAL_SENSOR_OK) {
        ESP_LOGE(TAG, "Failed to init sensor: %d", status);
        return;
    }
    
    // Loop principal
    for (int i = 0; i < 10; i++) {
        hal_sensor_data_t data;
        status = hal_sensor_context_read(sensor_ctx, &data);
        
        if (status == HAL_SENSOR_OK) {
            printf("[%s] T: %.2f°C | H: %.2f%% | P: %.2f hPa\n",
                   sensor_ctx->strategy->name,
                   data.temperature,
                   data.humidity,
                   data.pressure);
        } else {
            ESP_LOGE(TAG, "Read failed with strategy %s", sensor_ctx->strategy->name);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Cambiar a SPI después de 5 lecturas (ejemplo)
        if (i == 5) {
            ESP_LOGI(TAG, "Switching to SPI strategy...");
            // Primero deinicializar I2C
            hal_sensor_context_deinit(sensor_ctx);
            
            // Crear nuevo contexto con SPI
            sensor_ctx = hal_sensor_context_create(&sensor_strategy_spi);
            hal_sensor_context_init(sensor_ctx, &sensor_config);
        }
    }
    
    hal_sensor_context_deinit(sensor_ctx);
}
```

**Ventajas del Strategy Pattern**:
- ✅ Cambiar protocolos SIN recompilar
- ✅ Testear cada estrategia independientemente
- ✅ Agregar nuevas estrategias fácilmente
- ✅ Encapsulación de detalles específicos de cada protocolo

### 2. Patrón Adapter (Adaptador) - Ejemplo Completo Funcional

**Concepto**: Adaptar interfaces de diferentes sensores a una interfaz común, permitiendo intercambiarlos fácilmente.

**Caso de Uso Real**: Tienes BME280 (presión, temperatura, humedad) pero quieres poder usar BMP180 (solo presión y temperatura) como alternativa sin cambiar el código de la aplicación.

#### Paso 1: Definir Interfaz Genérica de Sensores

Archivo: `include/hal_generic_sensor.h`

```c
#ifndef __HAL_GENERIC_SENSOR_H__
#define __HAL_GENERIC_SENSOR_H__

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Estructura genérica de datos de sensor
 * Compatible con BME280, BMP180, DHT22, etc.
 */
typedef struct {
    float temperature;      // Temperatura en °C (todos los sensores)
    float humidity;         // Humedad % (solo algunos sensores)
    float pressure;         // Presión en hPa (solo algunos sensores)
    bool has_humidity;      // Flag: ¿este sensor tiene humedad?
    bool has_pressure;      // Flag: ¿este sensor tiene presión?
} generic_sensor_data_t;

/**
 * @brief Interfaz que todo sensor debe cumplir
 */
typedef struct {
    const char* sensor_name;                                          // "BME280", "BMP180", etc
    int32_t (*init)(void* config);                                    // Inicializar
    int32_t (*read)(generic_sensor_data_t* data);                     // Leer datos
    int32_t (*deinit)(void);                                          // Finalizar
    void (*print_info)(void);                                         // Imprimir información del sensor
} generic_sensor_interface_t;

#endif
```

#### Paso 2: Implementar Adaptador para BME280

Archivo: `src/hal_adapter_bme280.c`

```c
#include "hal_generic_sensor.h"
#include "sensors.h"
#include "system.h"
#include <esp_log.h>
#include <string.h>

static const char* TAG = "ADAPTER_BME280";

typedef struct {
    SystemDev* sys_devs;
    bool initialized;
} bme280_adapter_data_t;

static bme280_adapter_data_t bme280_adapter = {0};

/**
 * @brief Inicializar adaptador BME280
 */
static int32_t adapter_bme280_init(void* config) {
    ESP_LOGI(TAG, "Initializing BME280 adapter");
    
    if (bme280_adapter.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return 0;
    }
    
    // Inicializar sistema
    bme280_adapter.sys_devs = system_init();
    if (bme280_adapter.sys_devs == NULL) {
        ESP_LOGE(TAG, "System init failed");
        return -1;
    }
    
    // Inicializar sensor BME280
    sensorConfig sensor_cfg = {.bmeDev = bme280_adapter.sys_devs->bme};
    int8_t result = sensors_init(&sensor_cfg);
    
    if (result != BME280_OK) {
        ESP_LOGE(TAG, "BME280 init failed: %d", result);
        return -1;
    }
    
    bme280_adapter.initialized = true;
    ESP_LOGI(TAG, "BME280 adapter initialized");
    return 0;
}

/**
 * @brief Leer datos del BME280 y convertir a formato genérico
 */
static int32_t adapter_bme280_read(generic_sensor_data_t* data) {
    if (!bme280_adapter.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return -1;
    }
    
    if (data == NULL) {
        return -1;
    }
    
    SensorData bme_data = {0};
    sensors_update(&bme_data);
    
    // Convertir datos BME280 al formato genérico
    data->temperature = bme_data.bme.airTemp;
    data->humidity = bme_data.bme.humidity;        // BME280 tiene humedad
    data->pressure = bme_data.bme.pressure;
    data->has_humidity = true;                      // BME280 SÍ tiene humedad
    data->has_pressure = true;                      // BME280 SÍ tiene presión
    
    ESP_LOGD(TAG, "BME280 read: T=%.2f°C, H=%.2f%%, P=%.2f hPa",
             data->temperature, data->humidity, data->pressure);
    
    return 0;
}

static int32_t adapter_bme280_deinit(void) {
    bme280_adapter.initialized = false;
    ESP_LOGI(TAG, "BME280 adapter deinitialized");
    return 0;
}

static void adapter_bme280_print_info(void) {
    printf("╔════════════════════════════════════╗\n");
    printf("║  SENSOR: Bosch BME280              ║\n");
    printf("╠════════════════════════════════════╣\n");
    printf("║  Interfaz: I2C                     ║\n");
    printf("║  Dirección: 0x76 o 0x77            ║\n");
    printf("║  Parámetros:                       ║\n");
    printf("║    ✓ Temperatura: -40 a +85°C      ║\n");
    printf("║    ✓ Humedad: 0 a 100%%             ║\n");
    printf("║    ✓ Presión: 300 a 1100 hPa       ║\n");
    printf("║  Precisión:                        ║\n");
    printf("║    ± 0.5°C (temperatura)           ║\n");
    printf("║    ± 3%% (humedad)                  ║\n");
    printf("║    ± 1 hPa (presión)               ║\n");
    printf("╚════════════════════════════════════╝\n");
}

// Interfaz pública del adaptador BME280
const generic_sensor_interface_t bme280_sensor_interface = {
    .sensor_name = "BME280",
    .init = adapter_bme280_init,
    .read = adapter_bme280_read,
    .deinit = adapter_bme280_deinit,
    .print_info = adapter_bme280_print_info
};
```

#### Paso 3: Implementar Adaptador para BMP180 (Alternativa)

Archivo: `src/hal_adapter_bmp180.c`

```c
#include "hal_generic_sensor.h"
#include <esp_log.h>

static const char* TAG = "ADAPTER_BMP180";

typedef struct {
    // Datos específicos del BMP180 (simulados para ejemplo)
    bool initialized;
    float last_temperature;
    float last_pressure;
} bmp180_adapter_data_t;

static bmp180_adapter_data_t bmp180_adapter = {0};

/**
 * @brief Inicializar BMP180 (simulado)
 */
static int32_t adapter_bmp180_init(void* config) {
    ESP_LOGI(TAG, "Initializing BMP180 adapter");
    
    // TODO: Inicializar BMP180 real
    // - Configurar I2C a dirección 0x77
    // - Leer calibración data
    // - Verificar chip ID
    
    bmp180_adapter.initialized = true;
    ESP_LOGI(TAG, "BMP180 adapter initialized");
    return 0;
}

/**
 * @brief Leer BMP180 y convertir a formato genérico
 * NOTA: BMP180 NO tiene humedad, solo T y P
 */
static int32_t adapter_bmp180_read(generic_sensor_data_t* data) {
    if (!bmp180_adapter.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return -1;
    }
    
    if (data == NULL) {
        return -1;
    }
    
    // TODO: Leer datos reales del BMP180
    // Simulación por ahora:
    data->temperature = 24.5f;
    data->humidity = 0.0f;                  // BMP180 NO tiene humedad
    data->pressure = 1013.25f;
    data->has_humidity = false;             // BMP180 NO TIENE humedad
    data->has_pressure = true;              // BMP180 SÍ tiene presión
    
    ESP_LOGD(TAG, "BMP180 read: T=%.2f°C, P=%.2f hPa (sin humedad)",
             data->temperature, data->pressure);
    
    return 0;
}

static int32_t adapter_bmp180_deinit(void) {
    bmp180_adapter.initialized = false;
    ESP_LOGI(TAG, "BMP180 adapter deinitialized");
    return 0;
}

static void adapter_bmp180_print_info(void) {
    printf("╔════════════════════════════════════╗\n");
    printf("║  SENSOR: Bosch BMP180              ║\n");
    printf("╠════════════════════════════════════╣\n");
    printf("║  Interfaz: I2C                     ║\n");
    printf("║  Dirección: 0x77                   ║\n");
    printf("║  Parámetros:                       ║\n");
    printf("║    ✓ Temperatura: -40 a +85°C      ║\n");
    printf("║    ✗ Humedad: NO DISPONIBLE        ║\n");
    printf("║    ✓ Presión: 300 a 1100 hPa       ║\n");
    printf("║  Precisión:                        ║\n");
    printf("║    ± 2°C (temperatura)             ║\n");
    printf("║    ± 1 hPa (presión)               ║\n");
    printf("╚════════════════════════════════════╝\n");
}

// Interfaz pública del adaptador BMP180
const generic_sensor_interface_t bmp180_sensor_interface = {
    .sensor_name = "BMP180",
    .init = adapter_bmp180_init,
    .read = adapter_bmp180_read,
    .deinit = adapter_bmp180_deinit,
    .print_info = adapter_bmp180_print_info
};
```

#### Paso 4: Crear Gestor Genérico de Sensores

Archivo: `src/hal_sensor_manager.c`

```c
#include "hal_generic_sensor.h"
#include <esp_log.h>
#include <stdlib.h>

static const char* TAG = "SENSOR_MANAGER";

typedef struct {
    const generic_sensor_interface_t* interface;
    bool initialized;
    int read_count;
} sensor_manager_t;

static sensor_manager_t manager = {0};

/**
 * @brief Registrar un sensor (cambiar de uno a otro)
 */
int32_t hal_sensor_manager_register(const generic_sensor_interface_t* sensor_interface) {
    if (sensor_interface == NULL) {
        ESP_LOGE(TAG, "Sensor interface cannot be NULL");
        return -1;
    }
    
    if (manager.initialized) {
        ESP_LOGW(TAG, "Manager already has a sensor, deinitializing...");
        hal_sensor_manager_deinit();
    }
    
    manager.interface = sensor_interface;
    ESP_LOGI(TAG, "Sensor registered: %s", sensor_interface->sensor_name);
    
    return 0;
}

/**
 * @brief Inicializar el sensor registrado
 */
int32_t hal_sensor_manager_init(void* config) {
    if (manager.interface == NULL) {
        ESP_LOGE(TAG, "No sensor registered");
        return -1;
    }
    
    int32_t ret = manager.interface->init(config);
    if (ret == 0) {
        manager.initialized = true;
        manager.read_count = 0;
        ESP_LOGI(TAG, "Sensor '%s' initialized successfully", manager.interface->sensor_name);
        
        // Imprimir información del sensor
        manager.interface->print_info();
    }
    
    return ret;
}

/**
 * @brief Leer datos del sensor
 */
int32_t hal_sensor_manager_read(generic_sensor_data_t* data) {
    if (manager.interface == NULL || !manager.initialized) {
        ESP_LOGE(TAG, "Sensor not initialized");
        return -1;
    }
    
    int32_t ret = manager.interface->read(data);
    if (ret == 0) {
        manager.read_count++;
    }
    
    return ret;
}

/**
 * @brief Obtener información del sensor actual
 */
const char* hal_sensor_manager_get_name(void) {
    if (manager.interface == NULL) {
        return "None";
    }
    return manager.interface->sensor_name;
}

int hal_sensor_manager_get_read_count(void) {
    return manager.read_count;
}

/**
 * @brief Finalizar el sensor
 */
int32_t hal_sensor_manager_deinit(void) {
    if (manager.interface == NULL) {
        return 0;
    }
    
    int32_t ret = manager.interface->deinit();
    manager.initialized = false;
    ESP_LOGI(TAG, "Sensor '%s' deinitialized", manager.interface->sensor_name);
    
    return ret;
}
```

#### Paso 5: Usar el Patrón Adapter en main.c

```c
#include "hal_generic_sensor.h"
#include "esp_log.h"

static const char* TAG = "MAIN";

// Declarar interfaces externas
extern const generic_sensor_interface_t bme280_sensor_interface;
extern const generic_sensor_interface_t bmp180_sensor_interface;

void print_sensor_data(const generic_sensor_data_t* data) {
    printf("\n╔══════════════════════════════════════════╗\n");
    printf("║ Lectura #%d - Sensor: %s\n", 
           hal_sensor_manager_get_read_count(),
           hal_sensor_manager_get_name());
    printf("╠══════════════════════════════════════════╣\n");
    printf("║ Temperatura:  %7.2f °C\n", data->temperature);
    
    if (data->has_humidity) {
        printf("║ Humedad:      %7.2f %%\n", data->humidity);
    } else {
        printf("║ Humedad:      NO DISPONIBLE\n");
    }
    
    if (data->has_pressure) {
        printf("║ Presión:      %7.2f hPa\n", data->pressure);
    } else {
        printf("║ Presión:      NO DISPONIBLE\n");
    }
    
    printf("╚══════════════════════════════════════════╝\n");
}

void app_main() {
    // Seleccionar sensor (podría venir de configuración, EEPROM, etc.)
    const generic_sensor_interface_t* selected_sensor = &bme280_sensor_interface;
    
    // También podrías hacer:
    // const generic_sensor_interface_t* selected_sensor = &bmp180_sensor_interface;
    
    // Registrar el sensor
    if (hal_sensor_manager_register(selected_sensor) != 0) {
        ESP_LOGE(TAG, "Failed to register sensor");
        return;
    }
    
    // Inicializar el sensor
    if (hal_sensor_manager_init(NULL) != 0) {
        ESP_LOGE(TAG, "Failed to init sensor");
        return;
    }
    
    // Loop principal
    for (int i = 0; i < 10; i++) {
        generic_sensor_data_t data = {0};
        
        if (hal_sensor_manager_read(&data) == 0) {
            print_sensor_data(&data);
        } else {
            ESP_LOGE(TAG, "Failed to read sensor");
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        // Ejemplo: Cambiar de sensor en tiempo de ejecución
        if (i == 5) {
            ESP_LOGI(TAG, "Switching from %s to %s",
                     hal_sensor_manager_get_name(),
                     bmp180_sensor_interface.sensor_name);
            
            // Deinicializar sensor actual
            hal_sensor_manager_deinit();
            
            // Cambiar a otro sensor
            hal_sensor_manager_register(&bmp180_sensor_interface);
            hal_sensor_manager_init(NULL);
        }
    }
    
    hal_sensor_manager_deinit();
}
```

#### Ventajas del Patrón Adapter Completo

```
ANTES (acoplado):
┌─────────────┐
│   main.c    │
└──────┬──────┘
       │
       ├─────→ sensors_update()  (BME280 hardcoded)
       ├─────→ system_init()
       └─────→ Lógica específica BME280

DESPUÉS (adaptado):
┌─────────────┐
│   main.c    │
└──────┬──────┘
       │
       └─────→ hal_sensor_manager_read()
              (interfaz genérica)
              
              ↙─────────────────────────────────────┴──────────────╲
    ┌────────────────────┐                          ┌────────────────────┐
    │ adapter_bme280.c   │                          │ adapter_bmp180.c   │
    │ (completo)         │                          │ (sin humedad)      │
    └────────────────────┘                          └────────────────────┘
    
Cambiar sensor = 2 líneas en main.c
```

✅ **Agregar un nuevo sensor** = Crear solo un nuevo adapter  
✅ **Cambiar sensor en runtime** = Llamar a `register()` con otra interfaz  
✅ **Aplicación NUNCA ve detalles** = Solo ve interfaz genérica  
✅ **Testing es trivial** = Mock de la interfaz genérica

### 3. Patrón Builder (Constructor) - Ejemplo Completo Funcional

**Concepto**: Permite construir configuraciones complejas de forma legible y segura, evitando errores de parámetros.

**Caso de Uso**: Diferentes módulos del sistema pueden necesitar diferentes configuraciones de sensores sin tener que pasar muchos parámetros.

#### Paso 1: Definir la Interfaz del Builder

Archivo: `include/hal_sensor_config_builder.h`

```c
#ifndef __HAL_SENSOR_CONFIG_BUILDER_H__
#define __HAL_SENSOR_CONFIG_BUILDER_H__

#include "hal_sensors.h"
#include <stdint.h>

/**
 * @brief Constructor opaco para configuración
 */
typedef struct hal_sensor_config_builder hal_sensor_config_builder_t;

// Funciones de construcción (Fluent Interface)
hal_sensor_config_builder_t* hal_sensor_config_builder_new(void);
hal_sensor_config_builder_t* hal_sensor_config_with_speed(hal_sensor_config_builder_t* b, uint32_t speed_hz);
hal_sensor_config_builder_t* hal_sensor_config_with_scl_pin(hal_sensor_config_builder_t* b, uint8_t pin);
hal_sensor_config_builder_t* hal_sensor_config_with_sda_pin(hal_sensor_config_builder_t* b, uint8_t pin);
hal_sensor_config_builder_t* hal_sensor_config_with_i2c_port(hal_sensor_config_builder_t* b, uint8_t port);
hal_sensor_config_builder_t* hal_sensor_config_with_device_addr(hal_sensor_config_builder_t* b, uint8_t addr);

// Método final que construye la configuración
hal_sensor_config_t hal_sensor_config_build(hal_sensor_config_builder_t* b);

// Método para obtener valores por defecto pre-configurados
hal_sensor_config_t hal_sensor_config_defaults(void);
hal_sensor_config_t hal_sensor_config_esp32c3(void);
hal_sensor_config_t hal_sensor_config_performance(void);
hal_sensor_config_t hal_sensor_config_low_power(void);

#endif
```

#### Paso 2: Implementar el Builder

Archivo: `src/hal_sensor_config_builder.c`

```c
#include "hal_sensor_config_builder.h"
#include <stdlib.h>
#include <esp_log.h>

static const char* TAG = "CONFIG_BUILDER";

/**
 * @brief Estructura interna del builder
 * Opaca para el usuario
 */
struct hal_sensor_config_builder {
    uint32_t i2c_speed_hz;
    uint8_t scl_pin;
    uint8_t sda_pin;
    uint8_t i2c_port;
    uint8_t device_addr;
    bool speed_set;
    bool pins_set;
    bool port_set;
    bool addr_set;
};

/**
 * @brief Crear nuevo builder con valores por defecto
 */
hal_sensor_config_builder_t* hal_sensor_config_builder_new(void) {
    hal_sensor_config_builder_t* builder = malloc(sizeof(*builder));
    if (builder == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed");
        return NULL;
    }
    
    // Valores por defecto (compatibles con ESP32-C3)
    builder->i2c_speed_hz = 100000;  // 100 kHz
    builder->scl_pin = 9;
    builder->sda_pin = 8;
    builder->i2c_port = 0;
    builder->device_addr = 0x76;
    
    builder->speed_set = false;
    builder->pins_set = false;
    builder->port_set = false;
    builder->addr_set = false;
    
    ESP_LOGD(TAG, "Builder created with defaults");
    return builder;
}

/**
 * @brief Establecer velocidad I2C
 * Validar que esté en rango válido
 */
hal_sensor_config_builder_t* hal_sensor_config_with_speed(hal_sensor_config_builder_t* b, uint32_t speed_hz) {
    if (b == NULL) {
        return NULL;
    }
    
    if (speed_hz < 10000 || speed_hz > 1000000) {
        ESP_LOGW(TAG, "I2C speed %u Hz out of range [10k-1M], using default", speed_hz);
        return b;
    }
    
    b->i2c_speed_hz = speed_hz;
    b->speed_set = true;
    ESP_LOGD(TAG, "Speed set to %u Hz", speed_hz);
    return b;
}

/**
 * @brief Establecer pin SCL
 */
hal_sensor_config_builder_t* hal_sensor_config_with_scl_pin(hal_sensor_config_builder_t* b, uint8_t pin) {
    if (b == NULL) {
        return NULL;
    }
    
    b->scl_pin = pin;
    return b;
}

/**
 * @brief Establecer pin SDA
 */
hal_sensor_config_builder_t* hal_sensor_config_with_sda_pin(hal_sensor_config_builder_t* b, uint8_t pin) {
    if (b == NULL) {
        return NULL;
    }
    
    b->sda_pin = pin;
    return b;
}

/**
 * @brief Establecer pins SCL y SDA
 * Valida que sean diferentes
 */
static hal_sensor_config_builder_t* _with_pins_internal(hal_sensor_config_builder_t* b, uint8_t scl, uint8_t sda) {
    if (scl == sda) {
        ESP_LOGW(TAG, "SCL and SDA cannot be the same pin");
        return b;
    }
    
    b->scl_pin = scl;
    b->sda_pin = sda;
    b->pins_set = true;
    ESP_LOGD(TAG, "Pins set: SCL=%u, SDA=%u", scl, sda);
    return b;
}

/**
 * @brief Establecer puerto I2C
 */
hal_sensor_config_builder_t* hal_sensor_config_with_i2c_port(hal_sensor_config_builder_t* b, uint8_t port) {
    if (b == NULL) {
        return NULL;
    }
    
    if (port > 1) {
        ESP_LOGW(TAG, "I2C port %u invalid, using default", port);
        return b;
    }
    
    b->i2c_port = port;
    b->port_set = true;
    ESP_LOGD(TAG, "I2C port set to %u", port);
    return b;
}

/**
 * @brief Establecer dirección del dispositivo
 */
hal_sensor_config_builder_t* hal_sensor_config_with_device_addr(hal_sensor_config_builder_t* b, uint8_t addr) {
    if (b == NULL) {
        return NULL;
    }
    
    if (addr > 0x7F) {
        ESP_LOGW(TAG, "Address 0x%02X invalid for 7-bit I2C", addr);
        return b;
    }
    
    b->device_addr = addr;
    b->addr_set = true;
    ESP_LOGD(TAG, "Device address set to 0x%02X", addr);
    return b;
}

/**
 * @brief Construir la configuración final
 * Validar que todos los parámetros sean consistentes
 */
hal_sensor_config_t hal_sensor_config_build(hal_sensor_config_builder_t* b) {
    if (b == NULL) {
        ESP_LOGE(TAG, "Builder is NULL");
        return hal_sensor_config_defaults();
    }
    
    // Validar consistencia
    if (b->pins_set && b->scl_pin == b->sda_pin) {
        ESP_LOGE(TAG, "Invalid configuration: SCL and SDA are the same");
        free(b);
        return hal_sensor_config_defaults();
    }
    
    hal_sensor_config_t config = {
        .i2c_speed_hz = b->i2c_speed_hz,
        .scl_pin = b->scl_pin,
        .sda_pin = b->sda_pin,
        .i2c_port = b->i2c_port,
        .device_addr = b->device_addr
    };
    
    ESP_LOGI(TAG, "Configuration built: Speed=%u Hz, Port=%u, SCL=%u, SDA=%u, Addr=0x%02X",
             config.i2c_speed_hz, config.i2c_port, config.scl_pin, config.sda_pin, config.device_addr);
    
    free(b);
    return config;
}

/**
 * @brief Configuración por defecto (ESP32-C3)
 */
hal_sensor_config_t hal_sensor_config_defaults(void) {
    return (hal_sensor_config_t){
        .i2c_speed_hz = 100000,
        .scl_pin = 9,
        .sda_pin = 8,
        .i2c_port = 0,
        .device_addr = 0x76
    };
}

/**
 * @brief Configuración optimizada para ESP32-C3
 */
hal_sensor_config_t hal_sensor_config_esp32c3(void) {
    return (hal_sensor_config_t){
        .i2c_speed_hz = 100000,
        .scl_pin = 9,
        .sda_pin = 8,
        .i2c_port = 0,
        .device_addr = 0x76
    };
}

/**
 * @brief Configuración para máximo rendimiento
 * I2C más rápido, tolerancia a ruido reducida
 */
hal_sensor_config_t hal_sensor_config_performance(void) {
    return (hal_sensor_config_t){
        .i2c_speed_hz = 400000,  // 400 kHz Fast mode
        .scl_pin = 9,
        .sda_pin = 8,
        .i2c_port = 0,
        .device_addr = 0x76
    };
}

/**
 * @brief Configuración para bajo consumo
 * I2C más lento, más tolerancia a ruido
 */
hal_sensor_config_t hal_sensor_config_low_power(void) {
    return (hal_sensor_config_t){
        .i2c_speed_hz = 50000,   // 50 kHz para máxima estabilidad
        .scl_pin = 9,
        .sda_pin = 8,
        .i2c_port = 0,
        .device_addr = 0x76
    };
}
```

#### Paso 3: Usar el Builder en main.c

```c
#include "hal_sensor_config_builder.h"
#include "esp_log.h"

static const char* TAG = "MAIN";

void app_main() {
    hal_sensor_config_t config;
    
    // Opción 1: Usar configuración por defecto
    config = hal_sensor_config_defaults();
    
    // Opción 2: Construir configuración personalizada (Fluent Interface)
    config = hal_sensor_config_build(
        hal_sensor_config_with_speed(
            hal_sensor_config_builder_new(),
            400000  // 400 kHz
        )
    );
    
    // Opción 3: Construir con múltiples parámetros (legible)
    config = hal_sensor_config_build(
        hal_sensor_config_with_device_addr(
            hal_sensor_config_with_i2c_port(
                hal_sensor_config_with_sda_pin(
                    hal_sensor_config_with_scl_pin(
                        hal_sensor_config_with_speed(
                            hal_sensor_config_builder_new(),
                            100000
                        ),
                        10   // SCL en GPIO10
                    ),
                    11   // SDA en GPIO11
                ),
                1    // I2C port 1
            ),
            0x77 // BME280 en dirección alternativa
        )
    );
    
    // Opción 4: Usar presets para casos comunes
    // config = hal_sensor_config_performance();
    // config = hal_sensor_config_low_power();
    
    // Inicializar sensor con la configuración
    hal_sensor_status_t status = hal_sensor_init(&config);
    if (status != HAL_SENSOR_OK) {
        ESP_LOGE(TAG, "Failed to init sensor: %d", status);
        return;
    }
    
    // Loop principal
    for (int i = 0; i < 10; i++) {
        hal_sensor_data_t data;
        status = hal_sensor_read(&data);
        
        if (status == HAL_SENSOR_OK) {
            printf("T: %.2f°C | H: %.2f%% | P: %.2f hPa\n",
                   data.temperature, data.humidity, data.pressure);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    hal_sensor_deinit();
}
```

**Ventajas del Patrón Builder**:
- ✅ **Legibilidad**: Código auto-documentable
- ✅ **Flexibilidad**: Fácil agregar nuevos parámetros sin romper API
- ✅ **Validación**: Errores detectados durante construcción, no uso
- ✅ **Presets**: Configuraciones pre-optimizadas para casos comunes
- ✅ **Encadenamiento**: Interfaz fluida y elegante

---

## Flujos y Secuencias

### Diagrama de Secuencia de Inicialización (Completo)

Este diagrama muestra exactamente qué sucede cuando llamas a `hal_sensor_init()`:

```
app_main()
  │
  └──→ hal_sensor_init(&config)
        │
        ├──→ VALIDAR parámetros
        │   ├─ config != NULL ✓
        │   ├─ speed 10k-1M Hz ✓
        │   ├─ pines válidos ✓
        │   └─ SCL ≠ SDA ✓
        │
        ├──→ system_init()
        │   │
        │   ├──→ i2c_new_master_bus()  [ESP-IDF]
        │   │   │
        │   │   ├─ Configurar I2C_NUM_0
        │   │   ├─ SCL = GPIO_NUM_9
        │   │   ├─ SDA = GPIO_NUM_8
        │   │   ├─ Speed = 100 kHz
        │   │   ├─ Glitch filter = 7
        │   │   └─ Pull-ups = habilitados
        │   │   
        │   │   └──→ return busHandle [creado]
        │   │
        │   └──→ i2c_master_bus_add_device()  [ESP-IDF]
        │       │
        │       ├─ Dirección = 0x76 (BME280)
        │       ├─ Speed = 100 kHz
        │       ├─ Addressing = 7-bit
        │       │
        │       └──→ return deviceHandle [registrado]
        │
        ├──→ sensors_init(&sensorConfig)
        │   │
        │   ├──→ Configurar callbacks I2C
        │   │   ├─ .read = ibme280_i2c_read
        │   │   ├─ .write = ibme280_i2c_write
        │   │   └─ .delay_us = ibme280_delay_us
        │   │
        │   ├──→ bme280_init(&dev)  [Driver Bosch]
        │   │   │
        │   │   ├─ Leer BME280 chip ID (0xD0)
        │   │   │   └──→ ibme280_i2c_read(0xD0) ← Transmit/Receive I2C
        │   │   │
        │   │   ├─ Verificar ID = 0x60 ✓
        │   │   │
        │   │   └─ Leer calibración data (26 bytes)
        │   │       └──→ ibme280_i2c_read(0x88-0xA1) ← I2C ops
        │   │
        │   ├──→ bme280_get_sensor_settings()
        │   │   └─ Lee configuración actual del sensor
        │   │
        │   ├──→ Configurar Oversampling
        │   │   ├─ osr_t = 5  (temp 16x)
        │   │   ├─ osr_h = 5  (humidity 16x)
        │   │   └─ osr_p = 5  (pressure 16x)
        │   │
        │   ├──→ bme280_set_sensor_settings()
        │   │   └─ Escribir en registros 0xF2, 0xF4, 0xF5
        │   │
        │   └──→ bme280_set_sensor_mode(NORMAL)
        │       └─ Sensor comienza mediciones continuas
        │
        └──→ return HAL_SENSOR_OK ✓
```

### Diagrama de Secuencia de Lectura

```
hal_sensor_read(&data)
  │
  ├──→ VALIDAR estado
  │   ├─ initialized == true ✓
  │   └─ data != NULL ✓
  │
  └──→ sensors_update(&sensor_data)
      │
      └──→ bme280_get_sensor_data(BME280_ALL)
          │
          ├──→ Leer registro de estado (0xF3)
          │   └──→ ibme280_i2c_read(0xF3, 1 byte)
          │       │
          │       ├─ [I2C START]
          │       ├─ [Addr + W] → 0x76
          │       ├─ [Reg addr] → 0xF3
          │       ├─ [RESTART]  ← Repeated START
          │       ├─ [Addr + R] → 0x76
          │       ├─ [Read] ← 1 byte de estado
          │       └─ [I2C STOP]
          │
          ├──→ Esperar si medición en progreso
          │   └─ Poll cada 20ms
          │
          ├──→ Leer datos ADC (8 bytes)
          │   └──→ ibme280_i2c_read(0xF7, 8 bytes) ← Repeated START
          │
          ├──→ Aplicar calibración
          │   ├─ Temperature = raw_T * k_t + offset_t
          │   ├─ Pressure = raw_P * k_p + offset_p
          │   └─ Humidity = raw_H * k_h + offset_h
          │
          └──→ return (temp, humidity, pressure) ✓
```

### Flujo Completo: De Configuración a Datos

```
ETAPA 1: INICIALIZACIÓN
┌───────────────────────────────────────────────────────┐
│ 1. app_main() configura con hal_sensor_config_builder  │
│ 2. hal_sensor_init() inicializa I2C y sensor           │
│ 3. Sensor comienza mediciones continuas en background  │
└───────────────────────────────────────────────────────┘
                        ↓
ETAPA 2: ESPERA Y MEDICIÓN (No-blocking)
┌───────────────────────────────────────────────────────┐
│ Tiempo: ~100-200ms                                    │
│ BME280 toma mediciones automáticamente                 │
│ Aplicación puede hacer otras cosas                    │
└───────────────────────────────────────────────────────┘
                        ↓
ETAPA 3: LECTURA DE DATOS
┌───────────────────────────────────────────────────────┐
│ hal_sensor_read(&data) → Obtiene datos de BME280      │
│ (rápido: ~few ms)                                     │
└───────────────────────────────────────────────────────┘
                        ↓
ETAPA 4: PROCESAR Y USAR
┌───────────────────────────────────────────────────────┐
│ data.temperature, data.humidity, data.pressure ready   │
│ Usar en lógica de riego, almacenamiento, etc.         │
└───────────────────────────────────────────────────────┘
```

---

## Mejores Prácticas


### 1. **Manejo de Errores Robusto - Ejemplo Completo**

#### Tabla de Códigos de Error

```
┌──────────┬──────────────────────────┬─────────────────────┐
│ Código   │ Significado               │ Acción Recomendada  │
├──────────┼──────────────────────────┼─────────────────────┤
│ 0        │ OK - Operación exitosa   │ Continuar           │
│ -1       │ NULL pointer             │ Revisar punteros    │
│ -2       │ Device not found         │ Revisar conexión    │
│ -3       │ Communication error      │ Reset I2C           │
│ -4       │ Invalid configuration    │ Validar parámetros  │
│ -5       │ Timeout waiting          │ Aumentar timeout    │
│ -6       │ Calibration failed       │ Reboot sensor       │
└──────────┴──────────────────────────┴─────────────────────┘
```

#### Función de Inicialización Robusta

```c
/**
 * Inicialización con manejo de errores completo
 */
typedef enum {
    INIT_OK = 0,
    INIT_NULL_CONFIG = -1,
    INIT_INVALID_SPEED = -2,
    INIT_INVALID_PINS = -3,
    INIT_BME280_FAIL = -4,
    INIT_I2C_FAIL = -5,
    INIT_TIMEOUT = -6
} init_error_t;

init_error_t hal_sensor_init_robust(const hal_sensor_config_t* config) {
    // Paso 1: Validación de entrada
    if (config == NULL) {
        ESP_LOGE(TAG, "Error: Config is NULL");
        return INIT_NULL_CONFIG;
    }
    
    // Paso 2: Validar rango de velocidad
    if (config->i2c_speed_hz < 10000 || config->i2c_speed_hz > 1000000) {
        ESP_LOGE(TAG, "Error: I2C speed %u Hz out of range [10k-1M]", 
                 config->i2c_speed_hz);
        return INIT_INVALID_SPEED;
    }
    
    // Paso 3: Validar pines
    if (config->scl_pin >= 48 || config->sda_pin >= 48) {
        ESP_LOGE(TAG, "Error: Invalid GPIO pins (max=47)");
        return INIT_INVALID_PINS;
    }
    
    if (config->scl_pin == config->sda_pin) {
        ESP_LOGE(TAG, "Error: SCL and SDA cannot be the same");
        return INIT_INVALID_PINS;
    }
    
    // Paso 4: Inicializar I2C
    ESP_LOGI(TAG, "Initializing I2C at %u Hz...", config->i2c_speed_hz);
    
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = config->i2c_port,
        .scl_io_num = config->scl_pin,
        .sda_io_num = config->sda_pin,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    i2c_master_bus_handle_t bus_handle;
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &bus_handle);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error: Failed to create I2C bus: %s", esp_err_to_name(ret));
        return INIT_I2C_FAIL;
    }
    
    ESP_LOGI(TAG, "✓ I2C bus created");
    
    // Paso 5: Registrar dispositivo I2C
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_7,
        .device_address = config->device_addr,
        .scl_speed_hz = config->i2c_speed_hz,
    };
    
    i2c_master_dev_handle_t dev_handle;
    ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error: Failed to add device: %s", esp_err_to_name(ret));
        i2c_del_master_bus(bus_handle);
        return INIT_I2C_FAIL;
    }
    
    ESP_LOGI(TAG, "✓ Device registered at 0x%02X", config->device_addr);
    
    // Paso 6: Inicializar BME280
    uint8_t chip_id;
    ret = i2c_master_transmit_receive(dev_handle, 
                                      (uint8_t[]){0xD0}, 1,
                                      &chip_id, 1,
                                      -1);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error: Cannot read chip ID: %s", esp_err_to_name(ret));
        i2c_del_master_bus(bus_handle);
        return INIT_BME280_FAIL;
    }
    
    if (chip_id != 0x60) {
        ESP_LOGE(TAG, "Error: Invalid chip ID 0x%02X (expected 0x60)", chip_id);
        i2c_del_master_bus(bus_handle);
        return INIT_BME280_FAIL;
    }
    
    ESP_LOGI(TAG, "✓ BME280 detected (ID=0x%02X)", chip_id);
    
    return INIT_OK;
}

/**
 * Función de verificación periódica
 */
void hal_sensor_health_check(void) {
    uint8_t status;
    
    // Leer registro de estado
    i2c_master_transmit_receive(dev_handle,
                                (uint8_t[]){0xF3}, 1,
                                &status, 1,
                                -1);
    
    // Analizar bits de estado
    bool measuring = (status & 0x01);  // Bit 0: medición en progreso
    bool im_update = (status & 0x02);  // Bit 1: actualización imagen
    
    if (measuring) {
        ESP_LOGD(TAG, "✓ Sensor is measuring");
    }
    
    if (im_update) {
        ESP_LOGW(TAG, "⚠ Image update in progress");
    }
    
    ESP_LOGI(TAG, "Health check passed - Status: 0x%02X", status);
}
```

#### Error Handling en Lectura

```c
/**
 * Lectura con reintentos y manejo de errores
 */
typedef struct {
    float temperature;
    float humidity;
    float pressure;
    bool valid;
    int error_code;
} sensor_read_result_t;

sensor_read_result_t hal_sensor_read_with_retry(int max_retries) {
    sensor_read_result_t result = {0};
    
    for (int attempt = 0; attempt < max_retries; attempt++) {
        ESP_LOGD(TAG, "Read attempt %d/%d", attempt + 1, max_retries);
        
        // Intentar leer
        uint8_t data[8];
        esp_err_t ret = i2c_master_transmit_receive(
            dev_handle,
            (uint8_t[]){0xF7}, 1,
            data, 8,
            pdMS_TO_TICKS(100)
        );
        
        if (ret == ESP_OK) {
            // Éxito - convertir datos
            result.temperature = convert_temperature_data(data);
            result.humidity = convert_humidity_data(data);
            result.pressure = convert_pressure_data(data);
            result.valid = true;
            result.error_code = 0;
            
            ESP_LOGD(TAG, "✓ Read successful on attempt %d", attempt + 1);
            return result;
        }
        
        // Analizar tipo de error
        if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "⚠ Timeout on attempt %d", attempt + 1);
            result.error_code = -5;  // TIMEOUT
        } else if (ret == ESP_ERR_INVALID_ARG) {
            ESP_LOGE(TAG, "✗ Invalid argument on attempt %d", attempt + 1);
            result.error_code = -4;  // INVALID_CONFIG
            break;  // No reintentar si argumento es inválido
        } else {
            ESP_LOGW(TAG, "⚠ I2C error: %s", esp_err_to_name(ret));
            result.error_code = -3;  // COMMUNICATION_ERROR
        }
        
        // Esperar antes de reintentar
        if (attempt < max_retries - 1) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    
    // Todos los intentos fallaron
    result.valid = false;
    ESP_LOGE(TAG, "✗ Read failed after %d attempts", max_retries);
    
    return result;
}
```

**Uso**:
```c
sensor_read_result_t result = hal_sensor_read_with_retry(3);

if (result.valid) {
    printf("T: %.2f°C | H: %.2f%% | P: %.2f hPa\n",
           result.temperature, result.humidity, result.pressure);
} else {
    printf("Error code: %d\n", result.error_code);
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

### 3. **Logging Estratégico - Sistema Completo**

#### Sistema de Logging Multinivel

```c
#include "esp_log.h"
#include <stdio.h>
#include <time.h>

/**
 * @brief Niveles de logging
 * ERROR (crítico)   → WARN → INFO → DEBUG
 */

static const char* TAG = "SENSOR_HAL";
static uint32_t operation_start_time = 0;

/**
 * @brief Marcar inicio de operación
 */
void log_operation_start(const char* operation) {
    operation_start_time = esp_timer_get_time();
    ESP_LOGI(TAG, "START: %s", operation);
}

/**
 * @brief Registrar tiempo de operación
 */
void log_operation_end(const char* operation, bool success) {
    uint32_t elapsed_ms = (esp_timer_get_time() - operation_start_time) / 1000;
    
    if (success) {
        ESP_LOGI(TAG, "✓ %s completed in %u ms", operation, elapsed_ms);
    } else {
        ESP_LOGE(TAG, "✗ %s FAILED after %u ms", operation, elapsed_ms);
    }
}

/**
 * @brief Nivel ERROR: Situaciones críticas
 */
void example_error_logging() {
    // Hardware no detectado
    if (chip_id != 0x60) {
        ESP_LOGE(TAG, "ERROR: BME280 not found");
        ESP_LOGE(TAG, "  Expected ID: 0x60");
        ESP_LOGE(TAG, "  Received ID: 0x%02X", chip_id);
        ESP_LOGE(TAG, "  Action: Check wiring and power");
        return;  // Detener ejecución
    }
    
    // Error de comunicación I2C
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ERROR: I2C communication failed");
        ESP_LOGE(TAG, "  Error: %s (0x%X)", esp_err_to_name(ret), ret);
        ESP_LOGE(TAG, "  Address: 0x%02X, Port: %d", device_addr, i2c_port);
        return;
    }
}

/**
 * @brief Nivel WARNING: Problemas que permiten continuar
 */
void example_warning_logging() {
    // Dato fuera de rango
    if (temperature > 60.0f) {
        ESP_LOGW(TAG, "WARNING: Temperature unusually high");
        ESP_LOGW(TAG, "  Measured: %.2f°C", temperature);
        ESP_LOGW(TAG, "  Expected: 0-50°C");
        ESP_LOGW(TAG, "  Possible: Sensor malfunction or direct sunlight");
    }
    
    // Lectura tardía
    if (read_time_ms > 100) {
        ESP_LOGW(TAG, "WARNING: Slow sensor read");
        ESP_LOGW(TAG, "  Time: %d ms", read_time_ms);
        ESP_LOGW(TAG, "  Expected: <50ms");
        ESP_LOGW(TAG, "  Cause: May be I2C bus congestion");
    }
    
    // Medición no lista
    if (!(status & BME280_STATUS_MEAS_DONE)) {
        ESP_LOGW(TAG, "WARNING: Measurement not ready");
        ESP_LOGW(TAG, "  Status: 0x%02X", status);
        ESP_LOGW(TAG, "  Action: Retry in 50ms");
    }
}

/**
 * @brief Nivel INFO: Información importante
 */
void example_info_logging() {
    // Inicialización exitosa
    ESP_LOGI(TAG, "✓ Sensor initialized successfully");
    ESP_LOGI(TAG, "  Device: BME280");
    ESP_LOGI(TAG, "  Chip ID: 0x%02X", chip_id);
    ESP_LOGI(TAG, "  I2C Address: 0x%02X", i2c_addr);
    ESP_LOGI(TAG, "  I2C Speed: %u Hz", i2c_speed);
    
    // Cambio de configuración
    ESP_LOGI(TAG, "Configuration applied");
    ESP_LOGI(TAG, "  Oversampling: T=%d, H=%d, P=%d", osr_t, osr_h, osr_p);
    ESP_LOGI(TAG, "  Mode: %s", mode_name);
    
    // Lectura de datos
    ESP_LOGI(TAG, "Sensor data received");
    ESP_LOGI(TAG, "  Temperature: %.2f°C", temperature);
    ESP_LOGI(TAG, "  Humidity: %.2f%%", humidity);
    ESP_LOGI(TAG, "  Pressure: %.2f hPa", pressure);
}

/**
 * @brief Nivel DEBUG: Información detallada para desarrollo
 */
void example_debug_logging() {
    // Contenido de registro
    ESP_LOGD(TAG, "DEBUG: I2C transaction");
    ESP_LOGD(TAG, "  Register: 0xF7");
    ESP_LOGD(TAG, "  Bytes: %d", 8);
    ESP_LOGD(TAG, "  Address: 0x%02X", 0x76);
    
    // Datos crudos
    ESP_LOGD(TAG, "DEBUG: Raw ADC values");
    ESP_LOGD(TAG, "  ADC_T: 0x%06X", adc_t_raw);
    ESP_LOGD(TAG, "  ADC_H: 0x%04X", adc_h_raw);
    ESP_LOGD(TAG, "  ADC_P: 0x%06X", adc_p_raw);
    
    // Factores de calibración
    ESP_LOGD(TAG, "DEBUG: Calibration coefficients");
    ESP_LOGD(TAG, "  T1: %u", calib_data.dig_T1);
    ESP_LOGD(TAG, "  T2: %d", calib_data.dig_T2);
    ESP_LOGD(TAG, "  T3: %d", calib_data.dig_T3);
}

/**
 * @brief Función auxiliar para dumpear buffer I2C
 */
void log_buffer_hex(const char* label, const uint8_t* data, int len) {
    printf("%s: ", label);
    for (int i = 0; i < len; i++) {
        printf("0x%02X ", data[i]);
    }
    printf("\n");
    
    ESP_LOGD(TAG, "%s (len=%d)", label, len);
}

/**
 * @brief Logging de estadísticas
 */
typedef struct {
    uint32_t total_reads;
    uint32_t successful_reads;
    uint32_t failed_reads;
    uint32_t total_time_ms;
    float avg_temp;
    float max_temp;
    float min_temp;
} sensor_stats_t;

void log_sensor_statistics(const sensor_stats_t* stats) {
    printf("\n╔════════════════════════════════╗\n");
    printf("║     SENSOR STATISTICS          ║\n");
    printf("╠════════════════════════════════╣\n");
    printf("║ Total Reads:      %u            ║\n", stats->total_reads);
    printf("║ Successful:       %u            ║\n", stats->successful_reads);
    printf("║ Failed:           %u            ║\n", stats->failed_reads);
    printf("║ Success Rate:     %.1f%%         ║\n", 
           (float)stats->successful_reads / stats->total_reads * 100);
    printf("║ Total Time:       %u ms         ║\n", stats->total_time_ms);
    printf("║ Avg Temp:         %.2f°C        ║\n", stats->avg_temp);
    printf("║ Min/Max Temp:     %.2f/%.2f°C   ║\n", stats->min_temp, stats->max_temp);
    printf("╚════════════════════════════════╝\n");
    
    ESP_LOGI(TAG, "Session stats: %u reads, %.1f%% success",
             stats->total_reads,
             (float)stats->successful_reads / stats->total_reads * 100);
}
```

**Uso práctico**:
```c
void app_main() {
    // Inicialización
    log_operation_start("hal_sensor_init");
    hal_sensor_status_t status = hal_sensor_init(&config);
    log_operation_end("hal_sensor_init", status == HAL_SENSOR_OK);
    
    if (status != HAL_SENSOR_OK) {
        return;  // ERROR level logging ha indicado el problema
    }
    
    // Lectura principal
    for (int i = 0; i < 5; i++) {
        hal_sensor_data_t data;
        
        log_operation_start("hal_sensor_read");
        status = hal_sensor_read(&data);
        log_operation_end("hal_sensor_read", status == HAL_SENSOR_OK);
        
        if (status == HAL_SENSOR_OK) {
            ESP_LOGI(TAG, "Data[%d]: %.2f°C | %.2f%% | %.2f hPa",
                     i, data.temperature, data.humidity, data.pressure);
        }
    }
}

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

### 6. **Configuración Avanzada: Oversampling y Modos**

El BME280 permite cambiar el nivel de "oversampling" (múltiples muestras) para balancear precisión vs velocidad:

#### Valores de Oversampling (OSR)

```
OSR Value | Multiplicador | Tiempo Típico | Ruido
──────────┼───────────────┼──────────────┼──────────
0         | No (skip)     | ~0.5ms       | Alto
1         | 2x            | ~2ms         | Medio-alto
2         | 4x            | ~5ms         | Medio
3         | 8x            | ~13ms        | Medio-bajo
4         | 16x           | ~25ms        | Bajo
5         | 16x           | ~33ms        | Muy bajo
```

#### Ejemplo 1: Modo de Máxima Precisión

Para aplicaciones que necesitan máxima precisión (laboratorio, estación meteorológica):

```c
#include "hal_sensors.h"
#include "bme280.h"

/**
 * Configurar modo de máxima precisión
 * - Oversampling 16x en todos los parámetros
 * - I2C a 100 kHz (estable)
 * - Presión: 33ms
 */
void setup_maximum_precision_mode() {
    bme280_settings_t settings;
    
    // Leer configuración actual
    bme280_get_sensor_settings(&settings, &dev);
    
    // Máxima precisión
    settings.osr_t = 5;  // 16x oversampling temperatura
    settings.osr_h = 5;  // 16x oversampling humedad
    settings.osr_p = 5;  // 16x oversampling presión
    
    // Modo normal (mediciones continuas)
    settings.filter_coeff = 4;  // IIR filter coefficient
    
    // Aplicar
    if (bme280_set_sensor_settings(BME280_ALL_SETTINGS_SEL, &settings, &dev) != BME280_OK) {
        ESP_LOGE(TAG, "Failed to set settings");
        return;
    }
    
    // Activar modo normal
    bme280_set_sensor_mode(BME280_NORMAL_MODE, &dev);
    
    ESP_LOGI(TAG, "✓ Maximum precision mode configured (OSR: 5,5,5)");
}

/**
 * Función para leer datos con esperavalidación
 */
hal_sensor_status_t read_high_precision_data(float* temp, float* humidity, float* pressure) {
    bme280_data_t bme_data;
    
    // Esperar a que medición esté lista (100-200ms)
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Leer datos
    if (bme280_get_sensor_data(BME280_ALL, &bme_data, &dev) != BME280_OK) {
        return HAL_SENSOR_ERROR;
    }
    
    *temp = bme_data.temperature;
    *humidity = bme_data.humidity;
    *pressure = bme_data.pressure;
    
    return HAL_SENSOR_OK;
}
```

**Uso**:
```c
float temp, humidity, pressure;

setup_maximum_precision_mode();

// Cada lectura tarda ~150ms pero es muy precisa
if (read_high_precision_data(&temp, &humidity, &pressure) == HAL_SENSOR_OK) {
    printf("T: %.3f°C | H: %.3f%% | P: %.2f hPa\n", temp, humidity, pressure);
}
```

#### Ejemplo 2: Modo de Bajo Consumo de Energía

Para dispositivos con batería:

```c
/**
 * Configurar modo de bajo consumo
 * - Oversampling mínimo (skip algunos)
 * - I2C a 50 kHz (ahorra energía)
 * - Tiempos cortos de medición
 */
void setup_low_power_mode() {
    bme280_settings_t settings;
    
    bme280_get_sensor_settings(&settings, &dev);
    
    // Oversampling bajo
    settings.osr_t = 2;  // 4x oversampling temperatura
    settings.osr_h = 1;  // 2x oversampling humedad
    settings.osr_p = 1;  // 2x oversampling presión
    
    // Modo forzado (mide bajo demanda, no continuo)
    // Esto ahorra mucha energía
    
    if (bme280_set_sensor_settings(BME280_ALL_SETTINGS_SEL, &settings, &dev) != BME280_OK) {
        ESP_LOGE(TAG, "Failed to set settings");
        return;
    }
    
    ESP_LOGI(TAG, "✓ Low-power mode configured (OSR: 2,1,1)");
}

/**
 * Leer datos en modo bajo consumo
 * Solo medir cuando se necesita
 */
hal_sensor_status_t read_low_power_data(float* temp, float* humidity, float* pressure) {
    // Forzar una medición
    bme280_set_sensor_mode(BME280_FORCED_MODE, &dev);
    
    // Esperar solo ~10ms (tiempo corto porque oversampling es bajo)
    vTaskDelay(pdMS_TO_TICKS(20));
    
    bme280_data_t bme_data;
    if (bme280_get_sensor_data(BME280_ALL, &bme_data, &dev) != BME280_OK) {
        return HAL_SENSOR_ERROR;
    }
    
    *temp = bme_data.temperature;
    *humidity = bme_data.humidity;
    *pressure = bme_data.pressure;
    
    return HAL_SENSOR_OK;
}
```

**Uso**:
```c
float temp, humidity, pressure;

setup_low_power_mode();

// Lecturas rápidas (20ms) pero menos precisas
for (int i = 0; i < 10; i++) {
    if (read_low_power_data(&temp, &humidity, &pressure) == HAL_SENSOR_OK) {
        printf("Quick read #%d: T=%.2f°C\n", i+1, temp);
    }
    // Dormir entre lecturas para ahorrar batería
    vTaskDelay(pdMS_TO_TICKS(5000));  // 5 segundos
}
```

#### Ejemplo 3: Modo Balanceado (Por defecto)

Para la mayoría de aplicaciones:

```c
/**
 * Configuración balanceada
 * - Precisión razonable
 * - Velocidad aceptable
 * - Consumo moderado
 */
void setup_balanced_mode() {
    bme280_settings_t settings;
    
    bme280_get_sensor_settings(&settings, &dev);
    
    // Oversampling balanceado
    settings.osr_t = 4;  // 16x pero más rápido
    settings.osr_h = 4;  // 16x pero más rápido
    settings.osr_p = 4;  // 16x pero más rápido
    
    // Modo normal
    settings.filter_coeff = 2;  // IIR filter moderado
    
    bme280_set_sensor_settings(BME280_ALL_SETTINGS_SEL, &settings, &dev);
    bme280_set_sensor_mode(BME280_NORMAL_MODE, &dev);
    
    ESP_LOGI(TAG, "✓ Balanced mode configured (OSR: 4,4,4)");
}
```

#### Tabla Comparativa

```
┌──────────┬──────────┬──────────┬──────────┬─────────────┐
│ Modo     │ OSR Temp │ OSR Hum  │ OSR Pres │ Aplicación  │
├──────────┼──────────┼──────────┼──────────┼─────────────┤
│ Máxima   │    5     │    5     │    5     │ Laboratorio │
│ Balanceado│    4     │    4     │    4     │ Estación    │
│ Rápido   │    3     │    2     │    2     │ Datos rápido│
│ Eco      │    1     │    1     │    1     │ Batería     │
└──────────┴──────────┴──────────┴──────────┴─────────────┘
```

---

#### Estrategia de Testing

Para probar el código sin hardware real, creamos una versión "mock" (simulada) de I2C:

Archivo: `test/hal_sensor_mock.c`

```c
#include "hal_sensors.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char* TAG = "SENSOR_MOCK";

/**
 * @brief Simulación de datos del sensor
 */
typedef struct {
    float temperature;
    float humidity;
    float pressure;
    bool initialized;
    int call_count;
} mock_sensor_data_t;

static mock_sensor_data_t mock_data = {
    .temperature = 22.5f,
    .humidity = 45.3f,
    .pressure = 1013.25f,
    .initialized = false,
    .call_count = 0
};

/**
 * @brief Mock: Inicializar sensor sin I2C real
 */
int32_t mock_sensor_init(void* config) {
    printf("[MOCK] sensor_init called\n");
    mock_data.initialized = true;
    return 0;
}

/**
 * @brief Mock: Leer datos del sensor (datos simulados)
 */
int32_t mock_sensor_read(void* data) {
    if (!mock_data.initialized) {
        printf("[MOCK] ERROR: Not initialized\n");
        return -1;
    }
    
    generic_sensor_data_t* sensor_data = (generic_sensor_data_t*)data;
    
    // Simular variación en datos
    sensor_data->temperature = mock_data.temperature + (mock_data.call_count * 0.1f);
    sensor_data->humidity = mock_data.humidity;
    sensor_data->pressure = mock_data.pressure;
    sensor_data->has_humidity = true;
    sensor_data->has_pressure = true;
    
    mock_data.call_count++;
    
    printf("[MOCK] Read #%d: T=%.2f°C, H=%.2f%%, P=%.2f hPa\n",
           mock_data.call_count,
           sensor_data->temperature,
           sensor_data->humidity,
           sensor_data->pressure);
    
    return 0;
}

/**
 * @brief Mock: Finalizar
 */
int32_t mock_sensor_deinit(void) {
    printf("[MOCK] sensor_deinit called\n");
    mock_data.initialized = false;
    return 0;
}

/**
 * @brief Mock: Imprimir info
 */
void mock_sensor_print_info(void) {
    printf("╔════════════════════════════════════╗\n");
    printf("║  SENSOR: MOCK (Simulado)           ║\n");
    printf("║  Propósito: Testing sin hardware   ║\n");
    printf("║  Status: %s                       ║\n", mock_data.initialized ? "Initialized" : "Not init");
    printf("║  Calls: %d                        ║\n", mock_data.call_count);
    printf("╚════════════════════════════════════╝\n");
}

// Interfaz del sensor mock
const generic_sensor_interface_t mock_sensor_interface = {
    .sensor_name = "MOCK",
    .init = mock_sensor_init,
    .read = mock_sensor_read,
    .deinit = mock_sensor_deinit,
    .print_info = mock_sensor_print_info
};
```

#### Programa de Testing

Archivo: `test/test_sensor.c`

```c
#include "hal_generic_sensor.h"
#include "hal_sensor_config_builder.h"
#include <stdio.h>
#include <assert.h>

extern const generic_sensor_interface_t mock_sensor_interface;
extern const generic_sensor_interface_t bme280_sensor_interface;

/**
 * @brief Test 1: Inicialización básica
 */
void test_basic_init() {
    printf("\n═══ TEST 1: Basic Initialization ═══\n");
    
    // Registrar sensor mock
    assert(hal_sensor_manager_register(&mock_sensor_interface) == 0);
    
    // Inicializar
    assert(hal_sensor_manager_init(NULL) == 0);
    
    printf("✓ Test passed: Basic init\n");
    
    hal_sensor_manager_deinit();
}

/**
 * @brief Test 2: Lectura de datos
 */
void test_read_data() {
    printf("\n═══ TEST 2: Data Reading ═══\n");
    
    hal_sensor_manager_register(&mock_sensor_interface);
    hal_sensor_manager_init(NULL);
    
    // Hacer varias lecturas
    for (int i = 0; i < 3; i++) {
        generic_sensor_data_t data = {0};
        assert(hal_sensor_manager_read(&data) == 0);
        
        // Validar rangos
        assert(data.temperature > 0 && data.temperature < 50);
        assert(data.humidity > 0 && data.humidity < 100);
        assert(data.pressure > 900 && data.pressure < 1100);
        assert(data.has_humidity == true);
        assert(data.has_pressure == true);
    }
    
    printf("✓ Test passed: Data reading (count=%d)\n", 
           hal_sensor_manager_get_read_count());
    
    hal_sensor_manager_deinit();
}

/**
 * @brief Test 3: Cambio de sensor en runtime
 */
void test_sensor_switching() {
    printf("\n═══ TEST 3: Sensor Switching ═══\n");
    
    // Iniciar con MOCK
    hal_sensor_manager_register(&mock_sensor_interface);
    hal_sensor_manager_init(NULL);
    printf("Sensor actual: %s\n", hal_sensor_manager_get_name());
    assert(strcmp(hal_sensor_manager_get_name(), "MOCK") == 0);
    
    // Cambiar a BME280 (simulado)
    hal_sensor_manager_deinit();
    hal_sensor_manager_register(&bme280_sensor_interface);
    // No inicializamos BME280 porque no tenemos hardware real
    printf("Sensor registrado: %s\n", hal_sensor_manager_get_name());
    assert(strcmp(hal_sensor_manager_get_name(), "BME280") == 0);
    
    printf("✓ Test passed: Sensor switching\n");
}

/**
 * @brief Test 4: Validación de parámetros
 */
void test_config_validation() {
    printf("\n═══ TEST 4: Configuration Validation ═══\n");
    
    // Test velocidad válida
    hal_sensor_config_t cfg1 = hal_sensor_config_build(
        hal_sensor_config_with_speed(
            hal_sensor_config_builder_new(),
            100000
        )
    );
    assert(cfg1.i2c_speed_hz == 100000);
    printf("✓ Valid speed 100kHz accepted\n");
    
    // Test velocidad inválida (builder debería rechazarla)
    hal_sensor_config_t cfg2 = hal_sensor_config_build(
        hal_sensor_config_with_speed(
            hal_sensor_config_builder_new(),
            5000000  // Demasiado rápido
        )
    );
    // Debería volver a valores por defecto
    assert(cfg2.i2c_speed_hz == 100000);
    printf("✓ Invalid speed rejected (reverted to default)\n");
    
    printf("✓ Test passed: Config validation\n");
}

/**
 * @brief Test 5: Stress test
 */
void test_stress() {
    printf("\n═══ TEST 5: Stress Test ═══\n");
    
    hal_sensor_manager_register(&mock_sensor_interface);
    hal_sensor_manager_init(NULL);
    
    // 100 lecturas consecutivas
    for (int i = 0; i < 100; i++) {
        generic_sensor_data_t data = {0};
        int result = hal_sensor_manager_read(&data);
        assert(result == 0);
        
        if ((i + 1) % 20 == 0) {
            printf("  %d reads completed\n", i + 1);
        }
    }
    
    printf("✓ Test passed: 100 consecutive reads\n");
    printf("✓ Total reads: %d\n", hal_sensor_manager_get_read_count());
    
    hal_sensor_manager_deinit();
}

/**
 * @brief Función principal de testing
 */
void run_all_tests() {
    printf("\n╔═══════════════════════════════════════════╗\n");
    printf("║   SENSOR TESTING SUITE - MOCK MODE       ║\n");
    printf("╚═══════════════════════════════════════════╝\n");
    
    test_basic_init();
    test_read_data();
    test_sensor_switching();
    test_config_validation();
    test_stress();
    
    printf("\n╔═══════════════════════════════════════════╗\n");
    printf("║   ✓ ALL TESTS PASSED!                    ║\n");
    printf("╚═══════════════════════════════════════════╝\n");
}
```

**Ventajas del Testing con Mocks**:
- ✅ Probar lógica SIN hardware real
- ✅ Probar rápidamente sin tiempos de espera del sensor
- ✅ Reproducible y predecible
- ✅ Fácil de simular condiciones extremas
- ✅ Detectar bugs antes de hardware

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

---

## Integración Completa - Ejemplo de Proyecto Real

Esta sección muestra cómo usar TODOS los patrones y características en un caso real: **Sistema de Riego Automático con MacetoHuerto**.

### Caso de Uso Completo

**Requisitos**:
- Leer temperatura, humedad y presión
- Cambiar de sensor (BME280 o simulado) sin reiniciar
- Validar configuración antes de usar
- Logging para debugging
- Testing sin hardware

### Implementación Completa

#### Paso 1: Configurar el Sistema

```c
// main.c - Proyecto MacetoHuerto completo
#include "hal_sensors.h"
#include "hal_sensor_config_builder.h"
#include "hal_sensor_strategy.h"
#include "hal_sensor_adapter.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "MACETO_MAIN";

/**
 * @brief Contexto global de la aplicación
 */
typedef struct {
    hal_sensor_config_t config;
    hal_sensor_adapter_manager_t adapter_mgr;
    hal_sensor_strategy_context_t strategy_ctx;
    uint32_t read_count;
    uint32_t error_count;
} maceto_app_context_t;

static maceto_app_context_t app_ctx = {0};

/**
 * @brief PASO 1: Construir configuración con Builder Pattern
 */
hal_sensor_config_t build_sensor_config(bool performance_mode) {
    hal_sensor_config_builder_t* builder = hal_sensor_config_builder_new();
    
    if (performance_mode) {
        // Modo alto rendimiento
        builder = hal_sensor_config_with_speed(builder, 400000);  // 400 kHz
        ESP_LOGI(TAG, "Performance mode: 400kHz I2C");
    } else {
        // Modo bajo consumo
        builder = hal_sensor_config_with_speed(builder, 50000);   // 50 kHz
        ESP_LOGI(TAG, "Low-power mode: 50kHz I2C");
    }
    
    // Configurar para ESP32-C3
    builder = hal_sensor_config_with_i2c_port(builder, 0);
    builder = hal_sensor_config_with_scl_pin(builder, 9);
    builder = hal_sensor_config_with_sda_pin(builder, 8);
    builder = hal_sensor_config_with_device_addr(builder, 0x76);
    
    hal_sensor_config_t config = hal_sensor_config_build(builder);
    
    ESP_LOGI(TAG, "Config built: %u Hz, Port %u, 0x%02X",
             config.i2c_speed_hz, config.i2c_port, config.device_addr);
    
    return config;
}

/**
 * @brief PASO 2: Inicializar sistema con Adapter Pattern
 */
hal_sensor_status_t init_with_adapter() {
    ESP_LOGI(TAG, "Initializing sensor adapter manager...");
    
    // Registrar soporte para múltiples sensores
    // (En este caso solo BME280, pero la arquitectura permite más)
    app_ctx.adapter_mgr.active_sensor = HAL_SENSOR_BME280;
    
    // Inicializar sensor actual
    hal_sensor_status_t status = hal_sensor_init(&app_ctx.config);
    if (status != HAL_SENSOR_OK) {
        ESP_LOGE(TAG, "Sensor init failed: %d", status);
        return status;
    }
    
    ESP_LOGI(TAG, "✓ Sensor initialized successfully");
    return HAL_SENSOR_OK;
}

/**
 * @brief PASO 3: Configurar protocolo con Strategy Pattern
 */
hal_sensor_status_t setup_communication_strategy() {
    ESP_LOGI(TAG, "Setting up communication strategy...");
    
    // Inicializar estrategia I2C (por defecto)
    app_ctx.strategy_ctx.protocol = HAL_PROTOCOL_I2C;
    
    // En un sistema real, podrías permitir cambio en runtime:
    // app_ctx.strategy_ctx.protocol = HAL_PROTOCOL_SPI;
    // app_ctx.strategy_ctx.protocol = HAL_PROTOCOL_UART;
    
    switch (app_ctx.strategy_ctx.protocol) {
        case HAL_PROTOCOL_I2C:
            ESP_LOGI(TAG, "Strategy: I2C communication");
            break;
        case HAL_PROTOCOL_SPI:
            ESP_LOGI(TAG, "Strategy: SPI communication");
            break;
        case HAL_PROTOCOL_UART:
            ESP_LOGI(TAG, "Strategy: UART communication");
            break;
    }
    
    return HAL_SENSOR_OK;
}

/**
 * @brief PASO 4: Loop principal con lectura de datos
 */
void sensor_read_task(void* pvParameters) {
    ESP_LOGI(TAG, "Sensor read task started");
    
    for (int i = 0; i < 10; i++) {
        // Leer datos
        hal_sensor_data_t data;
        hal_sensor_status_t status = hal_sensor_read(&data);
        
        if (status == HAL_SENSOR_OK) {
            // Datos válidos
            app_ctx.read_count++;
            
            printf("\n╔═════════════════════════════════════╗\n");
            printf("║ Lectura #%u                          ║\n", app_ctx.read_count);
            printf("╠═════════════════════════════════════╣\n");
            printf("║ Temperatura: %.2f °C               ║\n", data.temperature);
            printf("║ Humedad:     %.2f %%               ║\n", data.humidity);
            printf("║ Presión:     %.2f hPa             ║\n", data.pressure);
            printf("╚═════════════════════════════════════╝\n");
            
            // LÓGICA DE NEGOCIO: Decisiones de riego
            if (data.humidity < 30.0f) {
                ESP_LOGW(TAG, "⚠️  Humedad baja: %.2f%% - ACTIVAR RIEGO", data.humidity);
                // pump_turn_on();  // Función para activar bomba
            } else if (data.humidity > 70.0f) {
                ESP_LOGI(TAG, "✓ Humedad adecuada: %.2f%%", data.humidity);
                // pump_turn_off();  // Función para desactivar bomba
            }
            
        } else {
            // Error en lectura
            app_ctx.error_count++;
            ESP_LOGE(TAG, "❌ Error reading sensor: %d (error count: %u)", 
                     status, app_ctx.error_count);
        }
        
        // Esperar 2 segundos antes de la próxima lectura
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    
    ESP_LOGI(TAG, "Sensor read task completed");
    ESP_LOGI(TAG, "Stats - Reads: %u, Errors: %u", 
             app_ctx.read_count, app_ctx.error_count);
    
    vTaskDelete(NULL);
}

/**
 * @brief Punto de entrada de la aplicación
 */
void app_main() {
    printf("\n╔════════════════════════════════════════════════╗\n");
    printf("║         MACETO HUERTO - SISTEMA COMPLETO      ║\n");
    printf("║  Temperatura | Humedad | Presión | Riego      ║\n");
    printf("╚════════════════════════════════════════════════╝\n\n");
    
    // FASE 1: Construcción (Builder Pattern)
    printf("[FASE 1] Construyendo configuración...\n");
    app_ctx.config = build_sensor_config(false);  // Modo bajo consumo
    
    // FASE 2: Inicialización (Adapter Pattern)
    printf("[FASE 2] Inicializando adaptador de sensor...\n");
    if (init_with_adapter() != HAL_SENSOR_OK) {
        ESP_LOGE(TAG, "Failed to init adapter");
        return;
    }
    
    // FASE 3: Configuración de estrategia (Strategy Pattern)
    printf("[FASE 3] Configurando estrategia de comunicación...\n");
    if (setup_communication_strategy() != HAL_SENSOR_OK) {
        ESP_LOGE(TAG, "Failed to setup strategy");
        return;
    }
    
    // FASE 4: Loop de operación
    printf("[FASE 4] Iniciando lectura de sensores...\n\n");
    xTaskCreate(
        sensor_read_task,
        "sensor_read",
        4096,
        NULL,
        10,
        NULL
    );
    
    // Mantener app_main corriendo (FreeRTOS necesita esto)
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
```

#### Paso 2: Cambio de Sensor en Runtime (Adapter Pattern)

```c
/**
 * @brief Cambiar de sensor sin reiniciar
 * Ejemplo: Si el BME280 falla, cambiar a sensor simulado
 */
void switch_to_fallback_sensor() {
    ESP_LOGW(TAG, "Switching to fallback sensor (MOCK)...");
    
    // Finalizar sensor actual
    hal_sensor_deinit();
    
    // Cambiar configuración
    app_ctx.adapter_mgr.active_sensor = HAL_SENSOR_MOCK;
    
    // Inicializar nuevo sensor
    if (hal_sensor_init(&app_ctx.config) == HAL_SENSOR_OK) {
        ESP_LOGI(TAG, "✓ Successfully switched to MOCK sensor");
    } else {
        ESP_LOGE(TAG, "Failed to switch sensor");
    }
}
```

#### Paso 3: Cambio de Protocolo en Runtime (Strategy Pattern)

```c
/**
 * @brief Cambiar de I2C a SPI (ejemplo teórico)
 * Útil si detectas ruido en I2C
 */
void switch_to_spi_protocol() {
    ESP_LOGW(TAG, "Switching to SPI protocol...");
    
    // Finalizar I2C
    hal_sensor_deinit();
    
    // Cambiar estrategia
    app_ctx.strategy_ctx.protocol = HAL_PROTOCOL_SPI;
    
    // Re-configurar con pines SPI
    app_ctx.config.scl_pin = 12;  // CLK → GPIO12
    app_ctx.config.sda_pin = 11;  // MOSI → GPIO11
    
    // Reinicializar
    if (hal_sensor_init(&app_ctx.config) == HAL_SENSOR_OK) {
        ESP_LOGI(TAG, "✓ Successfully switched to SPI");
    }
}
```

### Ventajas de Esta Arquitectura

**Patrones Aplicados**:
- ✅ **Builder**: Configuración limpia y validada
- ✅ **Adapter**: Cambio de sensor sin cambiar código de negocio
- ✅ **Strategy**: Cambio de protocolo transparente
- ✅ **HAL**: Aislación de detalles de hardware

**Resultados Finales**:
- ✅ Código mantenible y escalable
- ✅ Fácil agregar nuevos sensores
- ✅ Fácil cambiar protocolos
- ✅ Testing sin hardware
- ✅ Logging completo para debugging

---

**Versión**: 1.0
**Última actualización**: 3 de Mayo de 2026
**Autor**: Equipo de Desarrollo MacetoHuerto
