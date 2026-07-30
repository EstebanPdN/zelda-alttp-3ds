# Zelda 3DS E6 experimental

Build experimental para probar optimizaciones de Old 3DS sin tocar el perfil probado de New 3DS.

## Cambios de E6

- Mantiene la imagen normal de E5, sin el escalado agresivo de E4 que hacía ver los píxeles gigantes.
- Old 3DS: sube el presupuesto de Core 1 para el worker PPU de 30% a 70%.
- Old 3DS: permite que el worker PPU dibuje hasta media pantalla, en vez de quedarse limitado a un tercio.
- Pantalla inferior: fuerza un redraw inmediato cuando cambian estados críticos como vida, área/mapa, interior/exterior, dungeon, item equipado, magia, llaves, bombas, flechas o rupees.
- New 3DS: conserva el perfil probado; no cambia la ruta visual de v2.0/E5.

## Qué probar

- En Old 3DS, entrar/salir del castillo debe actualizar el mapa inferior sin esperar 2–3 segundos.
- Al recibir daño, la vida de la pantalla inferior debe cambiar casi al instante.
- Probar modo original primero. Wide sigue siendo más caro, pero E6 intenta acercarse más a 60 FPS.
- En un dump, revisar:
  - `Core 1 PPU budget` debe decir `70%` en Old 3DS.
  - `Average top draw/present`, `Average PPU draw` y `Measured presentation rate`.
  - `Last main PPU segment` y `Last slowest PPU worker` para ver si el split nuevo está balanceando mejor.

## Instalación directa

CIA:

https://github.com/EstebanPdN/zelda-alttp-3ds/raw/E6/Experimental/E6/zelda3-3ds-E6.cia
