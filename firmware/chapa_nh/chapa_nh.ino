/*
  Chapa NH — Chapa de nombre digital controlada por Bluetooth
  NANOHUMANO LABS

  Placa: LILYGO T-Display S3 (ESP32-S3R8, LCD 1,9" 170x320)

  Requisitos (Arduino IDE):
   - Core "esp32" de Espressif 2.0.14 (el que recomienda LILYGO para esta placa)
   - Librería TFT_eSPI. En TFT_eSPI/User_Setup_Select.h comenta el setup por
     defecto y descomenta:  #include <User_Setups/Setup206_LilyGo_T_Display_S3.h>
   - Placa: "ESP32S3 Dev Module"
       USB CDC On Boot: Enabled
       PSRAM: OPI PSRAM
       Flash Size: 16MB
       Partition Scheme: "16M Flash (3MB APP/9.9MB FATFS)"  (el BLE ocupa mucho)

  Botones:
   - BOOT (GPIO0)  pulsación corta  -> siguiente pantalla
   - KEY  (GPIO14) pulsación corta  -> cambia el brillo
   - KEY  (GPIO14) mantener 2 s     -> activa Bluetooth 3 min para la app

  Ahorro de energía:
   - El WiFi nunca se enciende y el Bluetooth solo durante la ventana de 3 min.
   - CPU a 80 MHz. La imagen se dibuja una vez y no se refresca salvo cambios.
   - Al terminar la ventana de Bluetooth la placa se reinicia para apagar la
     radio del todo. La hora del reloj se conserva en memoria RTC.

  Comandos BLE (texto "CLAVE:valor" en la característica CMD):
   L1, L2, L3  líneas de texto (nombre, apellido, subtítulo)
   MSG         mensaje libre
   BR          brillo 5..255
   SC          pantalla 0=nombre 1=reloj 2=mensaje 3=info
   COL         color de acento en hex (RRGGBB)
   TH          tema 0=oscuro 1=claro
   T           hora local en segundos (epoch)
   OFF         apagar Bluetooth ya
*/

#include <TFT_eSPI.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <sys/time.h>
#include <time.h>
#include "esp_system.h"
#include "esp_attr.h"

// ---------------- Pines T-Display S3 ----------------
#define PIN_LCD_POWER 15   // en batería tiene que estar en HIGH o la pantalla no enciende
#define PIN_LCD_BL    38   // retroiluminación (PWM)
#define PIN_BAT       4    // divisor de tensión de la batería (x2)
#define PIN_BTN_BOOT  0
#define PIN_BTN_KEY   14

#define ROTACION 1         // 1 o 3, según hacia qué lado quieras los botones

// ---------------- Bluetooth ----------------
#define BLE_NOMBRE "NH-Chapa"
#define SVC_UUID   "8f1c0001-2b7a-4e5c-9d3e-6a1b2c3d4e5f"
#define CMD_UUID   "8f1c0002-2b7a-4e5c-9d3e-6a1b2c3d4e5f"
#define CFG_UUID   "8f1c0003-2b7a-4e5c-9d3e-6a1b2c3d4e5f"
#define ST_UUID    "8f1c0004-2b7a-4e5c-9d3e-6a1b2c3d4e5f"
const uint32_t BLE_VENTANA_MS = 180000;   // 3 minutos

// Tipos declarados arriba para que Arduino genere bien los prototipos
struct Glifo { char c; uint8_t marca; };  // marca: 0 nada, 1 aguda, 2 virgulilla, 3 diéresis, 4 grave
struct Boton { uint8_t pin; bool abajo; uint32_t t0; bool largo; };

TFT_eSPI tft;
Preferences prefs;

struct Config {
  String l1, l2, l3, msg;
  uint8_t br;
  uint8_t sc;
  uint8_t th;
  uint32_t col;
} cfg;

enum { SC_NOMBRE = 0, SC_RELOJ, SC_MENSAJE, SC_INFO, NUM_SC };
const uint8_t BRILLOS[] = { 25, 70, 140, 255 };

uint16_t cBg, cFg, cSub, cAcc;

// Estado BLE
bool bleOn = false;
volatile bool bleConn = false;
volatile bool bleConnCambio = false;
uint32_t bleFin = 0, ultEstado = 0;
BLEServer* srv = nullptr;
BLECharacteristic *chCmd = nullptr, *chCfg = nullptr, *chSt = nullptr;

struct Cmd { char txt[200]; };
QueueHandle_t colaCmd;

// Hora conservada entre reinicios por software
RTC_NOINIT_ATTR uint32_t rtcMagic;
RTC_NOINIT_ATTR int64_t rtcEpoch;
const uint32_t MAGIC = 0x4E48C10C;

uint32_t batMv = 0;
int batPct = 0;
uint32_t ultBat = 0;
int ultMin = -1;

// ======================================================================
//  Retroiluminación
// ======================================================================
void initBacklight() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_LCD_BL, 5000, 8);
#else
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_LCD_BL, 0);
#endif
}

void setBrillo(uint8_t v) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_LCD_BL, v);
#else
  ledcWrite(0, v);
#endif
}

void siguienteBrillo() {
  uint8_t nuevo = BRILLOS[0];
  for (uint8_t b : BRILLOS) {
    if (b > cfg.br) { nuevo = b; break; }
  }
  cfg.br = nuevo;
  setBrillo(cfg.br);
}

// ======================================================================
//  Configuración persistente
// ======================================================================
void cargarConfig() {
  prefs.begin("chapa2", true);
  cfg.l1  = prefs.getString("l1", "ISMAEL");
  cfg.l2  = prefs.getString("l2", "");
  cfg.l3  = prefs.getString("l3", "");
  cfg.msg = prefs.getString("msg", "Hola, soy Ismael");
  cfg.br  = prefs.getUChar("br", 140);
  cfg.sc  = prefs.getUChar("sc", SC_NOMBRE);
  cfg.th  = prefs.getUChar("th", 0);
  cfg.col = prefs.getUInt("col", 0x1FA3B5);
  prefs.end();
  if (cfg.sc >= NUM_SC) cfg.sc = SC_NOMBRE;
  if (cfg.br < 5) cfg.br = 5;
}

void guardarConfig() {
  prefs.begin("chapa2", false);
  prefs.putString("l1", cfg.l1);
  prefs.putString("l2", cfg.l2);
  prefs.putString("l3", cfg.l3);
  prefs.putString("msg", cfg.msg);
  prefs.putUChar("br", cfg.br);
  prefs.putUChar("sc", cfg.sc);
  prefs.putUChar("th", cfg.th);
  prefs.putUInt("col", cfg.col);
  prefs.end();
}

uint16_t rgb565(uint32_t c) {
  return tft.color565((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

void aplicarTema() {
  if (cfg.th == 0) { cBg = TFT_BLACK; cFg = TFT_WHITE; cSub = 0xAD55; }
  else             { cBg = TFT_WHITE; cFg = 0x18E3;    cSub = 0x6B4D; }
  cAcc = rgb565(cfg.col);
}

// ======================================================================
//  Batería
// ======================================================================
void leerBateria() {
  uint32_t s = 0;
  for (int i = 0; i < 8; i++) s += analogReadMilliVolts(PIN_BAT);
  batMv = (s / 8) * 2;
  batPct = constrain(map((long)batMv, 3300, 4150, 0, 100), 0, 100);
}

// ======================================================================
//  Texto UTF-8 con tildes y eñes sobre fuentes GFX (que solo traen ASCII)
// ======================================================================

int decodificarUtf8(const String& s, Glifo* out, int maxN) {
  int n = 0;
  const uint8_t* p = (const uint8_t*)s.c_str();
  while (*p && n < maxN) {
    uint8_t b = *p++;
    if (b < 0x80) { out[n++] = { (char)b, 0 }; continue; }
    if (b == 0xC3 && *p) {
      uint8_t b2 = *p++;
      char c = '?'; uint8_t m = 0;
      switch (b2) {
        case 0x81: c = 'A'; m = 1; break;  case 0xA1: c = 'a'; m = 1; break;
        case 0x89: c = 'E'; m = 1; break;  case 0xA9: c = 'e'; m = 1; break;
        case 0x8D: c = 'I'; m = 1; break;  case 0xAD: c = 'i'; m = 1; break;
        case 0x93: c = 'O'; m = 1; break;  case 0xB3: c = 'o'; m = 1; break;
        case 0x9A: c = 'U'; m = 1; break;  case 0xBA: c = 'u'; m = 1; break;
        case 0x91: c = 'N'; m = 2; break;  case 0xB1: c = 'n'; m = 2; break;
        case 0x9C: c = 'U'; m = 3; break;  case 0xBC: c = 'u'; m = 3; break;
        case 0x80: c = 'A'; m = 4; break;  case 0xA0: c = 'a'; m = 4; break;
        case 0x88: c = 'E'; m = 4; break;  case 0xA8: c = 'e'; m = 4; break;
        case 0x92: c = 'O'; m = 4; break;  case 0xB2: c = 'o'; m = 4; break;
        case 0x87: c = 'C'; break;         case 0xA7: c = 'c'; break;
      }
      out[n++] = { c, m };
      continue;
    }
    if (b == 0xC2 && *p) { p++; continue; }        // ¡ ¿ º ...: se omiten
    while ((*p & 0xC0) == 0x80) p++;               // otros caracteres
    out[n++] = { '?', 0 };
  }
  return n;
}

int anchoCar(char c) {
  char s[2] = { c, 0 };
  return tft.textWidth(s);
}

int anchoUtf8(const String& s) {
  Glifo g[128];
  int n = decodificarUtf8(s, g, 128);
  int w = 0;
  for (int i = 0; i < n; i++) w += anchoCar(g[i].c);
  return w;
}

void dibujarMarca(uint8_t m, int cx, int base, int h, int grosor, uint16_t col) {
  for (int t = 0; t < grosor; t++) {
    switch (m) {
      case 1:  // aguda
        tft.drawLine(cx - h / 2 + t, base, cx + h / 2 + t, base - h, col);
        break;
      case 4:  // grave
        tft.drawLine(cx - h / 2 + t, base - h, cx + h / 2 + t, base, col);
        break;
      case 2:  // virgulilla
        tft.drawLine(cx - h, base - h / 4 - t, cx - h / 3, base - h - t, col);
        tft.drawLine(cx - h / 3, base - h - t, cx + h / 3, base - t, col);
        tft.drawLine(cx + h / 3, base - t, cx + h, base - (3 * h) / 4 - t, col);
        break;
      case 3:  // diéresis
        tft.fillCircle(cx - h / 2, base - h / 2, grosor, col);
        tft.fillCircle(cx + h / 2, base - h / 2, grosor, col);
        return;
    }
  }
}

// alinear: 0 izquierda, 1 centro, 2 derecha. y = línea base.
void dibujarUtf8(const String& s, int x, int y, uint8_t alinear, uint16_t col) {
  Glifo g[128];
  int n = decodificarUtf8(s, g, 128);
  int w = 0;
  for (int i = 0; i < n; i++) w += anchoCar(g[i].c);
  if (alinear == 1) x -= w / 2;
  else if (alinear == 2) x -= w;

  tft.setTextDatum(L_BASELINE);
  tft.setTextColor(col);
  int altMay = (tft.fontHeight() * 6) / 10;          // altura aproximada de mayúsculas
  int h = max(3, altMay / 5);
  int grosor = max(1, altMay / 12);

  for (int i = 0; i < n; i++) {
    char s2[2] = { g[i].c, 0 };
    tft.drawString(s2, x, y);
    int wc = anchoCar(g[i].c);
    if (g[i].marca) {
      int altura = isupper((unsigned char)g[i].c) ? altMay : (altMay * 72) / 100;
      int base = y - altura - max(2, altMay / 10);
      dibujarMarca(g[i].marca, x + wc / 2, base, h, grosor, col);
    }
    x += wc;
  }
}

// Parte un texto en líneas que quepan en maxW. 'cabe' = false si sobran líneas.
int partirTexto(const String& s, int maxW, String* out, int maxL, bool& cabe) {
  cabe = true;
  int n = 0;
  String linea = "";
  int i = 0, len = s.length();
  while (i < len) {
    int j = s.indexOf(' ', i);
    if (j < 0) j = len;
    String pal = s.substring(i, j);
    i = j + 1;
    if (!pal.length()) continue;
    String prueba = linea.length() ? linea + " " + pal : pal;
    if (!linea.length() || anchoUtf8(prueba) <= maxW) {
      linea = prueba;
    } else {
      if (n < maxL) out[n++] = linea; else cabe = false;
      linea = pal;
    }
  }
  if (linea.length()) { if (n < maxL) out[n++] = linea; else cabe = false; }
  return n;
}

// ======================================================================
//  Pantallas
// ======================================================================
const GFXfont* FUENTES_NOMBRE[] = {
  &FreeSansBold24pt7b, &FreeSansBold18pt7b, &FreeSansBold12pt7b, &FreeSansBold9pt7b
};

void fondo() {
  tft.fillScreen(cBg);
}

void pantallaNombre() {
  fondo();
  const int x0 = 8, x1 = 312, aw = x1 - x0, cx = 160;

  String lineas[2];
  int n = 0;
  if (cfg.l1.length()) lineas[n++] = cfg.l1;
  if (cfg.l2.length()) lineas[n++] = cfg.l2;

  const GFXfont* fSub = &FreeSans12pt7b;
  tft.setFreeFont(fSub);
  if (cfg.l3.length() && anchoUtf8(cfg.l3) > aw) fSub = &FreeSans9pt7b;
  tft.setFreeFont(fSub);
  int subH = cfg.l3.length() ? tft.fontHeight() : 0;

  int f = 0;
  for (; f < 4; f++) {
    tft.setFreeFont(FUENTES_NOMBRE[f]);
    int w = 0;
    for (int i = 0; i < n; i++) w = max(w, anchoUtf8(lineas[i]));
    if (w <= aw - 6 && n * tft.fontHeight() + subH <= 164) break;
  }
  if (f == 4) f = 3;
  tft.setFreeFont(FUENTES_NOMBRE[f]);
  int lh = tft.fontHeight();

  int y = (170 - (n * lh + subH)) / 2;
  for (int i = 0; i < n; i++) dibujarUtf8(lineas[i], cx, y + lh * i + (lh * 3) / 4, 1, cFg);

  if (subH) {
    tft.setFreeFont(fSub);
    dibujarUtf8(cfg.l3, cx, y + n * lh + (subH * 3) / 4, 1, cSub);
  }
}

const char* DIAS[]  = { "Domingo", "Lunes", "Martes", "Miércoles", "Jueves", "Viernes", "Sábado" };
const char* MESES[] = { "ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic" };

bool horaValida() { return time(nullptr) > 1600000000; }

// Devuelve true si ha redibujado algo
bool pantallaReloj(bool completa) {
  if (completa) { fondo(); ultMin = -1; }

  if (!horaValida()) {
    if (completa) {
      tft.setFreeFont(&FreeSansBold24pt7b);
      dibujarUtf8("--:--", 160, 85, 1, cFg);
      tft.setFreeFont(&FreeSans9pt7b);
      dibujarUtf8("Sincroniza la hora desde la app", 160, 135, 1, cSub);
    }
    return completa;
  }

  time_t ahora = time(nullptr);
  struct tm t;
  gmtime_r(&ahora, &t);            // la app envía ya la hora local
  if (!completa && t.tm_min == ultMin) return false;
  ultMin = t.tm_min;

  char hh[6];
  snprintf(hh, sizeof hh, "%02d:%02d", t.tm_hour, t.tm_min);

  tft.fillRect(10, 8, 310, 116, cBg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(cFg, cBg);
#ifdef LOAD_FONT7
  tft.setTextFont(7);
  tft.setTextSize(2);
  tft.drawString(hh, 160, 68);
#else
  tft.setFreeFont(&FreeSansBold24pt7b);
  tft.setTextSize(2);
  tft.drawString(hh, 160, 68);
#endif
  tft.setTextSize(1);

  char fecha[40];
  snprintf(fecha, sizeof fecha, "%s %d %s", DIAS[t.tm_wday], t.tm_mday, MESES[t.tm_mon]);
  tft.fillRect(10, 124, 310, 44, cBg);
  tft.setFreeFont(&FreeSans12pt7b);
  dibujarUtf8(fecha, 160, 154, 1, cSub);
  return true;
}

void pantallaMensaje() {
  fondo();
  const GFXfont* fs[] = { &FreeSansBold18pt7b, &FreeSansBold12pt7b, &FreeSansBold9pt7b };
  String lin[7];
  int n = 0, lh = 0;
  bool cabe = false;
  for (int k = 0; k < 3; k++) {
    tft.setFreeFont(fs[k]);
    lh = tft.fontHeight();
    int maxL = min(7, 164 / lh);
    n = partirTexto(cfg.msg, 296, lin, maxL, cabe);
    if (cabe) break;
  }
  int y = (170 - n * lh) / 2;
  for (int i = 0; i < n; i++) dibujarUtf8(lin[i], 160, y + lh * i + (lh * 3) / 4, 1, cFg);
}

void pantallaInfo() {
  leerBateria();
  fondo();
  char b[56];
  tft.setFreeFont(&FreeSansBold12pt7b);
  dibujarUtf8("NANOHUMANO LABS", 20, 30, 0, cAcc);

  tft.setFreeFont(&FreeSans9pt7b);
  snprintf(b, sizeof b, "Batería: %.2f V (%d%%)", batMv / 1000.0, batPct);
  dibujarUtf8(b, 20, 62, 0, cFg);
  snprintf(b, sizeof b, "Brillo: %d%%", (cfg.br * 100) / 255);
  dibujarUtf8(b, 20, 86, 0, cFg);
  dibujarUtf8(bleOn ? (bleConn ? "Bluetooth: conectado" : "Bluetooth: esperando la app")
                    : "Bluetooth: apagado", 20, 110, 0, cFg);
  dibujarUtf8("Mantén KEY 2 s para conectar la app", 20, 150, 0, cSub);
}

void dibujarIndicadores() {
  if (bleOn) {
    uint16_t c = bleConn ? TFT_DARKGREEN : TFT_BLUE;
    tft.fillRoundRect(282, 4, 34, 18, 5, c);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, c);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("BT", 299, 13);
  }
  if (batPct < 15 && batMv < 4300) {        // batería baja
    tft.drawRect(288, 150, 24, 13, TFT_RED);
    tft.fillRect(312, 153, 3, 7, TFT_RED);
    tft.fillRect(290, 152, 4, 9, TFT_RED);
  }
}

void dibujarPantalla() {
  tft.setTextSize(1);
  switch (cfg.sc) {
    case SC_NOMBRE:  pantallaNombre();      break;
    case SC_RELOJ:   pantallaReloj(true);   break;
    case SC_MENSAJE: pantallaMensaje();     break;
    case SC_INFO:    pantallaInfo();        break;
  }
  dibujarIndicadores();
}

// ======================================================================
//  Bluetooth
// ======================================================================
class SrvCB : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { bleConn = true; bleConnCambio = true; }
  void onDisconnect(BLEServer*) override {
    bleConn = false; bleConnCambio = true;
    BLEDevice::startAdvertising();
  }
};

class CmdCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    Cmd m;
    auto v = c->getValue();
    size_t n = v.length();
    if (n > sizeof(m.txt) - 1) n = sizeof(m.txt) - 1;
    memcpy(m.txt, v.c_str(), n);
    m.txt[n] = 0;
    xQueueSend(colaCmd, &m, 0);
  }
};

String esc(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

void actualizarCfgBLE() {
  if (!chCfg) return;
  char col[8];
  snprintf(col, sizeof col, "%06lX", (unsigned long)cfg.col);
  String j = "{\"l1\":\"" + esc(cfg.l1) + "\",\"l2\":\"" + esc(cfg.l2) +
             "\",\"l3\":\"" + esc(cfg.l3) + "\",\"msg\":\"" + esc(cfg.msg) +
             "\",\"br\":" + String(cfg.br) + ",\"sc\":" + String(cfg.sc) +
             ",\"th\":" + String(cfg.th) + ",\"col\":\"" + col + "\"}";
  chCfg->setValue((uint8_t*)j.c_str(), j.length());
}

// Estado corto para notificar: "mV,porcentaje,segundosRestantes"
void enviarEstado() {
  if (!chSt) return;
  leerBateria();
  int32_t resto = (int32_t)(bleFin - millis());
  if (resto < 0) resto = 0;
  char b[32];
  snprintf(b, sizeof b, "%lu,%d,%ld", (unsigned long)batMv, batPct, (long)(resto / 1000));
  chSt->setValue((uint8_t*)b, strlen(b));
  if (bleConn) chSt->notify();
}

void iniciarBLE() {
  bleFin = millis() + BLE_VENTANA_MS;
  if (bleOn) return;

  BLEDevice::init(BLE_NOMBRE);
  BLEDevice::setMTU(185);
  srv = BLEDevice::createServer();
  srv->setCallbacks(new SrvCB());

  BLEService* s = srv->createService(SVC_UUID);
  chCmd = s->createCharacteristic(CMD_UUID,
            BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  chCmd->setCallbacks(new CmdCB());
  chCfg = s->createCharacteristic(CFG_UUID, BLECharacteristic::PROPERTY_READ);
  chSt  = s->createCharacteristic(ST_UUID,
            BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  chSt->addDescriptor(new BLE2902());
  s->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SVC_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();

  bleOn = true;
  actualizarCfgBLE();
  enviarEstado();
}

// Para apagar la radio del todo se reinicia la placa, guardando la hora.
void apagarBLE() {
  guardarConfig();
  rtcEpoch = (int64_t)time(nullptr);
  rtcMagic = MAGIC;
  delay(100);
  ESP.restart();
}

void procesarComando(const char* txt) {
  String c(txt);
  c.trim();
  int k = c.indexOf(':');
  String clave = (k < 0) ? c : c.substring(0, k);
  String val   = (k < 0) ? String("") : c.substring(k + 1);
  clave.toUpperCase();
  bool redibujar = false;

  if      (clave == "L1")  { cfg.l1 = val;  redibujar = (cfg.sc == SC_NOMBRE); }
  else if (clave == "L2")  { cfg.l2 = val;  redibujar = (cfg.sc == SC_NOMBRE); }
  else if (clave == "L3")  { cfg.l3 = val;  redibujar = (cfg.sc == SC_NOMBRE); }
  else if (clave == "MSG") { cfg.msg = val; redibujar = (cfg.sc == SC_MENSAJE); }
  else if (clave == "BR")  { cfg.br = constrain(val.toInt(), 5, 255); setBrillo(cfg.br); redibujar = (cfg.sc == SC_INFO); }
  else if (clave == "SC")  { cfg.sc = constrain(val.toInt(), 0, NUM_SC - 1); redibujar = true; }
  else if (clave == "COL") { val.replace("#", ""); cfg.col = strtoul(val.c_str(), nullptr, 16) & 0xFFFFFF; aplicarTema(); redibujar = true; }
  else if (clave == "TH")  { cfg.th = val.toInt() ? 1 : 0; aplicarTema(); redibujar = true; }
  else if (clave == "T") {
    int64_t e = atoll(val.c_str());
    if (e > 1600000000LL) {
      struct timeval tv = { (time_t)e, 0 };
      settimeofday(&tv, nullptr);
      redibujar = (cfg.sc == SC_RELOJ);
    }
  }
  else if (clave == "OFF") { apagarBLE(); return; }
  else if (clave != "GET") return;

  guardarConfig();
  bleFin = millis() + BLE_VENTANA_MS;   // cada orden alarga la ventana
  actualizarCfgBLE();
  if (redibujar) dibujarPantalla();
  enviarEstado();
}

// ======================================================================
//  Botones
// ======================================================================
Boton bBoot = { PIN_BTN_BOOT, false, 0, false };
Boton bKey  = { PIN_BTN_KEY,  false, 0, false };

// 0 = nada, 1 = pulsación corta, 2 = pulsación larga (2 s)
int leerBoton(Boton& b) {
  bool p = digitalRead(b.pin) == LOW;
  uint32_t ahora = millis();
  if (p && !b.abajo) { b.abajo = true; b.t0 = ahora; b.largo = false; return 0; }
  if (p && b.abajo && !b.largo && ahora - b.t0 > 2000) { b.largo = true; return 2; }
  if (!p && b.abajo) {
    b.abajo = false;
    if (!b.largo && ahora - b.t0 > 40) return 1;
  }
  return 0;
}

// ======================================================================
void setup() {
  pinMode(PIN_LCD_POWER, OUTPUT);
  digitalWrite(PIN_LCD_POWER, HIGH);
  pinMode(PIN_BTN_BOOT, INPUT_PULLUP);
  pinMode(PIN_BTN_KEY, INPUT_PULLUP);

  setCpuFrequencyMhz(80);

  // Recuperar la hora tras el reinicio que apaga el Bluetooth
  if (esp_reset_reason() == ESP_RST_SW && rtcMagic == MAGIC && rtcEpoch > 1600000000LL) {
    struct timeval tv = { (time_t)(rtcEpoch + 2), 0 };
    settimeofday(&tv, nullptr);
  }
  rtcMagic = 0;

  colaCmd = xQueueCreate(8, sizeof(Cmd));
  cargarConfig();

  tft.init();
  tft.setRotation(ROTACION);
  aplicarTema();
  tft.fillScreen(cBg);
  initBacklight();
  setBrillo(cfg.br);

  analogReadResolution(12);
  leerBateria();
  dibujarPantalla();
}

void loop() {
  int e = leerBoton(bBoot);
  if (e == 1) {
    cfg.sc = (cfg.sc + 1) % NUM_SC;
    guardarConfig();
    dibujarPantalla();
    if (bleOn) { actualizarCfgBLE(); enviarEstado(); }
  }

  e = leerBoton(bKey);
  if (e == 1) {
    siguienteBrillo();
    guardarConfig();
    if (cfg.sc == SC_INFO) dibujarPantalla();
    if (bleOn) { actualizarCfgBLE(); enviarEstado(); }
  } else if (e == 2) {
    iniciarBLE();
    if (cfg.sc == SC_INFO) dibujarPantalla(); else dibujarIndicadores();
  }

  Cmd m;
  while (xQueueReceive(colaCmd, &m, 0) == pdTRUE) procesarComando(m.txt);

  if (bleOn) {
    if (bleConnCambio) {
      bleConnCambio = false;
      if (cfg.sc == SC_INFO) dibujarPantalla(); else dibujarIndicadores();
    }
    if ((int32_t)(millis() - bleFin) >= 0) apagarBLE();
    if (millis() - ultEstado > 5000) { ultEstado = millis(); enviarEstado(); }
  }

  if (cfg.sc == SC_RELOJ && pantallaReloj(false)) dibujarIndicadores();

  if (millis() - ultBat > 60000) {
    ultBat = millis();
    int antes = batPct;
    leerBateria();
    if ((antes < 15) != (batPct < 15)) dibujarPantalla();
  }

  delay(20);
}
