# Zelda 3DS v2.3.1 experimental

Build experimental para diagnosticar y corregir el black screen reportado en el issue #3.
No es release.

## Qué corrige

El log del usuario mostraba este patrón:

- `Top presenter: PICA200 RGB565 ... Core 1 PPU budget=0%`
- `PPU workers: Core 1=enabled, Core 2=enabled`
- después, black screen.

Eso significa que la app estaba creando un worker de PPU en Core 1 aunque el sistema había devuelto 0% de presupuesto de CPU para ese core. En una New 3DS LL eso podía dejar el hilo principal esperando para siempre a un worker que no recibía tiempo de CPU.

Esta build:

- Deshabilita el worker de Core 1 si el presupuesto real es 0%.
- Mantiene el worker de Core 2 en New 3DS cuando está disponible.
- Evita el bloqueo que ocurría justo después de `PPU workers: Core 1=enabled, Core 2=enabled`.
- Conserva la resolución completa; no usa half-scanline ni reducción visual.

## Qué mirar en `runtime.log`

En la consola que reportaba el bug, ahora debería aparecer algo parecido a:

`Core 1 PPU budget request: wanted=80% actual=80% get=0x00000000`

y después:

`PPU workers: Core 1=enabled, Core 2=enabled`

Si todos los intentos devuelven `actual=0%`, la build desactiva Core 1 para no congelarse y deja registrado:

`Core 1 PPU budget unavailable; disabling Core 1 worker`

## Instalación directa

CIA:

https://github.com/EstebanPdN/zelda-alttp-3ds/raw/v2.3.1/Experimental/v2.3.1/zelda3-3ds-v2.3.1-experimental.cia

Escanea `QR-v2.3.1-github.png` con FBI para instalar.
