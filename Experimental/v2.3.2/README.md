# Zelda 3DS v2.3.2 experimental

Build experimental para Old 3DS. No es release.

## Qué cambia frente a v2.3.1

v2.3.2 revierte por completo el experimento half-scanline de v2.3.1.

- Resolución vertical completa restaurada.
- Sin duplicar scanlines.
- Sin reducir resolución ni escalar píxeles para ganar FPS.
- New 3DS sigue usando la ruta estable normal.

## Optimización probada aquí

Esta build intenta mejorar Old 3DS con trabajo real del renderer:

- La paleta RGB del PPU se cachea y solo se recalcula cuando cambia CGRAM o brillo.
- El PPU deja de evaluar sprites en líneas/estados donde OBJ no puede dibujar.
- Se evita limpiar/dibujar subscreen cuando no se usa.
- El color-window usa un fast path cuando los modos equivalen a dibujo normal.
- La escritura final de paleta a ARGB está desenrollada en bloques para reducir overhead por píxel.
- Se mantiene el pacing de lógica a 60 Hz independiente del render, como en la optimización original documentada en `PROCESS.txt`.

## Qué mirar en el dump

- `Measured normal logic rate`: debe mantenerse cerca de 60 Hz.
- `Measured presentation rate`: FPS visual real.
- `Average PPU draw`: este es el cuello de botella principal que esta build intenta bajar.
- `Average bottom work`: debe mantenerse bajo; la pantalla inferior no debe retrasarse varios segundos.
- `Parallel PPU renderer`: en Old 3DS puede aparecer con Core 1 si el sistema lo permite.

## Instalación directa

CIA:

https://github.com/EstebanPdN/zelda-alttp-3ds/raw/v2.3.2/Experimental/v2.3.2/zelda3-3ds-v2.3.2-experimental.cia

Escanea `QR-v2.3.2-github.png` con FBI para instalar.
