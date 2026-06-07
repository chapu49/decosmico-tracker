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

const PORT = process.env.PORT || 3000;
app.listen(PORT, () => {
  console.log(`De Cosmico tracker escuchando en puerto ${PORT}`);
});
