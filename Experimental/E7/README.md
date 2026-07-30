# Zelda 3DS E7 experimental

Build experimental de estabilidad para corregir regresiones detectadas en E6.

## Diagnóstico de E6

Los dumps de E6 mostraron:

- `Core 1 PPU budget: unavailable/70% requested`
- `Parallel PPU renderer: unavailable`

Eso significa que la Old 3DS rechazó el presupuesto de 70%, y como consecuencia se perdió el renderer paralelo. Por eso E6 podía sentirse más lento que E5.

## Cambios de E7

- Vuelve al presupuesto seguro de Core 1 de 30%, para recuperar el renderer PPU paralelo.
- Revierte el split agresivo del PPU de E6 y vuelve al balance conservador de E5.
- Mantiene el redraw inmediato de pantalla inferior para cambios críticos de vida/mapa/estado.
- Permite explícitamente HOME menu y sleep.
- Maneja eventos APT de suspend/restore/sleep/wakeup/exit.
- Si la app entra al HOME menu o se suspende, cierra cualquier frame activo antes de ceder el control.
- Los errores fatales en 3DS ahora muestran una pantalla legible en vez de parecer un black screen silencioso.

## Qué probar

- Presionar HOME debe abrir el HOME menu.
- Desde HOME, cerrar la aplicación debe funcionar.
- Si una instalación limpia falla después de `Setup complete`, debe mostrar un error legible en pantalla y escribir `runtime.log` / `setup-error.txt`.
- En los dumps, `Parallel PPU renderer` debería volver a decir `enabled`.

## Instalación directa

CIA:

https://github.com/EstebanPdN/zelda-alttp-3ds/raw/E7/Experimental/E7/zelda3-3ds-E7.cia
