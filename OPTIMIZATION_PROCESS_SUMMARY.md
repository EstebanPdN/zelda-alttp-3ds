# Resumen del proceso de optimización 3DS

El archivo `PROCESS.txt` documenta cómo se arregló originalmente el problema de rendimiento en New 3DS. La clave fue dejar de tratar la caída visual como caída de velocidad del juego.

## Qué se descubrió en New 3DS

- El problema inicial no era la lógica del juego: la lógica costaba muy poco.
- El fallo grave era que el juego estaba atado al VBlank. Si un frame tardaba más de 16.67 ms, el juego esperaba otro VBlank y entraba en cámara lenta.
- Se separó el reloj lógico del render: la simulación corre a 60 Hz aunque la presentación visual pierda frames.
- Se cambió la espera de VBlank para no introducir una espera doble.
- Se añadió telemetría: tiempo de lógica, PPU, presentación, pantalla inferior, FPS lógico y FPS visual.

## Optimizaciones que sí ayudaron

- Presentador nativo 3DS con textura/GPU en vez de hacer toda la copia/conversión por CPU.
- Pitch alineado a 512, que redujo mucho el costo del PPU.
- Conversión/permutación de color hecha por PICA200/GPU, no con un bucle CPU por píxel.
- Renderer PPU paralelo en New 3DS usando Core 2.
- Pantalla inferior desacoplada/asíncrona para que no bloquee cada frame superior.
- Desactivar trabajo innecesario como límites de sprites ampliados cuando no aporta en 3DS.
- Mantener modo Stretch/Original como ruta rápida y dejar Wide como opción más cara.

## Qué se ha hecho en este chat para Old 3DS

- Se mantuvo la ruta New 3DS lo más cercana posible a v2.0, porque ya estaba probada.
- Se añadió detección automática New/Old 3DS.
- Se agregó overlay L+R+B con versión, modelo y FPS promedio.
- Se mantuvo la metadata instalada estable para que no cree otra app al instalar.
- Se corrigió el parser/config de primer arranque para evitar errores por claves antiguas.
- Se mejoró la pantalla inferior: input táctil corregido, redraw inmediato al tocar, y redraw crítico cuando cambian vida/mapa/estado.
- Se corrigió HOME/suspend/close handling para que el HOME menu y cierre de app funcionen mejor.
- Se añadió pantalla fatal legible en 3DS para evitar black screens silenciosos en errores de setup/assets.
- Se probó subir Core 1 a 70%, pero la Old 3DS lo rechazó y el renderer paralelo quedó desactivado; por eso se revirtió a 30%.
- La primera `v2.3.1` probó reducir trabajo renderizando líneas alternas, pero se descartó porque degradaba demasiado la imagen aunque subiera FPS.
- `v2.3.2` vuelve a resolución completa y prueba optimizaciones conservadoras del PPU: cache de paleta/color, evitar evaluación de sprites cuando OBJ está deshabilitado, saltar trabajo de subscreen cuando no se usa, fast path de color-window normal y escritura de paleta desenrollada.
- La nueva `v2.3.1` experimental de arranque corrige un bloqueo de New 3DS donde se creaba un worker PPU en Core 1 aunque el sistema hubiera devuelto 0% de presupuesto de CPU para ese core; ahora negocia 80/70/50/30% y solo desactiva Core 1 si todos los intentos vuelven con 0%.

## Qué se puede seguir optimizando

- Tomar como referencia lo que funcionó en New 3DS: presentación PICA200/Citro2D, timing lógico a 60 Hz, bounded catch-up, cuidado con VBlank, render superior completo, renderer PPU paralelo y cachés de tiles/scanlines. Esas piezas no deben promocionarse como features públicas, pero sí sirven como lista de control para el perfil Old 3DS.
- PPU por scanline: es el cuello principal en Old 3DS, con dumps alrededor de 27–29 ms.
- Dirty lines / dirty tiles: no redibujar líneas o tiles que no cambiaron.
- Caché de fondos por tilemap/paleta para overworld e interiores.
- Render adaptativo por escena: lluvia/overworld puede requerir ruta distinta a interiores.
- Sprite/background simplification solo en Old 3DS, con cuidado de no romper visuales.
- Balance del worker Core 1 dentro del límite real aceptado por Old 3DS.
- Reducir capturas/hooks de pantalla inferior cuando no se necesitan.
- Mantener Wide como ruta opcional cara; Original/Stretch son mejores para medir 60 FPS.
