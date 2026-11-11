# NH-04 Chapa · NANOHUMANO LABS

![Chapa NH-04](img/portada.png)

Chapa de nombre digital con una **LILYGO T-Display S3**: se cambia desde el móvil por Bluetooth, la pueden compartir varias personas y tiene temas animados de Navidad, Halloween y un calendario de Adviento hasta Reyes. La caja se imprime en 3D y se sujeta a la ropa con imanes.

- 🎬 **Vídeo:** https://youtu.be/ANPzw-ODm4M
- 🖨️ **STL (Printables):** [ENLACE PRINTABLES]
- 📱 **App de control:** https://nanohumano.github.io/chapa-nh/ (Chrome o Edge, en Android o PC con Bluetooth)

## Hardware

| Componente | Detalle |
|---|---|
| Placa | LILYGO T-Display S3 (ESP32-S3R8, LCD 1,9" 170×320, 16 MB flash, 8 MB PSRAM) |
| Batería | LiPo 802540, 800 mAh, conector JST 1,25 mm |
| Imanes | 10 discos de neodimio de 6 × 3 mm (5 en la tapa y 5 en la contraplaca) |
| Caja | 3 piezas en PLA, sin soportes (carpeta `caja/`) |

> Hay una versión AMOLED y otra táctil de la T-Display S3: este firmware y esta caja son para la **versión LCD normal**.
> Antes de conectar la batería, comprueba que el cable rojo va al **+** de la placa.

## Software

- Arduino IDE 2 con el core **esp32 de Espressif 2.0.14** (versiones posteriores no funcionan bien con TFT_eSPI en esta placa)
- Librería **TFT_eSPI**: en `User_Setup_Select.h` comenta `#include <User_Setup.h>` y activa `#include <User_Setups/Setup206_LilyGo_T_Display_S3.h>`
- Librerías del core: BLE, Preferences

### Configuración de la placa

| Opción | Valor |
|---|---|
| Placa | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| Flash Size | 16MB (128Mb) |
| PSRAM | OPI PSRAM (necesaria para las animaciones) |
| Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) |

No hay WiFi ni contraseñas: todo se configura desde la app por Bluetooth.

## Uso

| Botón | Acción |
|---|---|
| BOOT corto | Cambia de pantalla: nombre, reloj, mensaje, info |
| BOOT 2 s | Cambia de persona (hasta 4 perfiles) |
| KEY corto | Brillo |
| KEY 2 s | Activa el Bluetooth 3 minutos para la app |

En la app: **Conectar** → **NH-Chapa**. Desde ahí se cambian nombre, empresa o puesto, tipo de letra, tamaño, tema, brillo y mensaje. Al conectar se sincroniza la hora.

**Temas:** Normal, Navidad, Halloween, Calendario de Adviento (1 de diciembre a 6 de enero) y Automático según la fecha.

**Batería:** icono al 20 %, aviso en pantalla al 10 %, modo ahorro al 5 % y apagado seguro a 3,3 V (se enciende con KEY o RST). Los perfiles se guardan en la placa; la hora se vuelve a sincronizar al conectar la app si la batería se agota del todo.

## Caja

![Caja](img/caja.png)

| Pieza | Impresión |
|---|---|
| `chapa_nh_cuerpo.stl` | Cara de la pantalla sobre la cama |
| `chapa_nh_tapa.stl` | Cara lisa sobre la cama. Tapa corredera con clic |
| `chapa_nh_contraplaca.stl` | Cara lisa sobre la cama. Va por dentro de la ropa |

PLA, capa 0,2 mm, 3 perímetros, relleno 20 %. Los STL se generan con `caja/caja_chapa_nh.py` (Python + numpy), que usa las medidas del modelo 3D oficial de LILYGO.

## Archivos

| Ruta | Contenido |
|---|---|
| `firmware/chapa_nh/chapa_nh.ino` | Firmware para Arduino IDE |
| `index.html` | App web de control (GitHub Pages) |
| `caja/` | STL, generador de la caja e imágenes |

## Licencia

MIT © Nanohumano Labs
