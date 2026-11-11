# Chapa NH — NANOHUMANO LABS

Chapa de nombre digital con **LILYGO T-Display S3** controlada desde el móvil por Bluetooth.

- **App de control:** https://nanohumano.github.io/chapa-nh/ (Chrome en Android)
- **Firmware:** `firmware/chapa_nh/chapa_nh.ino`

## Compilar (Arduino IDE)
- Core ESP32 de Espressif **2.0.14**
- TFT_eSPI con `Setup206_LilyGo_T_Display_S3.h` activado en `User_Setup_Select.h`
- Placa **ESP32S3 Dev Module** · USB CDC On Boot: Enabled · Flash 16MB · PSRAM OPI · Partición "16M Flash (3MB APP/9.9MB FATFS)"

## Uso (firmware v2)
- **BOOT corto:** cambia de pantalla (nombre, reloj, mensaje, info)
- **BOOT 2 s:** cambia de persona (perfiles, hasta 4)
- **KEY corto:** brillo · **KEY 2 s:** Bluetooth 3 minutos para la app
- En la app: **Conectar** → **NH-Chapa**

## Temas
Normal, Navidad, Halloween, Calendario de Adviento (1 dic – 6 ene) y Automático según la fecha.

## Batería
- Icono naranja al 20 %, rojo y parpadeando al 10 %
- Aviso a pantalla completa cada 5 minutos por debajo del 10 %
- Modo ahorro al 5 % (sin animaciones, brillo bajo)
- A 3,3 V avisa "Me apago" y se apaga para proteger la batería. Se enciende con KEY o con el botón RST.
