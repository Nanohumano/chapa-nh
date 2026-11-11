/*
  Chapa NH v2 — Chapa de nombre digital — NANOHUMANO LABS
  Placa: LILYGO T-Display S3 (ESP32-S3R8, LCD 1,9" 170x320)

  Novedades v2
   - Perfiles de usuario (hasta 4): nombre, empresa, tipo de letra y tamaño.
     Mantener BOOT 2 s cambia de persona (p. ej. Ismael <-> Javi).
   - Temas animados: Normal, Navidad, Halloween, Calendario de Adviento
     (puerta que se abre cada día, Nochebuena, Navidad, Nochevieja y Reyes)
     y Automático (elige el tema según la fecha).
   - Aviso de batería: icono al 20 %, aviso a pantalla completa al 10 %,
     ahorro (sin animación, brillo bajo) al 5 % y apagado seguro a 3,3 V.

  Requisitos (Arduino IDE)
   - Core "esp32" de Espressif 2.0.14
   - TFT_eSPI con Setup206_LilyGo_T_Display_S3.h activado
   - Placa "ESP32S3 Dev Module": USB CDC On Boot Enabled, Flash 16MB,
     PSRAM "OPI PSRAM" (necesaria para las animaciones),
     Partition Scheme "16M Flash (3MB APP/9.9MB FATFS)"

  Botones
   - BOOT corto: siguiente pantalla (nombre, reloj, mensaje, info)
   - BOOT 2 s:   siguiente perfil
   - KEY corto:  brillo
   - KEY 2 s:    Bluetooth 3 minutos para la app

  Comandos BLE ("CLAVE:valor"), los de perfil actúan sobre el perfil activo
   NOM, EMP, LET (0-3), TAM (0 auto, 1 peq, 2 med, 3 grande)
   PERF:n (elegir), ADDP:nombre (añadir), DELP:n (borrar)
   TEMA (0 normal, 1 navidad, 2 halloween, 3 adviento, 4 automático)
   DIA (0 fecha real, 1-31 diciembre, 106 = 6 de enero), ABRIR (repetir puerta)
   MSG, BR (5-255), SC (0-3), COL (RRGGBB), T (hora local epoch), OFF, GET
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
#include "esp_sleep.h"

// ======================================================================
//  Tipos (arriba del todo para que Arduino genere bien los prototipos)
// ======================================================================
struct Glifo { char c; uint8_t marca; };
struct Boton { uint8_t pin; bool abajo; uint32_t t0; bool largo; };
struct Perfil { String nombre; String empresa; uint8_t letra; uint8_t tam; };
struct Copo { float x, y, v, f; uint8_t r; };
struct Chispa { float x, y, vx, vy, vida; uint16_t c; };
struct Estrella { int16_t x, y; uint8_t s; float f; };
struct Cmd { char txt[200]; };
struct Paleta { uint16_t bg1, bg2, a, b, c; const char* nombre; };
struct Caja { int x, y, w, px; };

// RGB 0xRRGGBB -> RGB565 en tiempo de compilación
#define C(rgb) ((uint16_t)((((rgb) >> 8) & 0xF800) | (((rgb) >> 5) & 0x07E0) | (((rgb) >> 3) & 0x001F)))

// ---------------- Pines ----------------
#define PIN_LCD_POWER 15
#define PIN_LCD_BL    38
#define PIN_BAT       4
#define PIN_BTN_BOOT  0
#define PIN_BTN_KEY   14
#define ROTACION      1

// ---------------- Bluetooth ----------------
#define BLE_NOMBRE "NH-Chapa"
#define SVC_UUID   "8f1c0001-2b7a-4e5c-9d3e-6a1b2c3d4e5f"
#define CMD_UUID   "8f1c0002-2b7a-4e5c-9d3e-6a1b2c3d4e5f"
#define CFG_UUID   "8f1c0003-2b7a-4e5c-9d3e-6a1b2c3d4e5f"
#define ST_UUID    "8f1c0004-2b7a-4e5c-9d3e-6a1b2c3d4e5f"
const uint32_t BLE_VENTANA_MS = 180000;

// ---------------- Pantalla ----------------
const int W = 320, H = 170;
TFT_eSPI tft;
TFT_eSprite spr = TFT_eSprite(&tft);
bool hayLienzo = false;

// ---------------- Configuración ----------------
#define MAX_PERF 4
#define LEN_NOM 16
#define LEN_EMP 24
#define LEN_MSG 60
Perfil perf[MAX_PERF];
uint8_t nPerf = 2, act = 0;
String msg;
uint8_t brillo = 140, pantalla = 0, temaSel = 1, diaPrueba = 0;
uint32_t colAcc = 0x1FA3B5;
Preferences prefs;

enum { SC_NOMBRE = 0, SC_RELOJ, SC_MENSAJE, SC_INFO, NUM_SC };
enum { T_NORMAL = 0, T_NAVIDAD, T_HALLOWEEN, T_ADVIENTO, T_AUTO };
const uint8_t BRILLOS[] = { 25, 70, 140, 255 };

// Tipos de letra: [estilo][tamaño 9,12,18,24 pt]
const GFXfont* FUENTE[4][4] = {
  { &FreeSansBold9pt7b,        &FreeSansBold12pt7b,        &FreeSansBold18pt7b,        &FreeSansBold24pt7b },
  { &FreeSerifBold9pt7b,       &FreeSerifBold12pt7b,       &FreeSerifBold18pt7b,       &FreeSerifBold24pt7b },
  { &FreeMonoBold9pt7b,        &FreeMonoBold12pt7b,        &FreeMonoBold18pt7b,        &FreeMonoBold24pt7b },
  { &FreeSansBoldOblique9pt7b, &FreeSansBoldOblique12pt7b, &FreeSansBoldOblique18pt7b, &FreeSansBoldOblique24pt7b }
};

// ---------------- Estado ----------------
bool bleOn = false;
volatile bool bleConn = false, bleConnCambio = false;
uint32_t bleFin = 0, ultEstado = 0;
BLEServer* srv = nullptr;
BLECharacteristic *chCmd = nullptr, *chCfg = nullptr, *chSt = nullptr;
QueueHandle_t colaCmd;

RTC_NOINIT_ATTR uint32_t rtcMagic;
RTC_NOINIT_ATTR int64_t rtcEpoch;
const uint32_t MAGIC = 0x4E48C10D;

uint32_t batMv = 4000, ultBat = 0, ultAvisoBat = 0;
int batPct = 80;
bool cargando = false, ahorro = false;

int ultMin = -1;
uint8_t temaAct = T_NORMAL;
bool animando = false;
uint32_t animT0 = 0, ultFrame = 0;
int diaMostrado = -1;
uint32_t puertaT0 = 0;
uint32_t splashHasta = 0;
uint32_t avisoHasta = 0;

// Animación
#define MAX_COPOS 90
#define MAX_CHISPAS 220
Copo copos[MAX_COPOS]; int nCopos = 0;
Chispa chispas[MAX_CHISPAS]; int nChispas = 0;
Estrella estrellas[60];
float ojoT = -1, ojoX = 0, ojoY = 0, ojoS = 1;
float murX = -999, murY = 0, murF = 0;
float fugX = 0, fugY = 0, fugV = 0;

const Paleta PALETAS[3] = {
  { C(0x081626), C(0x0F2A2A), C(0xE23B3B), C(0x2FBF71), C(0xF5D76E), "rojo y verde" },
  { C(0x06162A), C(0x123050), C(0x9FD8FF), C(0xE8F4FF), C(0xD7E3F2), "hielo" },
  { C(0x140C06), C(0x2B1A0C), C(0xF2B544), C(0xD9663A), C(0xFFF1C1), "oro" }
};
const char* ESCENAS[8] = { "Nieve", "Estrellas fugaces", "Luces", "Arbol", "Regalos", "Muneco de nieve", "Trineo", "Velas" };
const int SITIOS_OJOS[5][2] = { { 300, 95 }, { 150, 153 }, { 235, 153 }, { 95, 20 }, { 205, 20 } };

Boton bBoot = { PIN_BTN_BOOT, false, 0, false };
Boton bKey  = { PIN_BTN_KEY,  false, 0, false };

// ======================================================================
//  Utilidades
// ======================================================================
float frand(float a, float b) { return a + (b - a) * (esp_random() / 4294967295.0f); }
float easeOut(float x) { if (x < 0) x = 0; if (x > 1) x = 1; return 1 - (1 - x) * (1 - x) * (1 - x); }
uint16_t mezcla(uint16_t a, uint16_t b, float f) { if (f < 0) f = 0; if (f > 1) f = 1; return tft.alphaBlend((uint8_t)(f * 255), b, a); }
uint16_t rgb565(uint32_t c) { return tft.color565((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); }
uint16_t gris(float l) { if (l < 0) l = 0; if (l > 1) l = 1; uint8_t v = (uint8_t)(l * 255); return tft.color565(v, v, v); }
Perfil& P() { return perf[act]; }

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
uint8_t brilloReal() { return ahorro ? min((int)brillo, 40) : brillo; }

// ======================================================================
//  Configuración persistente
// ======================================================================
void cargarConfig() {
  prefs.begin("chapa3", true);
  nPerf = prefs.getUChar("np", 2);
  if (nPerf < 1 || nPerf > MAX_PERF) nPerf = 2;
  const char* defN[MAX_PERF] = { "ISMAEL", "JAVI", "NOMBRE", "NOMBRE" };
  char k[4];
  for (int i = 0; i < MAX_PERF; i++) {
    k[1] = '0' + i; k[2] = 0;
    k[0] = 'n'; perf[i].nombre  = prefs.getString(k, defN[i]);
    k[0] = 'e'; perf[i].empresa = prefs.getString(k, "RECEPCIÓN");
    k[0] = 'f'; perf[i].letra   = prefs.getUChar(k, 0) % 4;
    k[0] = 's'; perf[i].tam     = prefs.getUChar(k, 0) % 4;
  }
  act = prefs.getUChar("act", 0); if (act >= nPerf) act = 0;
  msg = prefs.getString("msg", "Hola, ¿en qué puedo ayudarte?");
  brillo = prefs.getUChar("br", 140); if (brillo < 5) brillo = 5;
  pantalla = prefs.getUChar("sc", 0) % NUM_SC;
  temaSel = prefs.getUChar("tema", T_NAVIDAD) % 5;
  diaPrueba = prefs.getUChar("dia", 0);
  colAcc = prefs.getUInt("col", 0x1FA3B5);
  prefs.end();
}

void guardarConfig() {
  prefs.begin("chapa3", false);
  prefs.putUChar("np", nPerf);
  prefs.putUChar("act", act);
  char k[4];
  for (int i = 0; i < MAX_PERF; i++) {
    k[1] = '0' + i; k[2] = 0;
    k[0] = 'n'; prefs.putString(k, perf[i].nombre);
    k[0] = 'e'; prefs.putString(k, perf[i].empresa);
    k[0] = 'f'; prefs.putUChar(k, perf[i].letra);
    k[0] = 's'; prefs.putUChar(k, perf[i].tam);
  }
  prefs.putString("msg", msg);
  prefs.putUChar("br", brillo);
  prefs.putUChar("sc", pantalla);
  prefs.putUChar("tema", temaSel);
  prefs.putUChar("dia", diaPrueba);
  prefs.putUInt("col", colAcc);
  prefs.end();
}

// ======================================================================
//  Batería
// ======================================================================
void leerBateria() {
  uint32_t s = 0;
  for (int i = 0; i < 8; i++) s += analogReadMilliVolts(PIN_BAT);
  batMv = (s / 8) * 2;
  cargando = batMv > 4300;            // con USB el ADC marca de más: lo tomamos como "cargando"
  batPct = constrain(map((long)batMv, 3350, 4150, 0, 100), 0, 100);
}

// ======================================================================
//  Fecha
// ======================================================================
bool horaValida() { return time(nullptr) > 1600000000; }
bool fecha(struct tm& t) { if (!horaValida()) return false; time_t a = time(nullptr); gmtime_r(&a, &t); return true; }

// Código del día de Adviento: 1-31 diciembre, 101-105 enero, 106 Reyes, 0 fuera de temporada
int diaHoy() {
  struct tm t;
  if (!fecha(t)) return 0;
  if (t.tm_mon == 11) return t.tm_mday;
  if (t.tm_mon == 0 && t.tm_mday == 1) return 31;          // Año Nuevo: misma fiesta que Nochevieja
  if (t.tm_mon == 0 && t.tm_mday <= 6) return 100 + t.tm_mday;
  return 0;
}
int diaAdviento() {
  if (diaPrueba) return diaPrueba;
  int d = diaHoy();
  return d ? d : 1;
}
int anyoNuevo() {
  struct tm t;
  if (!fecha(t)) return 2027;
  return (t.tm_mon == 11) ? t.tm_year + 1901 : t.tm_year + 1900;
}
uint8_t temaEfectivo() {
  if (temaSel != T_AUTO) return temaSel;
  struct tm t;
  if (!fecha(t)) return T_NORMAL;
  if (t.tm_mon == 9 && t.tm_mday >= 24) return T_HALLOWEEN;
  if (t.tm_mon == 11 || (t.tm_mon == 0 && t.tm_mday <= 6)) return T_ADVIENTO;
  return T_NORMAL;
}

// ======================================================================
//  Texto UTF-8 con tildes y eñes sobre fuentes GFX (solo traen ASCII)
// ======================================================================
int decodificarUtf8(const String& s, Glifo* out, int maxN) {
  int n = 0;
  const uint8_t* p = (const uint8_t*)s.c_str();
  while (*p && n < maxN) {
    uint8_t b = *p++;
    if (b < 0x80) { out[n].c = (char)b; out[n].marca = 0; n++; continue; }
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
      out[n].c = c; out[n].marca = m; n++;
      continue;
    }
    if (b == 0xC2 && *p) {                               // ¡ ¿ º ª
      uint8_t b2 = *p++;
      if (b2 == 0xA1) { out[n].c = '!'; out[n].marca = 5; n++; }       // ¡ (exclamación invertida)
      else if (b2 == 0xBF) { out[n].c = '?'; out[n].marca = 6; n++; }  // ¿
      continue;
    }
    while ((*p & 0xC0) == 0x80) p++;
    out[n].c = '?'; out[n].marca = 0; n++;
  }
  return n;
}

// Avance real de un carácter. textWidth() de una sola letra devuelve solo la parte
// dibujada (un espacio mide 0), así que se mide "c|" y se resta "|".
int anchoCar(TFT_eSPI& g, char c) { char s[3] = { c, '|', 0 }; return g.textWidth(s) - g.textWidth("|"); }

int anchoUtf8(TFT_eSPI& g, const String& s) {
  Glifo gl[64];
  int n = decodificarUtf8(s, gl, 64), w = 0;
  for (int i = 0; i < n; i++) w += anchoCar(g, gl[i].c);
  return w;
}

void dibujarMarca(TFT_eSPI& g, uint8_t m, int cx, int base, int h, int grosor, uint16_t col) {
  for (int t = 0; t < grosor; t++) {
    switch (m) {
      case 1: g.drawLine(cx - h / 2 + t, base, cx + h / 2 + t, base - h, col); break;
      case 4: g.drawLine(cx - h / 2 + t, base - h, cx + h / 2 + t, base, col); break;
      case 2:
        g.drawLine(cx - h, base - h / 4 - t, cx - h / 3, base - h - t, col);
        g.drawLine(cx - h / 3, base - h - t, cx + h / 3, base - t, col);
        g.drawLine(cx + h / 3, base - t, cx + h, base - (3 * h) / 4 - t, col);
        break;
      case 3:
        g.fillCircle(cx - h / 2, base - h / 2, grosor, col);
        g.fillCircle(cx + h / 2, base - h / 2, grosor, col);
        return;
    }
  }
}

// alinear: 0 izquierda, 1 centro, 2 derecha; y = línea base
void dibujarUtf8(TFT_eSPI& g, const String& s, int x, int y, uint8_t alinear, uint16_t col) {
  Glifo gl[64];
  int n = decodificarUtf8(s, gl, 64), w = 0;
  for (int i = 0; i < n; i++) w += anchoCar(g, gl[i].c);
  if (alinear == 1) x -= w / 2; else if (alinear == 2) x -= w;
  g.setTextDatum(L_BASELINE);
  g.setTextColor(col);
  int altMay = (g.fontHeight() * 6) / 10;
  int h = max(3, altMay / 5), grosor = max(1, altMay / 12);
  for (int i = 0; i < n; i++) {
    int wc = anchoCar(g, gl[i].c);
    if (gl[i].marca == 5 || gl[i].marca == 6) {
      // signo invertido: se dibuja girado 180° a mano con dos rectángulos
      int alto = altMay, cx = x + wc / 2;
      if (gl[i].marca == 5) { g.fillRect(cx - grosor, y - alto + alto / 4, grosor * 2 + 1, (alto * 3) / 4, col); g.fillRect(cx - grosor, y - alto, grosor * 2 + 1, grosor * 2 + 1, col); }
      else { char s2[2] = { '?', 0 }; g.drawString(s2, x, y); }
      x += wc; continue;
    }
    char s2[2] = { gl[i].c, 0 };
    g.drawString(s2, x, y);
    if (gl[i].marca) {
      int alt = isupper((unsigned char)gl[i].c) ? altMay : (altMay * 72) / 100;
      dibujarMarca(g, gl[i].marca, x + wc / 2, y - alt - max(2, altMay / 10), h, grosor, col);
    }
    x += wc;
  }
}

// Nombre y segunda línea del perfil activo, centrados en (cx, cy)
// Niveles de tamaño, de mayor a menor: {fuente (0=9pt .. 3=24pt), aumento}
const uint8_t NIV[6][2] = { { 3, 2 }, { 2, 2 }, { 3, 1 }, { 2, 1 }, { 1, 1 }, { 0, 1 } };
// Por dónde empieza cada tamaño: automático y grande desde el doble de 24 pt,
// mediano desde 24 pt y pequeño desde 18 pt (si no cabe, va bajando)
const uint8_t INICIO_TAM[4] = { 0, 3, 2, 0 };

Caja textoNombre(TFT_eSPI& g, int cx, int cy, int maxW, uint16_t col, uint16_t colSub, const String& linea2) {
  Perfil& p = P();
  int subH = 0;
  int k = INICIO_TAM[p.tam % 4];
  for (; k < 5; k++) {
    g.setTextSize(1); g.setFreeFont(FUENTE[0][NIV[k][0] >= 2 ? 1 : 0]);
    subH = linea2.length() ? g.fontHeight() : 0;
    g.setFreeFont(FUENTE[p.letra][NIV[k][0]]); g.setTextSize(NIV[k][1]);
    int capH = (g.fontHeight() * 62) / 100;
    if (anchoUtf8(g, p.nombre) <= maxW && capH + subH <= H - 12) break;
  }
  g.setFreeFont(FUENTE[p.letra][NIV[k][0]]); g.setTextSize(NIV[k][1]);
  int capH = (g.fontHeight() * 62) / 100;
  int alto = capH + subH;
  int top = cy - alto / 2;
  if (top < 4) top = 4;
  int w = anchoUtf8(g, p.nombre);
  dibujarUtf8(g, p.nombre, cx, top + capH, 1, col);
  g.setTextSize(1);
  if (linea2.length()) {
    g.setFreeFont(FUENTE[0][NIV[k][0] >= 2 ? 1 : 0]);
    dibujarUtf8(g, linea2, cx, top + capH + (subH * 9) / 10, 1, colSub);
  }
  Caja r = { cx - w / 2, top, w, capH };
  return r;
}

// ======================================================================
//  Dibujo básico sobre el lienzo
// ======================================================================
void degradado(uint16_t c1, uint16_t c2) {
  for (int y = 0; y < H; y++) spr.drawFastHLine(0, y, W, mezcla(c1, c2, (float)y / H));
}

void nieve(float dt, int n, float viento, uint16_t col) {
  if (n > MAX_COPOS) n = MAX_COPOS;
  while (nCopos < n) { Copo& c = copos[nCopos++]; c.x = frand(0, W); c.y = frand(-H, H); c.r = (uint8_t)frand(1, 3); c.v = frand(10, 26); c.f = frand(0, 6); }
  nCopos = n;
  for (int i = 0; i < nCopos; i++) {
    Copo& c = copos[i];
    c.y += c.v * dt;
    c.x += sinf(c.f + c.y / 18) * viento + viento * 0.4f;
    if (c.y > H + 3) { c.y = -3; c.x = frand(0, W); }
    if (c.x > W + 3) c.x = -3;
    uint16_t cc = c.r >= 2 ? col : mezcla(TFT_BLACK, col, 0.7f);
    if (c.r <= 1) spr.drawPixel((int)c.x, (int)c.y, cc);
    else spr.fillCircle((int)c.x, (int)c.y, c.r - 1, cc);
  }
}

void cielo(float t, uint16_t top, uint16_t bot) {
  degradado(top, bot);
  for (int i = 0; i < 60; i++) {
    Estrella& e = estrellas[i];
    float l = 0.4f + 0.6f * fabsf(sinf(t * 1.3f + e.f));
    uint16_t c = gris(l);
    if (e.s > 1) spr.fillRect(e.x, e.y, 2, 2, c); else spr.drawPixel(e.x, e.y, c);
  }
}

void estrella5(int x, int y, int r, uint16_t col) {
  float px[10], py[10];
  for (int i = 0; i < 10; i++) {
    float a = -PI / 2 + i * PI / 5, rr = (i % 2) ? r * 0.45f : r;
    px[i] = x + cosf(a) * rr; py[i] = y + sinf(a) * rr;
  }
  for (int i = 0; i < 10; i++) { int j = (i + 1) % 10; spr.fillTriangle(x, y, px[i], py[i], px[j], py[j], col); }
}

void brillo2(int x, int y, int r, uint16_t col) {
  // halo suave: anillos que se mezclan con lo que hay debajo
  for (int k = r; k > 0; k -= 2) {
    float f = 0.25f * (1.0f - (float)k / r);
    for (int a = 0; a < 16; a++) {
      int xx = x + cosf(a * PI / 8) * k, yy = y + sinf(a * PI / 8) * k;
      if (xx >= 0 && xx < W && yy >= 0 && yy < H) spr.drawPixel(xx, yy, mezcla(spr.readPixel(xx, yy), col, f + 0.1f));
    }
  }
}

void bombillas(float t, const uint16_t* cols, int nc, bool abajo) {
  uint16_t cable = C(0x283C28);
  for (int x = 0; x < W; x += 4) {
    spr.drawLine(x, 2 + sinf(x / 22.0f) * 2, x + 4, 2 + sinf((x + 4) / 22.0f) * 2, cable);
    if (abajo) spr.drawLine(x, H - 3 - sinf(x / 22.0f) * 2, x + 4, H - 3 - sinf((x + 4) / 22.0f) * 2, cable);
  }
  int i = 0;
  for (int fila = 0; fila < (abajo ? 2 : 1); fila++) {
    for (int x = 6; x < W; x += 18, i++) {
      int y = fila == 0 ? 7 + sinf(x / 22.0f) * 2 : H - 7 - sinf(x / 22.0f) * 2;
      bool on = ((int)(t * 3) + i) % 3 != 0;
      uint16_t c = cols[i % nc];
      if (on) { spr.fillEllipse(x, y, 4, 5, mezcla(spr.readPixel(x, y), c, 0.35f)); spr.fillEllipse(x, y, 2, 3, c); }
      else spr.fillEllipse(x, y, 2, 3, mezcla(TFT_BLACK, c, 0.3f));
    }
  }
}

void gorro(int x, int y, float s) {
  int b = 12 * s, alto = 20 * s;
  spr.fillTriangle(x - b, y, x + b, y, x + 14 * s, y - alto, C(0xD42A2A));
  spr.fillTriangle(x - b, y, x + 2 * s, y - alto * 0.8f, x + 14 * s, y - alto, C(0xB81F1F));
  spr.fillRoundRect(x - b - 2 * s, y - 2 * s, 2 * b + 4 * s, 5 * s, 2 * s, TFT_WHITE);
  spr.fillCircle(x + 14 * s, y - alto, 3 * s, TFT_WHITE);
}

void arbol(float t, int x, int base, int alto, const Paleta& p, int luces) {
  spr.fillRect(x - 4, base - 8, 8, 8, C(0x5B3A1E));
  for (int i = 0; i < 3; i++) {
    int y0 = base - 6 - i * alto / 3.4f, w = (3 - i) * alto / 3.1f;
    spr.fillTriangle(x - w / 2, y0, x + w / 2, y0, x, y0 - alto / 2.2f, i % 2 ? C(0x1E7A43) : C(0x23904F));
  }
  uint32_t semilla = 7;
  uint16_t cols[3] = { p.a, p.c, C(0x7EC8FF) };
  for (int i = 0; i < 14; i++) {
    semilla = semilla * 1103515245 + 12345; float r1 = ((semilla >> 8) & 0xFFFF) / 65535.0f;
    semilla = semilla * 1103515245 + 12345; float r2 = ((semilla >> 8) & 0xFFFF) / 65535.0f;
    int yy = base - 10 - r1 * alto * 0.75f;
    float ww = (float)(base - yy) / alto;
    int xx = x + (r2 - 0.5f) * alto * 0.62f * (1 - ww * 0.85f);
    bool on = i < luces && sinf(t * 4 + i) > -0.4f;
    spr.fillCircle(xx, yy, 2, on ? cols[i % 3] : C(0x2C4A38));
  }
  int sy = base - 6 - 2 * alto / 3.4f - alto / 2.2f - 1;
  if (luces >= 14) brillo2(x, sy, 10, p.c);
  estrella5(x, sy, 6, p.c);
}

void muneco(float t, int x, int base) {
  spr.fillRect(0, base, W, H - base, C(0xE8F1FF));
  spr.fillCircle(x, base - 16, 16, C(0xF4F8FF));
  spr.fillCircle(x, base - 42, 11, C(0xF4F8FF));
  spr.fillRect(x - 10, base - 58, 20, 3, TFT_BLACK);
  spr.fillRect(x - 7, base - 72, 14, 15, TFT_BLACK);
  spr.fillRect(x - 7, base - 60, 14, 3, C(0xC0392B));
  spr.fillCircle(x - 4, base - 45, 1, TFT_BLACK); spr.fillCircle(x + 4, base - 45, 1, TFT_BLACK);
  spr.fillTriangle(x, base - 41, x - 9, base - 39, x, base - 38, C(0xFF8C1A));
  for (int i = 0; i < 3; i++) spr.fillCircle(x, base - 24 + i * 7, 1, TFT_BLACK);
  uint16_t rama = C(0x6B4423);
  spr.drawLine(x - 13, base - 22, x - 28, base - 34, rama); spr.drawLine(x - 13, base - 23, x - 28, base - 35, rama);
  float a = -0.8f + sinf(t * 4) * 0.5f;
  int ex = x + 12 + cosf(a) * 16, ey = base - 22 + sinf(a) * 16;
  spr.drawLine(x + 12, base - 22, ex, ey, rama); spr.drawLine(x + 12, base - 23, ex, ey - 1, rama);
}

void regalos(float t, const Paleta& p) {
  uint16_t c1[4] = { p.a, p.b, C(0x7EC8FF), p.c }, c2[4] = { p.c, TFT_WHITE, p.a, p.b };
  const float ciclo = 1.4f;
  for (int i = 0; i < 6; i++) {
    int xx = 18 + i * 52 + (i % 2) * 6, sz = 18 + (i % 3) * 5;
    float tt = fmodf(t - i * ciclo * 0.55f, ciclo * 6 * 0.55f + 2);
    if (t - i * ciclo * 0.55f < 0) continue;
    int y = -30 + easeOut(tt / ciclo) * (H - 6 - sz + 30);
    spr.fillRect(xx, y, sz, sz, c1[i % 4]);
    spr.fillRect(xx + sz / 2 - 2, y, 4, sz, c2[i % 4]);
    spr.fillRect(xx, y + sz / 2 - 2, sz, 4, c2[i % 4]);
    spr.fillEllipse(xx + sz / 2 - 4, y - 2, 4, 2, c2[i % 4]);
    spr.fillEllipse(xx + sz / 2 + 4, y - 2, 4, 2, c2[i % 4]);
  }
}

void trineo(float t, int y0, uint16_t col) {
  int x = (int)fmodf(t * 55, W + 200) - 120, y = y0 + sinf(t * 2) * 4;
  for (int i = 0; i < 2; i++) {
    int rx = x + 50 + i * 26, ry = y + sinf(t * 8 + i) * 1.2f;
    spr.fillEllipse(rx, ry, 9, 4, col);
    spr.fillCircle(rx + 9, ry - 5, 3, col);
    spr.drawLine(rx + 9, ry - 7, rx + 7, ry - 13, col); spr.drawLine(rx + 10, ry - 7, rx + 13, ry - 12, col);
    int q = sinf(t * 10 + i) * 3;
    spr.drawLine(rx - 5, ry + 3, rx - 7 + q, ry + 9, col); spr.drawLine(rx + 5, ry + 3, rx + 7 - q, ry + 9, col);
    if (i == 1) spr.fillCircle(rx + 12, ry - 5, 1, C(0xE23B3B));
  }
  spr.drawLine(x + 30, y, x + 41, y, col);
  spr.fillTriangle(x, y - 8, x + 28, y - 8, x + 30, y + 2, col);
  spr.fillTriangle(x, y - 8, x + 30, y + 2, x + 2, y + 2, col);
  spr.drawLine(x - 3, y + 5, x + 33, y + 5, col); spr.drawLine(x + 33, y + 5, x + 35, y + 1, col);
  spr.fillCircle(x + 12, y - 12, 5, C(0xC0392B));
}

void luna(int x, int y, int r) {
  brillo2(x, y, r + 8, C(0xFDF3C4));
  spr.fillCircle(x, y, r, C(0xFDF3C4));
}

void velas(float t, int x0, int base, int encendidas) {
  uint16_t cols[4] = { C(0x7B2FBF), C(0x7B2FBF), C(0xE86AA5), C(0x7B2FBF) };
  spr.fillEllipse(x0 + 33, base + 2, 44, 7, C(0x1F6B3A));
  for (int i = 0; i < 4; i++) {
    int x = x0 + i * 22, h = 34 - i * 3;
    spr.fillRect(x, base - h, 12, h, cols[i]);
    spr.drawFastVLine(x + 6, base - h - 4, 4, C(0x222222));
    if (i < encendidas) {
      float f = 1 + sinf(t * 13 + i * 2) * 0.15f + sinf(t * 7.3f + i) * 0.1f;
      brillo2(x + 6, base - h - 8, 14, C(0xFFC85A));
      spr.fillEllipse(x + 6, base - h - 8, 3, 6 * f, C(0xFFD36B));
      spr.fillEllipse(x + 6, base - h - 7, 1, 3 * f, C(0xFFF6D8));
    }
  }
}

void estrellasFugaces(float dt) {
  if (fugV <= 0) { if (frand(0, 1) < dt * 0.9f) { fugX = frand(40, W); fugY = frand(5, 50); fugV = 1; } return; }
  fugX -= 220 * dt; fugY += 90 * dt; fugV -= dt * 1.4f;
  for (int k = 0; k < 8; k++) {
    float f = (1 - k / 8.0f) * fugV;
    int x = fugX + k * 5, y = fugY - k * 2;
    if (x >= 0 && x < W && y >= 0 && y < H) spr.drawLine(x, y, x + 5, y - 2, mezcla(spr.readPixel(x, y), TFT_WHITE, f));
  }
}

void fuegos(float dt, const uint16_t* cols, int nc) {
  if (frand(0, 1) < dt * 1.6f && nChispas < MAX_CHISPAS - 46) {
    float x = frand(30, W - 30), y = frand(20, 70);
    uint16_t c = cols[(int)frand(0, nc - 0.01f)];
    for (int i = 0; i < 46; i++) {
      float a = frand(0, 6.28f), v = frand(20, 60);
      Chispa& p = chispas[nChispas++]; p.x = x; p.y = y; p.vx = cosf(a) * v; p.vy = sinf(a) * v; p.vida = 1; p.c = c;
    }
  }
  int j = 0;
  for (int i = 0; i < nChispas; i++) {
    Chispa& p = chispas[i];
    p.x += p.vx * dt; p.y += p.vy * dt; p.vy += 30 * dt; p.vx *= 0.985f; p.vida -= dt * 0.8f;
    if (p.vida <= 0) continue;
    if (p.x >= 0 && p.x < W - 1 && p.y >= 0 && p.y < H - 1) spr.fillRect(p.x, p.y, 2, 2, mezcla(spr.readPixel(p.x, p.y), p.c, p.vida));
    chispas[j++] = p;
  }
  nChispas = j;
}

void camello(int x, int y, float f, uint16_t capa) {
  uint16_t s = C(0x0D0716);
  spr.fillEllipse(x, y - 14, 13, 6, s);
  spr.fillEllipse(x - 3, y - 20, 5, 4, s);
  spr.fillTriangle(x + 10, y - 17, x + 17, y - 30, x + 13, y - 11, s);
  spr.fillTriangle(x + 17, y - 30, x + 22, y - 30, x + 22, y - 27, s);
  spr.fillTriangle(x + 17, y - 30, x + 22, y - 27, x + 16, y - 24, s);
  int q = sinf(f) * 3;
  int dx[4] = { -9, -5, 6, 10 }, sg[4] = { 1, -1, -1, 1 };
  for (int i = 0; i < 4; i++) { spr.drawLine(x + dx[i], y - 10, x + dx[i] + q * sg[i], y, s); spr.drawLine(x + dx[i] + 1, y - 10, x + dx[i] + 1 + q * sg[i], y, s); }
  spr.fillTriangle(x - 6, y - 21, x + 2, y - 21, x - 1, y - 33, capa);
  spr.fillRect(x - 3, y - 38, 5, 3, C(0xF5D76E));
  spr.fillCircle(x - 1, y - 33, 2, C(0xD9A37A));
}

void reyes(float t) {
  degradado(C(0x0A0F2E), C(0x2D1B4A));
  for (int i = 0; i < 60; i++) { Estrella& e = estrellas[i]; spr.drawPixel(e.x, e.y * 8 / 10, gris(0.3f + 0.5f * fabsf(sinf(t + e.f)))); }
  int sx = 270, sy = 26;
  for (int k = 0; k < 40; k++) {                       // estela
    float f = 0.45f * (1 - k / 40.0f);
    int x = sx - k * 2.3f, y = sy + k;
    for (int w = -3; w <= 3; w++) if (y + w > 0) spr.drawPixel(x, y + w, mezcla(spr.readPixel(x, y + w), C(0xFFF0B4), f * (1 - abs(w) / 4.0f)));
  }
  brillo2(sx, sy, 16, C(0xFFF2B8));
  estrella5(sx, sy, 7 + sinf(t * 3) * 0.8f, C(0xFFF2B8));
  for (int x = 0; x < W; x++) {                        // dunas
    int y1 = 130 + 8 * sinf(x / 55.0f + 0.6f) - 6 * sinf(x / 23.0f);
    int y2 = 150 + 4 * sinf(x / 40.0f + 1.4f);
    spr.drawFastVLine(x, y1, H - y1, C(0x3B2547));
    spr.drawFastVLine(x, y2, H - y2, C(0x4A2E57));
  }
  uint16_t capas[3] = { C(0xF2C14E), C(0xE8E8E8), C(0xC0392B) };
  for (int i = 0; i < 3; i++) {
    int x = (int)fmodf(t * 14 + i * 46, W + 120) - 60;
    int y = 141 + sinf((x + 60) / 50.0f) * 3;
    camello(x, y, t * 5 + i, capas[i]);
  }
}

// ---------------- Halloween ----------------
void calabaza(float t, int x, int y) {
  for (int dx = -8; dx <= 8; dx += 8) spr.fillEllipse(x + dx, y, 10, 12, C(0xE8741C));
  spr.fillRect(x - 1, y - 15, 3, 5, C(0x3C6B1E));
  float b = 0.75f + sinf(t * 6) * 0.1f + sinf(t * 13) * 0.05f;
  uint16_t luz = mezcla(C(0x8A3A00), C(0xFFD65A), b);
  spr.fillTriangle(x - 9, y - 4, x - 4, y - 4, x - 6, y - 8, luz);
  spr.fillTriangle(x + 9, y - 4, x + 4, y - 4, x + 6, y - 8, luz);
  spr.fillTriangle(x - 10, y + 3, x + 10, y + 3, x, y + 8, luz);
}

void murcielago(int x, int y, float f) {
  int a = sinf(f) * 7;
  spr.fillEllipse(x, y, 3, 4, TFT_BLACK);
  spr.fillTriangle(x - 1, y - 2, x - 15, y - 2 - a, x - 3, y + 2, TFT_BLACK);
  spr.fillTriangle(x - 15, y - 2 - a, x - 7, y + 2 - a / 3, x - 3, y + 2, TFT_BLACK);
  spr.fillTriangle(x + 1, y - 2, x + 15, y - 2 - a, x + 3, y + 2, TFT_BLACK);
  spr.fillTriangle(x + 15, y - 2 - a, x + 7, y + 2 - a / 3, x + 3, y + 2, TFT_BLACK);
  spr.drawPixel(x - 1, y - 2, TFT_RED); spr.drawPixel(x + 1, y - 2, TFT_RED);
}

void ojos(float t) {
  float ab = t < 0.5f ? easeOut(t / 0.5f) : (t > 4.1f ? 1 - easeOut((t - 4.1f) / 0.5f) : 1);
  if (t > 2.6f && t < 2.75f) ab *= 0.1f;                // parpadeo
  float mira = 0;
  if (t > 0.8f && t < 1.6f) mira = -easeOut((t - 0.8f) / 0.4f);
  else if (t >= 1.6f && t < 2.4f) mira = -1 + 2 * easeOut((t - 1.6f) / 0.4f);
  else if (t >= 2.4f && t < 3.4f) mira = 1 - easeOut((t - 2.4f) / 0.5f);
  for (int lado = -1; lado <= 1; lado += 2) {
    int cx = ojoX + lado * 9 * ojoS, cy = ojoY;
    int rx = 7 * ojoS, ry = max(1, (int)(4.2f * ojoS * ab));
    if (ab > 0.05f) brillo2(cx, cy, 10 * ojoS, C(0xFF1A1A));
    spr.fillEllipse(cx, cy, rx, ry, C(0xFF2020));
    if (ab > 0.3f) spr.fillEllipse(cx + mira * 3.5f * ojoS, cy, 1, max(1, (int)(3.2f * ojoS * ab)), C(0x2A0000));
  }
}

void escenaHalloween(float t, float dt) {
  degradado(C(0x1A0D1F), C(0x050207));
  spr.fillCircle(286, 28, 13, C(0xE9E3C9));
  spr.fillCircle(292, 24, 12, C(0x16091A));
  uint16_t tela = C(0x5A5A66);
  for (int i = 0; i < 6; i++) { float a = i * PI / 10; spr.drawLine(0, 0, cosf(a) * 46, sinf(a) * 46, tela); }
  for (int r = 10; r <= 42; r += 10)
    for (int i = 0; i < 5; i++) {
      float a1 = i * PI / 10, a2 = (i + 1) * PI / 10; int r1 = r - (i % 2) * 2, r2 = r - ((i + 1) % 2) * 2;
      spr.drawLine(cosf(a1) * r1, sinf(a1) * r1, cosf(a2) * r2, sinf(a2) * r2, tela);
    }
  calabaza(t, 34, 150);
  if (ojoT < 0) {
    if (frand(0, 1) < dt * 0.6f) { int k = (int)frand(0, 4.99f); ojoX = SITIOS_OJOS[k][0]; ojoY = SITIOS_OJOS[k][1]; ojoS = (k == 0) ? 0.8f : 0.9f; ojoT = 0; }
  } else { ojoT += dt; ojos(ojoT); if (ojoT > 4.6f) ojoT = -1; }
  if (murX < -900) { if (frand(0, 1) < dt * 0.35f) { murX = -20; murY = frand(20, 60); murF = 0; } }
  else { murX += 95 * dt; murF += dt; murcielago(murX, murY + sinf(murF * 5) * 8, murF * 18); if (murX > W + 20) murX = -999; }
  textoNombre(spr, W / 2 + 10, 88, 250, C(0xFF8A1C), C(0xB06CFF), P().empresa);
}

// ---------------- Navidad ----------------
void escenaNavidad(float t, float dt) {
  degradado(C(0x071423), C(0x10283A));
  nieve(dt, 80, 0.3f, TFT_WHITE);
  const uint16_t cols[4] = { C(0xE23B3B), C(0x2FBF71), C(0xF5D76E), C(0x7EC8FF) };
  bombillas(t, cols, 4, true);
  Caja r = textoNombre(spr, W / 2, 90, 280, TFT_WHITE, C(0xF5D76E), P().empresa);
  gorro(r.x + r.px / 4, r.y + 2, r.px / 30.0f);
}

// ---------------- Adviento ----------------
void escenaDia(int d, float t, float dt) {
  int esc = -1, especial = 0;
  if (d == 24 || d == 25 || d == 31 || d == 106) especial = d;
  else if (d > 100) esc = 0;
  else esc = (d - 1) % 8;
  const Paleta& p = (d > 100) ? PALETAS[1] : PALETAS[((d - 1) / 8) % 3];
  int nx = W / 2, mw = 280, cy = 88;
  String l2 = P().empresa;
  const uint16_t navi[4] = { C(0xE23B3B), C(0x2FBF71), C(0xF5D76E), C(0x7EC8FF) };
  if (especial == 24) { cielo(t, C(0x040818), C(0x132449)); luna(270, 30, 12); trineo(t, 40, C(0x04060C)); nieve(dt, 30, 0.2f, TFT_WHITE); l2 = "FELIZ NOCHEBUENA"; }
  else if (especial == 25) { cielo(t, C(0x060A18), C(0x151A33)); fuegos(dt, navi, 4); bombillas(t, navi, 4, false); l2 = "¡FELIZ NAVIDAD!"; }
  else if (especial == 31) { const uint16_t c[4] = { C(0xF5D76E), TFT_WHITE, C(0xC48CFF), C(0x7EC8FF) }; cielo(t, C(0x05040F), C(0x1A1030)); fuegos(dt, c, 4); l2 = String("¡FELIZ ") + anyoNuevo() + "!"; }
  else if (especial == 106) { reyes(t); nx = 140; mw = 230; cy = 64; l2 = "¡FELICES REYES!"; }
  else {
    degradado(p.bg1, p.bg2);
    switch (esc) {
      case 0: nieve(dt, 90, 0.35f, &p == &PALETAS[0] ? TFT_WHITE : p.b); break;
      case 1: cielo(t, p.bg1, p.bg2); estrellasFugaces(dt); break;
      case 2: { const uint16_t c[4] = { p.a, p.b, p.c, C(0x7EC8FF) }; bombillas(t, c, 4, true); } break;
      case 3: arbol(t, 270, 160, 120, p, min(14, (int)(t * 4))); nieve(dt, 25, 0.2f, TFT_WHITE); nx = 125; mw = 210; break;
      case 4: regalos(t, p); break;
      case 5: nieve(dt, 40, 0.3f, TFT_WHITE); muneco(t, 268, 158); nx = 120; mw = 200; break;
      case 6: cielo(t, p.bg1, p.bg2); luna(40, 30, 11); trineo(t, 36, C(0x03050A)); break;
      case 7: velas(t, 226, 156, min(4, (d + 5) / 6)); nx = 112; mw = 190; break;
    }
  }
  textoNombre(spr, nx, cy, mw, TFT_WHITE, especial ? C(0xF5D76E) : p.c, l2);
  // etiqueta con el día
  String et = d == 106 ? "6 ENE" : (d > 100 ? String(d - 100) + " ENE" : String(d) + " DIC");
  spr.setFreeFont(&FreeSansBold9pt7b);
  int w = spr.textWidth(et) + 10;
  spr.fillRoundRect(6, 7, w, 18, 4, C(0x101418));
  spr.setTextDatum(L_BASELINE); spr.setTextColor(p.c);
  spr.drawString(et, 11, 21);
}

void escenaAdviento(float t, float dt) {
  int d = diaAdviento();
  float prog = (millis() - puertaT0) / 2200.0f;
  escenaDia(d, t, dt);
  if (prog >= 1) return;
  int ancho = W * (1 - easeOut((prog - 0.25f) / 0.75f));
  if (ancho <= 0) return;
  spr.fillRect(0, 0, ancho, H, C(0x5A1D1D));
  if (ancho > 14) spr.drawRect(6, 6, ancho - 12, H - 12, C(0xF5D76E));
  if (ancho > 60) {
    uint32_t s = d * 9301 + 49297;
    for (int i = 0; i < 30; i++) { s = s * 1103515245 + 12345; int x = (s >> 8) % ancho; s = s * 1103515245 + 12345; int y = (s >> 8) % H; spr.drawPixel(x, y, C(0x8A4A4A)); }
    String num = d == 106 ? "6" : String(d > 100 ? d - 100 : d);
    spr.setFreeFont(&FreeSansBold24pt7b); spr.setTextSize(2); spr.setTextDatum(MC_DATUM); spr.setTextColor(C(0xF5D76E));
    spr.drawString(num, ancho / 2, H / 2 - 8);
    spr.setTextSize(1); spr.setFreeFont(&FreeSansBold9pt7b);
    spr.drawString(d > 100 ? "ENERO" : "DICIEMBRE", ancho / 2, H / 2 + 38);
  }
  spr.fillCircle(max(ancho - 16, 4), H / 2, 3, C(0xF5D76E));
  for (int k = 0; k < 12; k++) {
    int x = ancho + k;
    if (x < W) spr.drawFastVLine(x, 0, H, mezcla(spr.readPixel(x, H / 2), TFT_BLACK, 0.55f * (1 - k / 12.0f)));
  }
}

// ======================================================================
//  Indicadores y avisos (sobre cualquier destino: pantalla o lienzo)
// ======================================================================
void iconoBateria(TFT_eSPI& g, int x, int y, uint16_t col) {
  g.drawRect(x, y, 22, 11, col);
  g.fillRect(x + 22, y + 3, 2, 5, col);
  int w = constrain(batPct * 18 / 100, 1, 18);
  g.fillRect(x + 2, y + 2, w, 7, col);
}

void dibujarIndicadores(TFT_eSPI& g) {
  if (bleOn) {
    uint16_t c = bleConn ? TFT_DARKGREEN : TFT_BLUE;
    g.fillRoundRect(282, 4, 34, 18, 5, c);
    g.setTextFont(2); g.setTextSize(1); g.setTextColor(TFT_WHITE, c); g.setTextDatum(MC_DATUM);
    g.drawString("BT", 299, 13);
  }
  if (!cargando && batPct <= 20) {
    bool parpadea = batPct <= 10 && (millis() / 500) % 2;
    if (!parpadea) iconoBateria(g, 290, 152, batPct <= 10 ? TFT_RED : TFT_ORANGE);
  }
}

void pantallaAvisoBateria(TFT_eSPI& g, bool critica) {
  g.fillScreen(TFT_BLACK);
  int x = 120, y = 30;
  uint16_t c = critica ? TFT_RED : TFT_ORANGE;
  g.drawRoundRect(x, y, 70, 36, 5, c); g.drawRoundRect(x + 1, y + 1, 68, 34, 4, c);
  g.fillRect(x + 70, y + 11, 5, 14, c);
  g.fillRect(x + 5, y + 5, critica ? 6 : 12, 26, c);
  g.setFreeFont(&FreeSansBold18pt7b);
  dibujarUtf8(g, critica ? "Me apago" : "Batería baja", W / 2, 112, 1, TFT_WHITE);
  g.setFreeFont(&FreeSansBold12pt7b);
  dibujarUtf8(g, critica ? "Cárgame por USB-C" : String(batPct) + "%  ·  cárgame pronto", W / 2, 145, 1, C(0xAAAAAA));
}

// ======================================================================
//  Pantallas estáticas
// ======================================================================
const char* DIAS[]  = { "Domingo", "Lunes", "Martes", "Miércoles", "Jueves", "Viernes", "Sábado" };
const char* MESES[] = { "ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic" };

void pantallaNombreNormal() {
  tft.fillScreen(TFT_BLACK);
  textoNombre(tft, W / 2, 86, 296, TFT_WHITE, rgb565(colAcc), P().empresa);
  dibujarIndicadores(tft);
}

bool pantallaReloj(bool completa) {
  if (completa) { tft.fillScreen(TFT_BLACK); ultMin = -1; }
  if (!horaValida()) {
    if (completa) {
      tft.setFreeFont(&FreeSansBold24pt7b); dibujarUtf8(tft, "--:--", 160, 85, 1, TFT_WHITE);
      tft.setFreeFont(&FreeSans9pt7b); dibujarUtf8(tft, "Sincroniza la hora desde la app", 160, 135, 1, C(0xAAAAAA));
    }
    return completa;
  }
  struct tm t; fecha(t);
  if (!completa && t.tm_min == ultMin) return false;
  ultMin = t.tm_min;
  char hh[6]; snprintf(hh, sizeof hh, "%02d:%02d", t.tm_hour, t.tm_min);
  tft.fillRect(0, 8, W, 116, TFT_BLACK);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(TFT_WHITE, TFT_BLACK);
#ifdef LOAD_FONT7
  tft.setTextFont(7); tft.setTextSize(2); tft.drawString(hh, 160, 68);
#else
  tft.setFreeFont(&FreeSansBold24pt7b); tft.setTextSize(2); tft.drawString(hh, 160, 68);
#endif
  tft.setTextSize(1);
  char f[40]; snprintf(f, sizeof f, "%s %d %s", DIAS[t.tm_wday], t.tm_mday, MESES[t.tm_mon]);
  tft.fillRect(0, 124, W, 44, TFT_BLACK);
  tft.setFreeFont(&FreeSans12pt7b); dibujarUtf8(tft, f, 160, 154, 1, C(0xAAAAAA));
  return true;
}

int partirTexto(const String& s, int maxW, String* out, int maxL, bool& cabe) {
  cabe = true; int n = 0; String linea = ""; int i = 0, len = s.length();
  while (i < len) {
    int j = s.indexOf(' ', i); if (j < 0) j = len;
    String pal = s.substring(i, j); i = j + 1;
    if (!pal.length()) continue;
    String prueba = linea.length() ? linea + " " + pal : pal;
    if (!linea.length() || anchoUtf8(tft, prueba) <= maxW) linea = prueba;
    else { if (n < maxL) out[n++] = linea; else cabe = false; linea = pal; }
  }
  if (linea.length()) { if (n < maxL) out[n++] = linea; else cabe = false; }
  return n;
}

void pantallaMensaje() {
  tft.fillScreen(TFT_BLACK);
  const GFXfont* fs[] = { &FreeSansBold18pt7b, &FreeSansBold12pt7b, &FreeSansBold9pt7b };
  String lin[7]; int n = 0, lh = 0; bool cabe = false;
  for (int k = 0; k < 3; k++) {
    tft.setFreeFont(fs[k]); lh = tft.fontHeight();
    n = partirTexto(msg, 296, lin, min(7, 164 / lh), cabe);
    if (cabe) break;
  }
  int y = (H - n * lh) / 2;
  for (int i = 0; i < n; i++) dibujarUtf8(tft, lin[i], 160, y + lh * i + (lh * 3) / 4, 1, TFT_WHITE);
  dibujarIndicadores(tft);
}

void pantallaInfo() {
  leerBateria();
  tft.fillScreen(TFT_BLACK);
  char b[64];
  tft.setFreeFont(&FreeSansBold12pt7b); dibujarUtf8(tft, "NANOHUMANO LABS", 14, 28, 0, rgb565(colAcc));
  tft.setFreeFont(&FreeSans9pt7b);
  if (cargando) snprintf(b, sizeof b, "Batería: cargando por USB");
  else snprintf(b, sizeof b, "Batería: %.2f V (%d%%)", batMv / 1000.0, batPct);
  dibujarUtf8(tft, b, 14, 56, 0, TFT_WHITE);
  snprintf(b, sizeof b, "Perfil: %d de %d", act + 1, nPerf); dibujarUtf8(tft, b, 14, 78, 0, TFT_WHITE);
  const char* temas[] = { "Normal", "Navidad", "Halloween", "Adviento", "Automático" };
  snprintf(b, sizeof b, "Tema: %s", temas[temaSel]); dibujarUtf8(tft, b, 14, 100, 0, TFT_WHITE);
  dibujarUtf8(tft, bleOn ? (bleConn ? "Bluetooth: conectado" : "Bluetooth: esperando la app") : "Bluetooth: apagado", 14, 122, 0, TFT_WHITE);
  dibujarUtf8(tft, "BOOT 2 s: perfil  ·  KEY 2 s: app", 14, 154, 0, C(0x999999));
}

void splashPerfil() {
  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(&FreeSans9pt7b);
  char b[24]; snprintf(b, sizeof b, "PERFIL %d / %d", act + 1, nPerf);
  dibujarUtf8(tft, b, W / 2, 40, 1, rgb565(colAcc));
  textoNombre(tft, W / 2, 100, 296, TFT_WHITE, C(0xAAAAAA), P().empresa);
}

// ======================================================================
//  Control de pantalla y animación
// ======================================================================
void actualizarModo() {
  uint8_t nuevo = temaEfectivo();
  bool anim = hayLienzo && !ahorro && pantalla == SC_NOMBRE && nuevo != T_NORMAL;
  if (nuevo == T_ADVIENTO) {
    int d = diaAdviento();
    if (d != diaMostrado || temaAct != T_ADVIENTO) { diaMostrado = d; puertaT0 = millis(); }
  }
  if (anim && (!animando || nuevo != temaAct)) { animT0 = millis(); nCopos = 0; nChispas = 0; ojoT = -1; murX = -999; fugV = 0; }
  temaAct = nuevo;
  if (anim != animando) { animando = anim; setCpuFrequencyMhz(anim ? 160 : 80); }
}

void dibujarPantalla() {
  actualizarModo();
  if (avisoHasta > millis()) return;
  if (splashHasta > millis()) { splashPerfil(); return; }
  if (animando) { ultFrame = 0; return; }        // el bucle se encarga
  tft.setTextSize(1);
  switch (pantalla) {
    case SC_NOMBRE:  pantallaNombreNormal(); break;
    case SC_RELOJ:   pantallaReloj(true); dibujarIndicadores(tft); break;
    case SC_MENSAJE: pantallaMensaje(); break;
    case SC_INFO:    pantallaInfo(); dibujarIndicadores(tft); break;
  }
}

void frame() {
  uint32_t ahora = millis();
  float dt = ultFrame ? (ahora - ultFrame) / 1000.0f : 0.05f;
  if (dt > 0.1f) dt = 0.1f;
  ultFrame = ahora;
  float t = (ahora - animT0) / 1000.0f;
  spr.setTextSize(1);
  switch (temaAct) {
    case T_NAVIDAD:   escenaNavidad(t, dt); break;
    case T_HALLOWEEN: escenaHalloween(t, dt); break;
    case T_ADVIENTO:  escenaAdviento(t, dt); break;
    default: spr.fillSprite(TFT_BLACK); break;
  }
  dibujarIndicadores(spr);
  spr.pushSprite(0, 0);
}

// ======================================================================
//  Bluetooth
// ======================================================================
class SrvCB : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { bleConn = true; bleConnCambio = true; }
  void onDisconnect(BLEServer*) override { bleConn = false; bleConnCambio = true; BLEDevice::startAdvertising(); }
};
class CmdCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    Cmd m; auto v = c->getValue(); size_t n = v.length();
    if (n > sizeof(m.txt) - 1) n = sizeof(m.txt) - 1;
    memcpy(m.txt, v.c_str(), n); m.txt[n] = 0;
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
  char col[8]; snprintf(col, sizeof col, "%06lX", (unsigned long)colAcc);
  String j = "{\"v\":2,\"perf\":[";
  for (int i = 0; i < nPerf; i++) {
    if (i) j += ",";
    j += "{\"n\":\"" + esc(perf[i].nombre) + "\",\"e\":\"" + esc(perf[i].empresa) + "\",\"f\":" + perf[i].letra + ",\"s\":" + perf[i].tam + "}";
  }
  j += "],\"act\":" + String(act) + ",\"tema\":" + temaSel + ",\"dia\":" + diaPrueba + ",\"hoy\":" + diaHoy() +
       ",\"msg\":\"" + esc(msg) + "\",\"br\":" + brillo + ",\"sc\":" + pantalla + ",\"col\":\"" + col + "\"}";
  chCfg->setValue((uint8_t*)j.c_str(), j.length());
}

void enviarEstado() {
  if (!chSt) return;
  leerBateria();
  int32_t resto = (int32_t)(bleFin - millis()); if (resto < 0) resto = 0;
  char b[40]; snprintf(b, sizeof b, "%lu,%d,%ld,%d", (unsigned long)batMv, batPct, (long)(resto / 1000), cargando ? 1 : 0);
  chSt->setValue((uint8_t*)b, strlen(b));
  if (bleConn) chSt->notify();
}

void iniciarBLE() {
  bleFin = millis() + BLE_VENTANA_MS;
  if (bleOn) return;
  BLEDevice::init(BLE_NOMBRE);
  BLEDevice::setMTU(247);
  srv = BLEDevice::createServer(); srv->setCallbacks(new SrvCB());
  BLEService* s = srv->createService(SVC_UUID);
  chCmd = s->createCharacteristic(CMD_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  chCmd->setCallbacks(new CmdCB());
  chCfg = s->createCharacteristic(CFG_UUID, BLECharacteristic::PROPERTY_READ);
  chSt  = s->createCharacteristic(ST_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  chSt->addDescriptor(new BLE2902());
  s->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SVC_UUID); adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  bleOn = true;
  actualizarCfgBLE(); enviarEstado();
}

void reiniciarConservandoHora() {
  guardarConfig();
  rtcEpoch = (int64_t)time(nullptr); rtcMagic = MAGIC;
  delay(100); ESP.restart();
}

String limitar(const String& s, int n) {
  // corta sin partir un carácter UTF-8
  if ((int)s.length() <= n) return s;
  int k = n; while (k > 0 && ((uint8_t)s[k] & 0xC0) == 0x80) k--;
  return s.substring(0, k);
}

void procesarComando(const char* txt) {
  String c(txt); c.trim();
  int k = c.indexOf(':');
  String clave = (k < 0) ? c : c.substring(0, k);
  String val = (k < 0) ? String("") : c.substring(k + 1);
  clave.toUpperCase();
  bool redibujar = true;

  if (clave == "NOM" || clave == "L1") P().nombre = limitar(val, LEN_NOM);
  else if (clave == "EMP")  P().empresa = limitar(val, LEN_EMP);
  else if (clave == "LET")  P().letra = val.toInt() % 4;
  else if (clave == "TAM")  P().tam = val.toInt() % 4;
  else if (clave == "PERF") { int n = val.toInt(); if (n >= 0 && n < nPerf) { act = n; splashHasta = millis() + 900; } }
  else if (clave == "ADDP") { if (nPerf < MAX_PERF) { perf[nPerf].nombre = limitar(val.length() ? val : String("NOMBRE"), LEN_NOM); perf[nPerf].empresa = P().empresa; perf[nPerf].letra = 0; perf[nPerf].tam = 0; act = nPerf++; } }
  else if (clave == "DELP") {
    int n = val.toInt();
    if (nPerf > 1 && n >= 0 && n < nPerf) { for (int i = n; i < nPerf - 1; i++) perf[i] = perf[i + 1]; nPerf--; if (act >= nPerf) act = nPerf - 1; }
  }
  else if (clave == "TEMA") temaSel = val.toInt() % 5;
  else if (clave == "DIA")  { int d = val.toInt(); diaPrueba = (d >= 1 && d <= 31) || d == 106 ? d : 0; diaMostrado = -1; }
  else if (clave == "ABRIR") { puertaT0 = millis(); redibujar = false; }
  else if (clave == "MSG")  msg = limitar(val, LEN_MSG);
  else if (clave == "BR")   { brillo = constrain(val.toInt(), 5, 255); setBrillo(brilloReal()); redibujar = (pantalla == SC_INFO) && !animando; }
  else if (clave == "SC")   pantalla = constrain(val.toInt(), 0, NUM_SC - 1);
  else if (clave == "COL")  { val.replace("#", ""); colAcc = strtoul(val.c_str(), nullptr, 16) & 0xFFFFFF; }
  else if (clave == "T")    { int64_t e = atoll(val.c_str()); if (e > 1600000000LL) { struct timeval tv = { (time_t)e, 0 }; settimeofday(&tv, nullptr); diaMostrado = -1; } }
  else if (clave == "OFF")  { reiniciarConservandoHora(); return; }
  else if (clave != "GET")  return;
  else redibujar = false;

  guardarConfig();
  bleFin = millis() + BLE_VENTANA_MS;
  actualizarCfgBLE();
  if (redibujar) dibujarPantalla();
  enviarEstado();
}

// ======================================================================
//  Botones
// ======================================================================
int leerBoton(Boton& b) {
  bool p = digitalRead(b.pin) == LOW;
  uint32_t ahora = millis();
  if (p && !b.abajo) { b.abajo = true; b.t0 = ahora; b.largo = false; return 0; }
  if (p && b.abajo && !b.largo && ahora - b.t0 > 2000) { b.largo = true; return 2; }
  if (!p && b.abajo) { b.abajo = false; if (!b.largo && ahora - b.t0 > 40) return 1; }
  return 0;
}

// ======================================================================
//  Batería: avisos y protección
// ======================================================================
void vigilarBateria() {
  if (millis() - ultBat < 20000) return;
  ultBat = millis();
  leerBateria();
  if (cargando) { if (ahorro) { ahorro = false; setBrillo(brilloReal()); dibujarPantalla(); } return; }
  if (batMv < 3300) {                                // apagado seguro para no dañar la LiPo
    pantallaAvisoBateria(tft, true);
    delay(4000);
    setBrillo(0);
    digitalWrite(PIN_LCD_POWER, LOW);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_KEY, 0);   // KEY la vuelve a encender (cuando esté cargada)
    esp_deep_sleep_start();
  }
  bool nuevoAhorro = batPct <= 5;
  if (nuevoAhorro != ahorro) { ahorro = nuevoAhorro; setBrillo(brilloReal()); dibujarPantalla(); }
  if (batPct <= 10 && millis() - ultAvisoBat > 300000) {       // cada 5 minutos
    ultAvisoBat = millis();
    pantallaAvisoBateria(tft, false);
    avisoHasta = millis() + 3000;
  }
}

// ======================================================================
void setup() {
  pinMode(PIN_LCD_POWER, OUTPUT); digitalWrite(PIN_LCD_POWER, HIGH);
  pinMode(PIN_BTN_BOOT, INPUT_PULLUP); pinMode(PIN_BTN_KEY, INPUT_PULLUP);
  setCpuFrequencyMhz(80);

  if (esp_reset_reason() == ESP_RST_SW && rtcMagic == MAGIC && rtcEpoch > 1600000000LL) {
    struct timeval tv = { (time_t)(rtcEpoch + 2), 0 }; settimeofday(&tv, nullptr);
  }
  rtcMagic = 0;

  colaCmd = xQueueCreate(8, sizeof(Cmd));
  cargarConfig();

  tft.init(); tft.setRotation(ROTACION); tft.fillScreen(TFT_BLACK);
  initBacklight();

  spr.setColorDepth(16);
  spr.setAttribute(PSRAM_ENABLE, 1);
  hayLienzo = spr.createSprite(W, H) != nullptr;

  for (int i = 0; i < 60; i++) { estrellas[i].x = frand(0, W); estrellas[i].y = frand(0, H * 0.7f); estrellas[i].s = frand(0, 1) < 0.3f ? 2 : 1; estrellas[i].f = frand(0, 6); }

  analogReadResolution(12);
  leerBateria();
  ahorro = !cargando && batPct <= 5;
  setBrillo(brilloReal());
  dibujarPantalla();
}

void loop() {
  int e = leerBoton(bBoot);
  if (e == 1) {
    pantalla = (pantalla + 1) % NUM_SC; guardarConfig(); dibujarPantalla();
    if (bleOn) { actualizarCfgBLE(); enviarEstado(); }
  } else if (e == 2) {
    act = (act + 1) % nPerf; guardarConfig();
    splashHasta = millis() + 900; dibujarPantalla();
    if (bleOn) { actualizarCfgBLE(); enviarEstado(); }
  }

  e = leerBoton(bKey);
  if (e == 1) {
    uint8_t nuevo = BRILLOS[0];
    for (uint8_t b : BRILLOS) if (b > brillo) { nuevo = b; break; }
    brillo = nuevo; setBrillo(brilloReal()); guardarConfig();
    if (pantalla == SC_INFO && !animando) dibujarPantalla();
    if (bleOn) { actualizarCfgBLE(); enviarEstado(); }
  } else if (e == 2) {
    iniciarBLE();
    if (!animando) dibujarPantalla();
  }

  Cmd m;
  while (xQueueReceive(colaCmd, &m, 0) == pdTRUE) procesarComando(m.txt);

  if (bleOn) {
    if (bleConnCambio) { bleConnCambio = false; if (!animando) dibujarPantalla(); }
    if ((int32_t)(millis() - bleFin) >= 0) reiniciarConservandoHora();
    if (millis() - ultEstado > 5000) { ultEstado = millis(); enviarEstado(); }
  }

  vigilarBateria();

  // fin del aviso o del cartel de perfil: volver a la pantalla normal
  static bool enPausa = false;
  bool pausa = avisoHasta > millis() || splashHasta > millis();
  if (enPausa && !pausa) dibujarPantalla();
  enPausa = pausa;

  static uint32_t ultModo = 0;
  if (millis() - ultModo > 30000) { ultModo = millis(); uint8_t antes = temaAct; bool a = animando; actualizarModo(); if (antes != temaAct || a != animando) dibujarPantalla(); }

  if (!pausa) {
    if (animando) { if (millis() - ultFrame >= 50) frame(); }
    else if (pantalla == SC_RELOJ) { if (pantallaReloj(false)) dibujarIndicadores(tft); }
    else if (batPct <= 10 && !cargando) {               // que parpadee el icono
      static uint32_t ultParp = 0;
      if (millis() - ultParp > 500) { ultParp = millis(); tft.fillRect(288, 150, 28, 15, TFT_BLACK); dibujarIndicadores(tft); }
    }
  }

  delay(animando ? 2 : 20);
}
