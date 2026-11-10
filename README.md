# Chapa NH — NANOHUMANO LABS

Chapa de nombre digital con **LILYGO T-Display S3** controlada desde el móvil por Bluetooth.

- **App de control:** https://nanohumano.github.io/chapa-nh/ (Chrome en Android)
- **Firmware:** `firmware/chapa_nh/chapa_nh.ino`

## Compilar (Arduino IDE)
- Core ESP32 de Espressif **2.0.14**
- TFT_eSPI con `Setup206_LilyGo_T_Display_S3.h` activado en `User_Setup_Select.h`
- Placa **ESP32S3 Dev Module** · USB CDC On Boot: Enabled · Flash 16MB · PSRAM OPI · Partición "16M Flash (3MB APP/9.9MB FATFS)"

## Uso
- **BOOT:** cambia de pantalla (nombre, reloj, mensaje, info)
- **KEY:** cambia el brillo · mantener 2 s activa el Bluetooth 3 minutos
- En la app: **Conectar** → **NH-Chapa**
