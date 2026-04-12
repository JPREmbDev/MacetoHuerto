# 🪴 Macetohuerto: Smart Irrigation System

Sistema de monitoreo y riego automatizado basado en **ESP32-C3**, diseñado para funcionar con energía solar y reportar datos a un stack de telemetría (TIG).

## 📋 Descripción General
Este proyecto gestiona de forma inteligente el riego de un huerto urbano utilizando sensores de humedad, temperatura y peso. Los datos se envían vía **MQTT** a un servidor **Proxmox** que aloja una base de datos **InfluxDB** y un panel de **Grafana**.

---

## 🛠️ Hardware Utilizado
- **Controlador:** ESP32-C3 Mini.
- **Sensores Ambientales:** BME280 (I2C) para temperatura, humedad y presión.
- **Sensores Analógicos:** LDR (Luz) y Humedad de suelo (Capacitivo) vía **ADS1115** (ADC de 16 bits).
- **Gestión de Agua:** Célula de carga con **HX711** para pesar el depósito de agua.
- **Actuadores:** Bomba de agua controlada por **MOSFETs**.
- **Energía:** Panel solar con sistema de carga y balanceador para baterías 18650.

---

## 🚀 Roadmap de Desarrollo

### Fase 1: Entorno y Comunicación 🟢 (Completado)
- [x] Configuración inicial (PlatformIO + ESP-IDF/Arduino).
- [x] Estructura de ramas Git.
- [x] Implementación de interfaz para **BME280**.

### Fase 2: Expansión de Sensores e I2C 🟡 (En curso)
- [ ] **I2C Scanner**: Diagnóstico del bus.
- [ ] **ADS1115**: Lectura de LDR y humedad de suelo.
- [ ] **HX711**: Calibración y lectura de peso.

### Fase 3: Control y Actuadores 🔴
- [ ] **Lógica de Riego**: Algoritmo por umbrales.
- [ ] **Control de Bomba**: Gestión del MOSFET.
- [ ] **Deep Sleep**: Optimización energética.

### Fase 4: Telemetría y Monitoreo 🔵
- [ ] **Conectividad MQTT**.
- [ ] **InfluxDB & Grafana Dashboards**.

---

## 🔌 Esquema de Conexión (Pinout Sugerido)
| Componente | Protocolo | Pin ESP32-C3 | Notas |
| :--- | :--- | :--- | :--- |
| **BME280 / ADS1115** | I2C SDA | GPIO 8 | Bus compartido |
| **BME280 / ADS1115** | I2C SCL | GPIO 9 | Bus compartido |
| **HX711 (DT)** | Digital | GPIO 4 | Datos célula carga |
| **HX711 (SCK)** | Digital | GPIO 5 | Reloj célula carga |
| **Bomba Agua** | Digital | GPIO 10 | Control vía MOSFET |

---

## 💻 Desarrollo y Firmware
El firmware está desarrollado en C++ utilizando **PlatformIO** y el framework de **Espressif (ESP-IDF/Arduino)**.

### 🌿 Flujo de Trabajo (Git Flow)
Para asegurar la estabilidad, seguimos un sistema de ramas:
1. **`main`**: Código estable y funcional.
2. **`develop`**: Rama de integración para nuevas funciones.
3. **`feature/nombre-funcionalidad`**: Ramas aisladas para cada sensor o actuador.

> **⚠️ Regla de oro:** Cada nuevo desarrollo nace desde una rama `develop` limpia para evitar conflictos.

### 📁 Estructura del Proyecto
```text
.
├── src/                # Código fuente principal
├── include/            # Archivos de cabecera (.h)
├── lib/                # Librerías específicas de sensores
└── platformio.ini      # Configuración y dependencias
```

### 🐧 Requisitos en Linux (Fedora)
Para depurar y cargar el código, se recomienda el uso de **Minicom**:

```bash
# Permisos para el puerto serial (requiere reinicio de sesión)
sudo usermod -a -G dialout $USER 

# Ejecución de monitoreo
minicom -D /dev/ttyACM0 -b 115200 -c on
``` 