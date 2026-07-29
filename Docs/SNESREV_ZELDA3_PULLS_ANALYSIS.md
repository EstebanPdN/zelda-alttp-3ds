# Análisis de PRs/issues de snesrev/zelda3 sobre widescreen

Fecha de revisión: 2026-07-29.

Repositorio upstream: <https://github.com/snesrev/zelda3>

## Pull requests relevantes

### PR #314: `[WIP] Ultrawide`

URL: <https://github.com/snesrev/zelda3/pull/314>

Estado visto: abierto.

Este es el intento upstream más cercano al problema. Añade soporte 32:9 y propone un sistema de `extTilemap` para superar el tilemap SNES visible normal. Puntos importantes del PR:

- amplía `extended_aspect_ratio` de `uint8` a `uint16`, porque 32:9 necesita más de 255 px extra;
- sube `kPpuExtraLeftRight` para buffers más anchos;
- añade almacenamiento de tilemap extendido fuera del VRAM ring buffer normal;
- distingue entre scroll del PPU de 10 bits y scroll completo del juego;
- añade una iteración posterior para quitar junk tiles durante transiciones.

Lección para este port 3DS:

- el problema horizontal no es solo aspect ratio; es tilemap + scroll + transición;
- copiar todo `extTilemap` sería más invasivo que lo necesario para 400 px en 3DS;
- el PR sigue WIP, así que no debe importarse entero como si fuera una solución cerrada;
- sí confirma que transiciones y tilemap stale/junk son el punto delicado.

### PR #90: `Add support for 18:9 aspect ratio`

URL: <https://github.com/snesrev/zelda3/pull/90>

Estado visto: merged.

Este PR es la base de soporte de ratios más anchos en upstream. Sirve para entender el modelo de `ExtendedAspectRatio`, pero no resuelve la regla de cámara “parar antes del borde” del overworld.

Lección para este port 3DS:

- usar `extended_aspect_ratio` y extra side space está bien;
- no basta para arreglar bordes negros/glitches al tocar límites del mapa.

### PR #71: `wip: larger screen`

URL: <https://github.com/snesrev/zelda3/pull/71>

Estado visto: cerrado.

Intento temprano del propio upstream para pantalla más grande. Tocaba archivos como PPU, `main.c`, `dungeon.h`, `sprite.c`, tipos y `zelda_rtl`.

Lección para este port 3DS:

- el problema se consideró complejo desde temprano;
- ampliar pantalla tocando muchas capas aumenta el riesgo de romper lógica;
- para 3DS conviene aislar el cambio en el render y no reabrir toda la lógica.

### PR #193: `Fix 4:3 aspect ratio and add 8:7`

URL: <https://github.com/snesrev/zelda3/pull/193>

Estado visto: abierto.

Está relacionado con aspect ratio y presentación, no con cámara fixed de overworld.

Lección para este port 3DS:

- separar “cómo escalo/estiro la imagen” de “qué mundo adicional dibujo”;
- por eso el menú 3DS debe tener `Original`, `Stretch` y `Wide` como modo principal.

### PR #28: `Scale fullscreen image with aspect ratio.`

URL: <https://github.com/snesrev/zelda3/pull/28>

Estado visto: cerrado.

Trata escalado de fullscreen, no lógica de widescreen.

Lección para este port 3DS:

- `Stretch` debe ser un modo de presentación, no un modo de lógica ni de renderer wide.

## Issues relevantes

### Issue #227: `Prevent black bars at the edge of map segment in widescreen`

URL: <https://github.com/snesrev/zelda3/issues/227>

Estado visto: abierto.

Este issue describe exactamente el comportamiento deseado: en widescreen, detener el scroll antes de que aparezca barra negra y dejar que Link camine hasta el borde como en aspecto normal.

Lección para este port 3DS:

- el modo `Fixed` debe imitar la cámara nativa, no extender la lógica del mundo;
- el fix debe aplicarse al overworld edge camera.

### Issue #37: `Feature Request: Widescreen hack to show more of the map`

URL: <https://github.com/snesrev/zelda3/issues/37>

Estado visto: cerrado con etiqueta `notnow`.

El issue muestra que un widescreen real y limpio no era trivial para upstream.

Lección para este port 3DS:

- hay que asumir riesgo en transiciones y tilemap;
- no hay una solución upstream oficial lista para copiar.

### Issue #246: `Agahnim barrier fails to spawn with widescreen enabled`

URL: <https://github.com/snesrev/zelda3/issues/246>

Estado visto: abierto.

Confirma que ampliar checks de pantalla en la lógica puede afectar spawn/despawn o triggers de sprites.

Lección para este port 3DS:

- no usar `kFeatures0_ExtendScreen64` para el modo fixed;
- evitar que el menú 3DS cambie reglas de spawn/despawn.

### Issues #98, #115, #116 y #159

URLs:

- <https://github.com/snesrev/zelda3/issues/98>
- <https://github.com/snesrev/zelda3/issues/115>
- <https://github.com/snesrev/zelda3/issues/116>
- <https://github.com/snesrev/zelda3/issues/159>

Estos issues documentan fallos de renderer, fondos especiales, cono de luz y sprites en vistas extendidas.

Lección para este port 3DS:

- no tocar dungeons ni efectos especiales con el fixed del overworld;
- preferir el renderer nuevo;
- mantener cambios de wide reducidos, reversibles y por frame.

### Issue #322: `Widescreen UI`

URL: <https://github.com/snesrev/zelda3/issues/322>

Estado visto: abierto.

Es un tema de UI wide, separado de cámara/scroll. No bloquea el fix de overworld.

## Decisión tomada para v3.4 3DS

No se importa el PR #314 entero. En su lugar:

- se mantiene el ancho 3DS de 400 px (`72 px` extra por lado);
- se deja la lógica nativa del juego intacta;
- se agrega un clamp visual horizontal solo para overworld;
- se aplica un offset transitorio de sprites en PPU, sin mutar OAM;
- se libera el offset durante transiciones horizontales para evitar salto visual;
- se evita `kFeatures0_ExtendScreen64`.

Esto toma la lección de upstream sin traer una reescritura WIP de tilemap ultrawide.

