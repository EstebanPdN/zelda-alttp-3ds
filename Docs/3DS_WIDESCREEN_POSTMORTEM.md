# Postmortem del widescreen 3DS

Este documento resume qué estaba mal en los intentos anteriores del modo wide/fixed y qué principio usa la versión nueva.

## Cómo funciona el juego original

En el overworld, la cámara original no se mueve libremente con Link hasta el infinito. El flujo real es:

1. `Link_Main()` actualiza posición, velocidad y estado de Link.
2. `Overworld_OperateCameraScroll()` mueve `BG2HOFS_copy2` / `BG2VOFS_copy2` solo si Link cruza los umbrales de cámara (`camera_x_coord_scroll_low`, `camera_x_coord_scroll_hi`, etc.).
3. `OverworldCameraBoundaryCheck()` detiene la cámara al llegar a los límites de la zona actual (`ow_scroll_vars0.xstart`, `ow_scroll_vars0.xend`, `ystart`, `yend`).
4. `OverworldHandleTransitions()` no depende de que la cámara siga moviéndose. La transición se dispara por la posición real de Link contra el borde del área (`kOverworld_OffsetBaseX/Y` y `overworld_right_bottom_bound_for_scroll`).
5. Durante la transición, `OverworldScrollTransition()` mueve la cámara por pasos fijos (`-8`/`+8`) hasta los targets nativos, carga stripes del tilemap y luego `Overworld_SetCameraBoundaries()` instala los límites de la nueva zona.

La regla importante: en resolución nativa, cuando Link se acerca a un borde, la cámara se para antes que Link; Link toca el borde, se dispara la transición, y el juego pasa a la zona siguiente.

## Qué se hizo mal en versiones anteriores

### 1. Se mezcló cámara visual con lógica de juego

Los intentos `v1.9` a `v2.8` tocaron demasiados sitios: `overworld.c`, `dungeon.c`, `player_oam.c`, `zelda_rtl.c`, PPU y menú. Eso convirtió un problema de presentación en un problema de lógica.

El error principal fue intentar “hacer wide” cambiando condiciones de cámara, bounds, transiciones o coordenadas que el juego usa para decidir spawn/despawn, puertas, ancillas, sprites y cambios de área.

### 2. `kFeatures0_ExtendScreen64` era demasiado agresivo para 3DS

El modo `Force` activaba `kFeatures0_ExtendScreen64`. Ese flag altera código en sprites, ancillas y mensajes para extender spawn/despawn y checks de pantalla.

Eso encaja con los síntomas reportados:

- sprites o fragmentos raros en el lado opuesto al acercarse a izquierda/derecha;
- entidades que aparecen/desaparecen en sitios incorrectos;
- diferencias entre dungeons, overworld y transiciones.

Conclusión: en esta versión 3DS, `Wide + Standard` y `Wide + Fixed` no deben activar `kFeatures0_ExtendScreen64`.

### 3. Se mutó OAM/BG temporalmente desde el renderer

Algunos intentos hicieron shifts copiando o modificando:

- `BG1HOFS_copy2` / `BG2HOFS_copy2`;
- valores de PPU ya escritos;
- OAM o coordenadas de sprites.

Aunque se restaurara después, era una ruta frágil: el juego, el NMI, el PPU y los workers de render no comparten todos el mismo significado de “temporal”. Esa mezcla puede producir sprites desplazados, dirección de transición invertida o residuos de tilemap.

Conclusión: el renderer puede cambiar solo estado transitorio del PPU para el frame actual, y debe restaurarlo antes de terminar el frame. No debe escribir de vuelta al estado de juego.

### 4. Los fixes de transición eran parches de dirección, no un modelo

Las versiones anteriores añadieron casos especiales para transiciones horizontales/verticales. Eso arreglaba un síntoma y rompía otro: por ejemplo, podía desaparecer el borde negro vertical pero reaparecer glitch horizontal o animación en dirección extraña.

La transición original ya tiene una secuencia correcta. El modo fixed no debe cambiar `overworld_screen_transition`, `overworld_screen_trans_dir_bits`, targets ni `link_x_coord/link_y_coord`.

### 5. Se tocó demasiado fuera del overworld

Las dungeons ya funcionan bien porque usan otro modelo de cámara/room bounds. Meter el fixed ahí aumenta el riesgo de duplicar efectos como cono de luz, fondos especiales o checks de habitaciones.

Conclusión: el modo fixed nuevo es overworld-only.

## Enfoque correcto para esta versión

El nuevo modo `Wide: Fixed` implementa una cámara visual, no una cámara lógica.

- La lógica del juego sigue usando `BG2HOFS_copy2` real.
- El render calcula una `visual_x` solo para dibujar.
- Si la cámara nativa está demasiado cerca del borde izquierdo, `visual_x` se mueve hacia dentro por el margen wide.
- Si está demasiado cerca del borde derecho, `visual_x` se mueve hacia dentro por el margen wide.
- En 3DS wide, el margen usado es `72 px`, porque la pantalla renderiza `256 + 72 + 72 = 400 px`.
- Los sprites reciben un offset transitorio `renderObjXOffset` dentro del PPU para que sigan alineados con el fondo.
- El OAM real no se modifica.
- `BG*_HOFS_copy*` no se modifica.
- `kFeatures0_ExtendScreen64` no se activa.
- Al terminar el frame se restauran los hscrolls y el offset transitorio.

## Transiciones horizontales

Para evitar el salto al pasar de gameplay estable a transición:

- al iniciar una transición derecha, la vista conserva el offset fixed del borde derecho;
- durante los primeros `72 px` de scroll, ese offset se libera gradualmente;
- después la transición vuelve al scroll nativo;
- la izquierda usa la misma regla en espejo.

Así la transición no cambia de dirección ni toca targets, pero tampoco salta de golpe desde la cámara fixed a la cámara nativa.

## Menú Screen

El menú queda como pidió el usuario:

```text
Mode: Original / Stretch / Wide
Wide: Standard / Fixed
```

`Wide: Standard` conserva el comportamiento wide estable de v1.8/v3.3.

`Wide: Fixed` aplica solo el clamp visual del overworld.

## Reglas que no se deben romper otra vez

- No activar `kFeatures0_ExtendScreen64` desde el menú 3DS.
- No tocar cámara de dungeons para resolver overworld.
- No mutar OAM real.
- No mutar `BG1HOFS_copy2` / `BG2HOFS_copy2`.
- No cambiar la lógica de transición del overworld.
- No restaurar `svcExitProcess()` para cerrar la app.
- Mantener `extend_y`/240 líneas para usar toda la altura de pantalla 3DS.

