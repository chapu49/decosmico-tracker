# Carpeta de firmware OTA

Aca va el binario que los equipos descargan por OTA.

## Como lanzar una actualizacion

1. En Arduino IDE, compilar el firmware nuevo: **Sketch -> Export Compiled Binary**
   (genera un archivo .bin en la carpeta del sketch).
2. Renombrar ese .bin a **firmware.bin** y copiarlo a esta carpeta (`firmware/`).
3. En `server.js`, subir el numero de `FIRMWARE_VERSION` (ej: de 5 a 6).
   IMPORTANTE: el numero debe coincidir con el `FW_VERSION` que pusiste dentro del firmware nuevo.
4. Hacer commit y push. Render redespliega.
5. Los equipos, al chequear (al arrancar o cada 24h), ven la version nueva y se actualizan solos.

## Reglas de seguridad

- Probar SIEMPRE el firmware nuevo en el equipo de pruebas (canario) antes de subir la version.
- El binario correcto es el de la app (firmware.bin), NO el .merged.bin ni el bootloader.
- Si el equipo no logra conectar WiFi tras actualizar, hace rollback solo a la version anterior.
