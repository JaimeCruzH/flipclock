# Registro de cambios, rutas y dependencias

Ultima actualizacion: **2026-09-04**. Este documento resume la integracion de
bateria, la reparacion del modo Noche, el apagado por deep sleep y la limpieza
documental realizada en el repositorio.

## Apagado y despertar por RESET

Se incorporo el apagado por deep sleep desde **ajustes -> HORA**. El flujo
conserva el boton **BOOT/GPIO0** para programacion y usa **RESET/EN** como
reinicio externo para volver a iniciar el reloj.

- `src/settings_ui.c`: boton **Apagar** en la barra inferior de la pestana
  **HORA**, junto a **Guardar** y **Cancelar**, con confirmacion previa.
- `src/power_manager.cpp` y `src/power_manager.h`: guardado del estado del
  Pomodoro, apagado de WiFi, backlight y panel, y entrada en deep sleep.
- `src/esp_bsp.c` y `src/esp_bsp.h`: preparacion del panel y backlight antes
  del sleep.
- `src/pomodoro_src.cpp` y `src/pomodoro_src.h`: persistencia del tiempo
  restante antes de dormir.

La prueba en hardware confirmo que **Apagar** apaga la pantalla y que pulsar
**RESET** vuelve a iniciar el dispositivo correctamente.

## Cambios de bateria

Aplicados en los commits `bfb5ac3` y `a0a9524`:

- `src/battery_src.c` y `src/battery_src.h`: lectura del divisor de bateria en
  `GPIO8`, porcentaje aproximado, tendencia, historial de descarga y autonomia.
- `src/main.cpp`: inicializacion del muestreador al arrancar.
- `src/settings_ui.c`: media movil de 15 lecturas para el valor mostrado en
  `PANTALLA -> WIFI`.
- `docs/BATERIA.md`: calculo, limites y procedimiento de medicion.

La bateria se muestrea en una tarea independiente cada 10 minutos. No se
modifico la logica de la pantalla Noche para incorporar esta funcion.

## Reparacion del modo Noche

- `platformio.ini`: `esp32-s3-display`, que es el perfil de produccion, ahora
  activa `NIGHT_TTF_USE=1` y `LV_USE_TINY_TTF=1`.
- `src/night_ui.c`: la hora usa Tiny TTF con la fuente Montserrat embebida; si
  la inicializacion falla, el respaldo bitmap ya no recibe una transformacion
  que lo agrande artificialmente y se registra el motivo.
- `src/assets/night_font.S`: la fuente se incluye en todos los perfiles que
  usan Tiny TTF, no solo en el perfil de prueba.
- `docs/MODO_NOCHE_TINY_TTF.md`: perfil adoptado, pruebas y consumo de memoria
  actualizados.

La causa del fallo era que el perfil normal no compilaba con Tiny TTF. El
codigo caia a `lv_font_montserrat_48` y lo escalaba en X e Y, por eso aparecia
grande y pixelado. La medicion de bateria no cambio esa ruta grafica.

## Perfiles validos

| Perfil | Uso |
|---|---|
| `esp32-s3-display` | Produccion; es el perfil que debe cargarse normalmente. |
| `esp32-s3-night-bench` | Diagnostico de Tiny TTF y benchmark. |
| `esp32-s3-night-tiny` | Perfil compatible de prueba; conserva los flags TTF. |

La fuente de verdad de estos perfiles es `platformio.ini`. No se debe cargar
un binario de `.pio/build/` de otro perfil por accidente.

## Rutas validas

Las rutas del repositorio son relativas a su raiz:

- `platformio.ini`: configuracion de PlatformIO y dependencias.
- `src/`: codigo de aplicacion, BSP y port de LVGL.
- `src/assets/`: sprites y fuente embebida del reloj.
- `vendor/lvgl9-library.json`: manifest versionado de LVGL.
- `vendor/lvgl9/`: copia local regenerada de LVGL 9.5.0; esta ignorada por Git.
- `tools/setup_lvgl.py`: recrea `vendor/lvgl9/`.
- `tools/gen_assets.py`: regenera los sprites.
- `docs/`: documentacion y recursos visuales versionados.
- `.pio/`: salida local de compilacion; esta ignorada por Git.

En PowerShell, las rutas auxiliares locales se derivan sin fijar una carpeta de
usuario:

```powershell
$projectRoot = (Resolve-Path .).Path
$projectPython = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
$workspaceRoot = Split-Path $projectRoot -Parent
$caBundle = Join-Path $workspaceRoot 'win-ca-bundle.pem'
$factoryBackup = Join-Path $workspaceRoot 'backup\factory_full_16MB.bin'
```

`$projectPython` es la ruta funcional de PlatformIO verificada en la máquina de
desarrollo. `$caBundle` y `$factoryBackup` son auxiliares locales, no forman
parte del repositorio y solo se usan cuando `Test-Path` confirma que existen.

Se eliminaron de la documentación las rutas absolutas a entornos Python no
funcionales y los comandos dependientes de ellas. No se documentan copias de
LVGL, proyectos hermanos ni carpetas de prueba como dependencias de
`flipclock`.

## Librerias que sirven

`platformio pkg list -e esp32-s3-display` confirma una sola libreria declarada
por el proyecto:

- **LVGL 9.5.0**, enlazada desde `symlink://./vendor/lvgl9`. Se usa para toda la
  interfaz, el display, el tactil y Tiny TTF.

El framework **Arduino-ESP32 3.2.1** tambien es necesario. Aporta `Arduino.h`,
`Preferences`, WiFi, HTTP, TLS, cJSON y los componentes ESP-IDF/FreeRTOS para
ADC, GPIO, I2C, SPI, LCD, tareas y semaforos. No debe duplicarse como una
entrada `lib_deps`.

Para las herramientas de desarrollo, `tools/gen_assets.py` usa **Pillow** y
**numpy** solo para regenerar sprites en el PC; no son librerias del firmware.

No existen `lib/` ni `include/`, y no hay dependencias `lib_deps` sin uso. Por
eso no se elimina ninguna libreria: borrar LVGL o cualquiera de los componentes
del framework impediria compilar.

## Verificacion realizada

```powershell
& $projectPython -m platformio pkg list -e esp32-s3-display
& $projectPython -m platformio run -e esp32-s3-display
& $projectPython -m platformio run -e esp32-s3-night-bench
& $projectPython -m platformio run -e esp32-s3-display -t upload --upload-port COM8
```

Resultados: ambos builds terminaron con `SUCCESS`; la compilacion final de
produccion usa 51.688 B de RAM estatica y 4.327.242 B de flash de aplicacion;
la carga final en `COM8` verifico el hash y reinicio por RTS. La consola
devolvio `flipclock: listo` y los comandos `t` y `f` respondieron.
