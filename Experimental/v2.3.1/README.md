# Zelda 3DS v2.3.1 experimental

Build experimental agresiva para Old 3DS. No es release.

## Cambio principal

Esta variante activa en Old 3DS un renderer PPU de media resolución vertical:

- Renderiza líneas alternas de la pantalla superior.
- Duplica la línea anterior para completar la imagen.
- Mantiene la lógica del juego a 60 Hz.
- Mantiene New 3DS fuera de esta ruta experimental.

La idea no es declarar esto como solución final, sino medir si el costo real está dominado por el trabajo por scanline del PPU. Si el FPS sube mucho, el siguiente paso será buscar una versión más fina de esta idea, como dirty lines/caché por tiles o un modo de resolución adaptativa menos visible.

## Qué mirar en el dump

- `Old 3DS half-scanline PPU: enabled`
- `Average PPU draw`
- `Average top draw/present`
- `Measured presentation rate`
- `Measured normal logic rate`

## Instalación directa

CIA:

https://github.com/EstebanPdN/zelda-alttp-3ds/raw/v2.3.1/Experimental/v2.3.1/zelda3-3ds-v2.3.1-experimental.cia
