/*
 * De Cosmico - Backend de rastreo de antenas Starlink
 * Version simple para prototipo: guarda posiciones en memoria (sin base de datos).
 *
 * Rutas:
 *   POST /api/location        -> el Heltec envia: { serial, lat, lon, sats, hdop, vel }
 *   GET  /api/locations       -> el dashboard lee las ultimas posiciones + historial
 *   GET  /                    -> el dashboard (mapa)
 *   GET  /api/health          -> chequeo de que el servidor vive
 *   POST /api/clear           -> limpia historial
 *   GET  /api/firmware/version-> el equipo consulta la version OTA disponible
 *   GET  /api/firmware/bin    -> el equipo descarga el firmware nuevo
 */

const express = require("express");
const path = require("path");
const fs = require("fs");

const app = express();
app.use(express.json());
app.use(express.static(path.join(__dirname, "public")));

// ---- OTA: version de firmware disponible ----
// Subir el archivo firmware.bin a la carpeta /firmware y actualizar este numero.
// El equipo compara su version con esta: si la del servidor es mayor, se actualiza.
const FIRMWARE_VERSION = 6;
const FIRMWARE_PATH = path.join(__dirname, "firmware", "firmware.bin");

// ---- Almacenamiento en memoria ----
// ultimaPosicion[serial] = { serial, lat, lon, sats, hdop, recibido }
const ultimaPosicion = {};
// historial[serial] = [ {lat, lon, recibido}, ... ]  (limitado para no llenar la RAM)
const historial = {};
const MAX_HISTORIAL = 500;

// ---- Recibir posicion desde el dispositivo ----
app.post("/api/location", (req, res) => {
  const { serial, lat, lon, sats, hdop, vel } = req.body || {};

  // Validacion basica
  if (!serial || typeof lat !== "number" || typeof lon !== "number") {
    return res.status(400).json({ ok: false, error: "Faltan datos: serial, lat, lon" });
  }

  const velActual = typeof vel === "number" ? vel : null;

  // Velocidad maxima por dispositivo
  if (velActual !== null) {
    const previaMax = (ultimaPosicion[serial] && ultimaPosicion[serial].velMax) || 0;
    var velMax = Math.max(previaMax, velActual);
  } else {
    var velMax = (ultimaPosicion[serial] && ultimaPosicion[serial].velMax) || null;
  }

  const punto = {
    serial,
    lat,
    lon,
    sats: sats ?? null,
    hdop: hdop ?? null,
    vel: velActual,
    velMax: velMax,
    recibido: new Date().toISOString(),
  };

  ultimaPosicion[serial] = punto;

  if (!historial[serial]) historial[serial] = [];
  historial[serial].push({ lat, lon, recibido: punto.recibido });
  if (historial[serial].length > MAX_HISTORIAL) historial[serial].shift();

  console.log(`[${punto.recibido}] ${serial}: ${lat}, ${lon} (sats:${sats ?? "?"} hdop:${hdop ?? "?"} vel:${velActual ?? "?"}km/h)`);

  res.json({ ok: true });
});

// ---- Entregar posiciones al dashboard ----
app.get("/api/locations", (req, res) => {
  res.json({
    dispositivos: Object.values(ultimaPosicion),
    historial,
  });
});

// ---- Healthcheck ----
app.get("/api/health", (req, res) => {
  res.json({ ok: true, dispositivos: Object.keys(ultimaPosicion).length });
});

// ---- Limpiar historial (borra trayectorias y posiciones) ----
// Borra todo, o solo un serial con ?serial=XXX
app.post("/api/clear", (req, res) => {
  const serial = (req.query.serial || (req.body && req.body.serial)) || null;
  if (serial) {
    delete historial[serial];
    delete ultimaPosicion[serial];
    console.log(`Historial borrado para ${serial}`);
  } else {
    for (const k in historial) delete historial[k];
    for (const k in ultimaPosicion) delete ultimaPosicion[k];
    console.log("Historial completo borrado");
  }
  res.json({ ok: true });
});

// ---- OTA: el equipo consulta que version hay disponible ----
app.get("/api/firmware/version", (req, res) => {
  const existe = fs.existsSync(FIRMWARE_PATH);
  res.json({
    version: FIRMWARE_VERSION,
    disponible: existe,
    tam: existe ? fs.statSync(FIRMWARE_PATH).size : 0,
  });
});

// ---- OTA: el equipo descarga el binario ----
app.get("/api/firmware/bin", (req, res) => {
  if (!fs.existsSync(FIRMWARE_PATH)) {
    return res.status(404).send("No hay firmware cargado");
  }
  const stat = fs.statSync(FIRMWARE_PATH);
  res.setHeader("Content-Type", "application/octet-stream");
  res.setHeader("Content-Length", stat.size);   // httpUpdate del ESP32 necesita el tamano
  res.setHeader("Content-Disposition", "attachment; filename=firmware.bin");
  fs.createReadStream(FIRMWARE_PATH).pipe(res);
});

const PORT = process.env.PORT || 3000;
app.listen(PORT, () => {
  console.log(`De Cosmico tracker escuchando en puerto ${PORT}`);
  console.log(`Firmware OTA: version ${FIRMWARE_VERSION}, archivo ${fs.existsSync(FIRMWARE_PATH) ? "presente" : "AUSENTE"}`);
});
