/*
 * ============================================================
 *  De Cosmico - Tracker de Antenas Starlink
 *  Heltec Wireless Tracker V1.1 (ESP32-S3 + UC6580 + ST7735)
 * ============================================================
 *  Que hace:
 *   - Muestra splash "De Cosmico" al arrancar
 *   - Lee posicion del GPS (GNSS UC6580)
 *   - Se conecta al WiFi del Starlink
 *   - Envia lat/lon/sats/hdop/vel al servidor cada 10 segundos
 *   - Si no hay WiFi o falla el envio, guarda los puntos en
 *     un buffer y los reenvia cuando vuelve la conexion
 *   - Guarda un LOG interno en flash cada 30 segundos (sobrevive
 *     a los cortes de WiFi). Se descarga/borra por el monitor serie.
 *   - Muestra el estado en vivo en la pantalla
 *
 *  COMANDOS por monitor serie (escribir la letra y Enter):
 *   L  -> volcar (listar) todo el log
 *   B  -> borrar el log
 *   I  -> info: cuantos registros hay
 *
 *  Librerias necesarias (Administrar bibliotecas):
 *   - TinyGPSPlus (Mikal Hart)
 *   - Adafruit ST7735 and ST7789 (+ Adafruit GFX)
 *  (WiFi, HTTPClient y LittleFS ya vienen con el ESP32)
 * ============================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <TinyGPSPlus.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <math.h>
#include <FS.h>
#include <LittleFS.h>

// ===================== CONFIGURACION =====================
// --- Identificacion del equipo ---
const char* SERIAL_ANTENA = "ut30c00518-03220e1c-1833f1e0";

// --- WiFi del Starlink (prototipo: red abierta sin clave) ---
const char* WIFI_SSID = "STARLINK";
const char* WIFI_PASS = "";   // sin contrasena

// --- Servidor en Render ---
const char* SERVIDOR_URL = "https://decosmico-tracker.onrender.com/api/location";

// --- Cada cuanto enviar (milisegundos) ---
const unsigned long INTERVALO_ENVIO = 10000;  // 10 segundos

// --- Cada cuanto guardar en el LOG interno (milisegundos) ---
const unsigned long INTERVALO_LOG = 30000;    // 30 segundos

// --- Nombre del archivo de log en la flash ---
const char* LOG_FILE = "/log.csv";

// --- Tamano del buffer offline (cantidad de puntos guardados) ---
#define MAX_BUFFER 120
// =========================================================

// ---- Pines de la placa (Wireless Tracker V1.1) ----
#define VEXT_CTRL   3
#define GNSS_RX     33
#define GNSS_TX     34
#define TFT_CS      38
#define TFT_DC      40
#define TFT_RST     39
#define TFT_SCLK    41
#define TFT_MOSI    42
#define TFT_BL      21

TinyGPSPlus gps;
SPIClass spiTFT(FSPI);
Adafruit_ST7735 tft = Adafruit_ST7735(&spiTFT, TFT_CS, TFT_DC, TFT_RST);

uint16_t VIOLETA, MAGENTA, AZUL;

// Buffer offline (cola circular simple)
struct Punto { double lat; double lon; int sats; double hdop; double vel; };
Punto buffer[MAX_BUFFER];
int bufCount = 0;

unsigned long ultimoEnvio = 0;
unsigned long ultimoLog = 0;
int enviadosOK = 0;
int registrosLog = 0;
bool fsOK = false;
String ultimoEstado = "Iniciando";

// ---------- LOG en flash (LittleFS) ----------
void initLog() {
  // El "true" formatea si no se puede montar la primera vez
  if (LittleFS.begin(true)) {
    fsOK = true;
    // Si el archivo no existe, escribir el encabezado CSV
    if (!LittleFS.exists(LOG_FILE)) {
      File f = LittleFS.open(LOG_FILE, "w");
      if (f) {
        f.println("fecha_hora_utc,lat,lon,sats,hdop,vel_kmh,wifi,enviado");
        f.close();
      }
    } else {
      // Contar registros existentes (lineas menos el encabezado)
      File f = LittleFS.open(LOG_FILE, "r");
      if (f) {
        while (f.available()) { if (f.read() == '\n') registrosLog++; }
        f.close();
        if (registrosLog > 0) registrosLog--; // descontar encabezado
      }
    }
  } else {
    fsOK = false;
  }
}

// Devuelve la fecha-hora del GPS en texto, o "sin_hora" si no hay fix de tiempo
String horaGPS() {
  if (gps.date.isValid() && gps.time.isValid()) {
    char buf[24];
    sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d",
            gps.date.year(), gps.date.month(), gps.date.day(),
            gps.time.hour(), gps.time.minute(), gps.time.second());
    return String(buf);
  }
  return "sin_hora";
}

void escribirLog(double lat, double lon, int sats, double hdop, double vel, bool wifi, bool enviado) {
  if (!fsOK) return;
  File f = LittleFS.open(LOG_FILE, "a");   // append
  if (!f) return;
  f.print(horaGPS());        f.print(",");
  f.print(lat, 6);           f.print(",");
  f.print(lon, 6);           f.print(",");
  f.print(sats);             f.print(",");
  f.print(hdop, 1);          f.print(",");
  f.print(vel, 1);           f.print(",");
  f.print(wifi ? 1 : 0);     f.print(",");
  f.println(enviado ? 1 : 0);
  f.close();                  // cerrar siempre: evita corrupcion si se corta la energia
  registrosLog++;
}

void volcarLog() {
  if (!fsOK) { Serial.println("FS no disponible"); return; }
  File f = LittleFS.open(LOG_FILE, "r");
  if (!f) { Serial.println("No hay log"); return; }
  Serial.println("===== INICIO LOG =====");
  while (f.available()) Serial.write(f.read());
  f.close();
  Serial.println("===== FIN LOG =====");
}

void borrarLog() {
  if (!fsOK) return;
  LittleFS.remove(LOG_FILE);
  registrosLog = 0;
  // recrear con encabezado
  File f = LittleFS.open(LOG_FILE, "w");
  if (f) { f.println("fecha_hora_utc,lat,lon,sats,hdop,vel_kmh,wifi,enviado"); f.close(); }
  Serial.println("Log borrado.");
}

// Procesar comandos por el monitor serie (L=volcar, B=borrar, I=info)
void procesarComandos() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'L' || c == 'l') volcarLog();
    else if (c == 'B' || c == 'b') borrarLog();
    else if (c == 'I' || c == 'i') {
      Serial.print("Registros en log: ");
      Serial.println(registrosLog);
    }
  }
}

// ---------- Espiral del logo ----------
void dibujarEspiral(int cx, int cy, float escala, uint16_t color) {
  for (int brazo = 0; brazo < 2; brazo++) {
    float offset = brazo * PI;
    float xPrev = cx, yPrev = cy;
    for (float t = 0.15; t < 3.6; t += 0.18) {
      float r = t * escala;
      float ang = t * 1.6 + offset;
      int x = cx + (int)(r * cos(ang));
      int y = cy + (int)(r * sin(ang));
      tft.drawLine(xPrev, yPrev, x, y, color);
      tft.drawLine(xPrev, yPrev + 1, x, y + 1, color);
      xPrev = x; yPrev = y;
    }
  }
  tft.fillCircle(cx, cy, 3, MAGENTA);
}

void mostrarSplash() {
  tft.fillScreen(ST77XX_BLACK);
  int cx = 34, cy = 38;
  tft.drawCircle(cx, cy, 28, AZUL);
  tft.drawCircle(cx, cy, 27, AZUL);
  dibujarEspiral(cx, cy, 7.0, VIOLETA);
  tft.setTextColor(MAGENTA);
  tft.setTextSize(2);
  tft.setCursor(72, 20);
  tft.print("De");
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(72, 40);
  tft.print("COSMICO");
  tft.setTextColor(AZUL);
  tft.setCursor(20, 70);
  tft.print("Tracking System");
}

// ---------- Pantalla de estado en vivo ----------
void mostrarEstado() {
  tft.fillScreen(ST77XX_BLACK);

  // Encabezado de marca
  tft.setTextSize(1);
  tft.setTextColor(MAGENTA);
  tft.setCursor(2, 2);
  tft.print("De ");
  tft.setTextColor(ST77XX_WHITE);
  tft.print("Cosmico");

  // WiFi
  tft.setCursor(2, 16);
  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(ST77XX_GREEN);
    tft.print("WiFi OK");
  } else {
    tft.setTextColor(ST77XX_RED);
    tft.print("WiFi NO");
  }

  // GPS
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(70, 16);
  tft.print("Sat:");
  tft.print(gps.satellites.value());

  // Coordenadas
  tft.setCursor(2, 30);
  if (gps.location.isValid()) {
    tft.setTextColor(ST77XX_CYAN);
    tft.print(gps.location.lat(), 5);
    tft.setCursor(2, 40);
    tft.print(gps.location.lng(), 5);
    // Velocidad en vivo
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(90, 30);
    tft.print(gps.speed.isValid() ? gps.speed.kmph() : 0, 0);
    tft.print("km/h");
  } else {
    tft.setTextColor(ST77XX_RED);
    tft.print("Sin fix GPS");
  }

  // Envios y buffer
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(2, 54);
  tft.print("Env:");
  tft.print(enviadosOK);
  tft.print(" Log:");
  tft.print(registrosLog);
  tft.setCursor(2, 64);
  tft.print("Buffer:");
  tft.print(bufCount);

  // Estado actual
  tft.setTextColor(AZUL);
  tft.setCursor(70, 64);
  tft.print(ultimoEstado);
}

// ---------- Enviar un punto al servidor ----------
bool enviarPunto(const Punto& p) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.begin(SERVIDOR_URL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"serial\":\"" + String(SERIAL_ANTENA) + "\",";
  json += "\"lat\":" + String(p.lat, 6) + ",";
  json += "\"lon\":" + String(p.lon, 6) + ",";
  json += "\"sats\":" + String(p.sats) + ",";
  json += "\"hdop\":" + String(p.hdop, 1) + ",";
  json += "\"vel\":" + String(p.vel, 1);
  json += "}";

  int codigo = http.POST(json);
  http.end();

  if (codigo == 200) {
    enviadosOK++;
    ultimoEstado = "Enviado";
    return true;
  } else {
    ultimoEstado = "Err " + String(codigo);
    return false;
  }
}

// ---------- Guardar en buffer offline ----------
void guardarEnBuffer(const Punto& p) {
  if (bufCount < MAX_BUFFER) {
    buffer[bufCount++] = p;
  } else {
    // buffer lleno: descarta el mas viejo (corre todo una posicion)
    for (int i = 1; i < MAX_BUFFER; i++) buffer[i - 1] = buffer[i];
    buffer[MAX_BUFFER - 1] = p;
  }
}

// ---------- Vaciar el buffer cuando vuelve el WiFi ----------
void vaciarBuffer() {
  while (bufCount > 0 && WiFi.status() == WL_CONNECTED) {
    if (enviarPunto(buffer[0])) {
      for (int i = 1; i < bufCount; i++) buffer[i - 1] = buffer[i];
      bufCount--;
    } else {
      break;  // si falla, dejamos el resto para el proximo intento
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Encender GPS y pantalla
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, HIGH);
  delay(200);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Pantalla
  spiTFT.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.initR(INITR_MINI160x80);
  tft.setRotation(1);
  tft.invertDisplay(true);
  VIOLETA = tft.color565(150, 60, 220);
  MAGENTA = tft.color565(220, 40, 200);
  AZUL    = tft.color565(80, 90, 230);

  mostrarSplash();
  delay(3000);

  // Inicializar log en flash
  initLog();
  Serial.print("Log: ");
  Serial.println(fsOK ? "listo" : "ERROR de FS");

  // GPS
  Serial1.begin(115200, SERIAL_8N1, GNSS_RX, GNSS_TX);

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Conectando a WiFi");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi conectado. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Sin WiFi (seguira intentando)");
  }
}

void loop() {
  // Leer GPS continuamente
  while (Serial1.available() > 0) {
    gps.encode(Serial1.read());
  }

  // Atender comandos del monitor serie (L=volcar, B=borrar, I=info)
  procesarComandos();

  // Reintentar WiFi si se cayo
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long ultimoIntentoWifi = 0;
    if (millis() - ultimoIntentoWifi > 10000) {
      ultimoIntentoWifi = millis();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  }

  // Cada INTERVALO_ENVIO, procesar posicion
  if (millis() - ultimoEnvio >= INTERVALO_ENVIO) {
    ultimoEnvio = millis();

    if (gps.location.isValid()) {
      Punto p;
      p.lat  = gps.location.lat();
      p.lon  = gps.location.lng();
      p.sats = gps.satellites.value();
      p.hdop = gps.hdop.isValid() ? gps.hdop.hdop() : 0;
      p.vel  = gps.speed.isValid() ? gps.speed.kmph() : 0;

      // Primero intentar vaciar lo que haya en buffer
      vaciarBuffer();

      // Luego enviar el punto actual; si falla, al buffer
      if (!enviarPunto(p)) {
        guardarEnBuffer(p);
        ultimoEstado = "Guardado";
      }

      Serial.printf("%.6f, %.6f | sats:%d | enviados:%d | buffer:%d | log:%d\n",
                    p.lat, p.lon, p.sats, enviadosOK, bufCount, registrosLog);
    } else {
      ultimoEstado = "Sin fix";
      Serial.println("Esperando fix GPS...");
    }
  }

  // Cada INTERVALO_LOG, guardar un registro en flash (aunque no haya fix/wifi)
  if (millis() - ultimoLog >= INTERVALO_LOG) {
    ultimoLog = millis();
    bool wifi = (WiFi.status() == WL_CONNECTED);
    if (gps.location.isValid()) {
      double vel = gps.speed.isValid() ? gps.speed.kmph() : 0;
      double hdop = gps.hdop.isValid() ? gps.hdop.hdop() : 0;
      // "enviado" = hubo wifi y el buffer esta vacio (se logro mandar todo)
      bool enviado = wifi && (bufCount == 0);
      escribirLog(gps.location.lat(), gps.location.lng(),
                  gps.satellites.value(), hdop, vel, wifi, enviado);
    } else {
      // Sin fix: igual registramos el momento, con 0s, para ver el hueco
      escribirLog(0, 0, gps.satellites.value(), 0, 0, wifi, false);
    }
  }

  // Actualizar pantalla cada 1 segundo
  static unsigned long ultimaPantalla = 0;
  if (millis() - ultimaPantalla > 1000) {
    ultimaPantalla = millis();
    mostrarEstado();
  }
}
