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

## 💻 Desarrollo y Firmware
El firmware está desarrollado en C++ utilizando **PlatformIO** y el framework de **Espressif (ESP-IDF/Arduino)**.

### Requisitos en Linux (Fedora)
Para depurar y cargar el código, se recomienda el uso de **Minicom**:
```bash
# Permisos para el puerto serial
sudo usermod -a -G dialout $USER # Requiere reinicio de sesión

# Ejecución de monitoreo
minicom -D /dev/ttyACM0 -b 115200 -c on