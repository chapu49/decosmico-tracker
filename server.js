/*
 * De Cosmico - Backend de rastreo de antenas Starlink
 * Version simple para prototipo: guarda posiciones en memoria (sin base de datos).
 *
 * Rutas:
 *   POST /api/location   -> el Heltec envia: { serial, lat, lon, sats, hdop }
 *   GET  /api/locations  -> el dashboard lee todas las ultimas posiciones + historial
 *   GET  /              -> el dashboard (mapa)
 *   GET  /api/health     -> chequeo de que el servidor vive
 */

const express = require("express");
const path = require("path");

const app = express();
app.use(express.json());
app.use(express.static(path.join(__dirname, "public")));

// ---- Almacenamiento en memoria ----
// ultimaPosicion[serial] = { serial, lat, lon, sats, hdop, recibido }
const ultimaPosicion = {};
// historial[serial] = [ {lat, lon, recibido}, ... ]  (limitado para no llenar la RAM)
const historial = {};
const MAX_HISTORIAL = 500;

// ---- Recibir posicion desde el dispositivo ----
app.post("/api/location", (req, res) => {
  const { serial, lat, lon, sats, hdop } = req.body || {};

  // Validacion basica
  if (!serial || typeof lat !== "number" || typeof lon !== "number") {
    return res.status(400).json({ ok: false, error: "Faltan datos: serial, lat, lon" });
  }

  const punto = {
    serial,
    lat,
    lon,
    sats: sats ?? null,
    hdop: hdop ?? null,
    recibido: new Date().toISOString(),
  };

  ultimaPosicion[serial] = punto;

  if (!historial[serial]) historial[serial] = [];
  historial[serial].push({ lat, lon, recibido: punto.recibido });
  if (historial[serial].length > MAX_HISTORIAL) historial[serial].shift();

  console.log(`[${punto.recibido}] ${serial}: ${lat}, ${lon} (sats:${sats ?? "?"} hdop:${hdop ?? "?"})`);

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

const PORT = process.env.PORT || 3000;
app.listen(PORT, () => {
  console.log(`De Cosmico tracker escuchando en puerto ${PORT}`);
});
