# De Cosmico - Backend de rastreo de antenas Starlink

Servidor simple para el prototipo. Recibe posiciones GPS de los dispositivos Heltec,
las guarda en memoria y las muestra en un mapa.

## Rutas

- `POST /api/location` — el dispositivo envia `{ serial, lat, lon, sats, hdop }`
- `GET /api/locations` — el dashboard lee las posiciones
- `GET /` — el dashboard (mapa)
- `GET /api/health` — chequeo de estado

## Probar localmente (opcional)

```
npm install
npm start
```
Luego abrir http://localhost:3000

## Desplegar en Render

1. Subir esta carpeta a un repositorio de GitHub.
2. En Render: New -> Web Service -> conectar el repo.
3. Configuracion:
   - Build Command: `npm install`
   - Start Command: `npm start`
   - Plan: Free
4. Deploy. Render entrega una URL publica (ej: https://decosmico-tracker.onrender.com)
5. Esa URL es la que se carga en el firmware del dispositivo.

## Nota sobre el prototipo

Esta version guarda los datos en memoria: si el servidor se reinicia, se pierden.
Para produccion (10+ equipos) se agrega una base de datos PostgreSQL.
