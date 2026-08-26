# Entorno de desarrollo, compilacion y carga

Guia operativa del proyecto `flipclock`. La ultima verificacion de las rutas,
versiones y comandos de esta guia se hizo el **2026-08-26**.

La guia vive dentro del repositorio para que un clon de GitHub indique donde
estan las herramientas y que hacer cuando una instalacion local no funciona.

## 1. Mapa de rutas

Todas las rutas siguientes corresponden a la maquina de desarrollo actual.

| Recurso | Ruta o ubicacion | Estado |
|---|---|---|
| Proyecto | `C:\Claude\PROJECTS\ESP32\flipclock` | Repositorio Git |
| Configuracion de PlatformIO | `platformio.ini` | Versionada; fuente de verdad del build |
| Codigo de aplicacion y BSP | `src/` | Versionado |
| LVGL usado por este proyecto | `vendor/lvgl9/` | Local, ignorado por Git; se regenera |
| Manifest parcheado de LVGL | `vendor/lvgl9-library.json` | Versionado; necesario para compilar |
| Script para traer LVGL | `tools/setup_lvgl.py` | Versionado |
| Assets embebidos | `src/assets/` | Versionados |
| Generador de assets | `tools/gen_assets.py` | Versionado |
| Artefactos de compilacion | `.pio/` | Local, ignorado por Git |
| Entorno Python de PlatformIO | `C:\Claude\PROJECTS\ESP32\.venv` | Local, fuera del repo |
| Python base del entorno | `C:\Users\jaime\AppData\Roaming\uv\python\cpython-3.11-windows-x86_64-none\python.exe` | Existe en la maquina; lo necesita `.venv` |
| Certificados para el proxy TLS | `C:\Claude\PROJECTS\ESP32\win-ca-bundle.pem` | Local, fuera del repo |
| Respaldo de fabrica | `C:\Claude\PROJECTS\ESP32\backup\factory_full_16MB.bin` | No borrar |

El directorio padre tambien contiene proyectos de prueba (`lvgl9_test`,
`lvgl_test` y `test_suite`) y el codigo original del fabricante en `vendor/`.
No son dependencias que PlatformIO deba mezclar con este proyecto: para
`flipclock` se usa exclusivamente `vendor/lvgl9/`.

## 2. Versiones verificadas

El entorno funcional esta en:

- Python 3.11.15.
- PlatformIO Core 6.1.19, ejecutado como modulo Python.
- esptool 5.3.1 disponible en el entorno Python.
- Plataforma pioarduino `54.03.21`.
- Arduino-ESP32 3.2.1 sobre ESP-IDF 5.4.x.
- LVGL 9.5.0.
- Entorno PlatformIO: `esp32-s3-display`.

La placa real responde como ESP32-S3 con 8 MB de PSRAM, 16 MB de flash,
USB-Serial/JTAG y MAC `7c:e8:b1:b2:89:60`. El puerto configurado es `COM8`
(USB VID:PID `303A:1001`).

PlatformIO puede mostrar el identificador generico `ESP32-S3-DevKitC-1-N8
(8 MB QD, No PSRAM)`. No eliminar por eso la configuracion especial del
`platformio.ini`: esta placa necesita PSRAM octal y flash de 16 MB.

## 3. Que se versiona y que se regenera

El `.gitignore` del proyecto excluye deliberadamente `.venv/`, `.pio/`,
`vendor/lvgl9/` y `preview/`. Por tanto, un upload a GitHub no debe contener
el entorno Python ni la copia local de LVGL; no hay que buscarlos en GitHub.

Despues de clonar el repositorio:

1. Se necesita un Python local funcional.
2. `tools/setup_lvgl.py` descarga o reconstruye `vendor/lvgl9/`.
3. `vendor/lvgl9-library.json` debe permanecer versionado.
4. `.pio/` se genera solo durante el build.

Los sprites existentes no necesitan regenerarse para compilar. Si se cambian
sus fuentes, usar:

```text
python tools/gen_assets.py --preview   # escribe PNGs en preview/
python tools/gen_assets.py --emit      # actualiza src/assets/
```

## 4. Comprobacion rapida del entorno

Usar **PowerShell nativo de Windows**, no Git Bash ni MSYS/Mingw. Desde la
raiz del proyecto:

```powershell
$projectDir = "C:\Claude\PROJECTS\ESP32\flipclock"
$projectPython = "C:\Claude\PROJECTS\ESP32\.venv\Scripts\python.exe"
$pythonBase = "C:\Users\jaime\AppData\Roaming\uv\python\cpython-3.11-windows-x86_64-none\python.exe"

Set-Location $projectDir
Test-Path $projectPython
Test-Path $pythonBase
& $projectPython --version
& $projectPython -m platformio --version
& $projectPython -m esptool version
```

La comprobacion verificada devuelve Python 3.11.15, PlatformIO 6.1.19 y
esptool 5.3.1. El Python alternativo de
`C:\Users\jaime\AppData\Local\hermes\hermes-agent\venv` funciona, pero no
contiene PlatformIO; sirve como diagnostico, no como entorno del proyecto.

Si aparece `No Python at ...` al ejecutar `.venv`, comprobar primero que el
Python base exista y ejecutar desde PowerShell nativo. En esta maquina el
mensaje aparecio inicialmente porque el entorno restringido no podia acceder
al interprete base; no significaba que GitHub hubiera borrado los archivos.

### Reparar el entorno solo si la comprobacion falla

No reinstalar nada si los tres comandos de version funcionan. Si el Python
base existe pero los lanzadores de `.venv` estan dañados, reparar el entorno
sin tocar el repositorio:

```powershell
$envDir = "C:\Claude\PROJECTS\ESP32\.venv"
& $pythonBase -m venv --upgrade $envDir
& "$envDir\Scripts\python.exe" -m pip install "platformio==6.1.19" "esptool==5.3.1"
```

Definir antes las variables de certificados de la seccion siguiente si el
comando necesita descargar paquetes. Si PlatformIO vuelve a mostrar el error
de `click` sobre `ParamType.get_metavar`, fijar `click==8.1.8` dentro de este
mismo entorno. No instalar PlatformIO globalmente ni sustituir el Python base
si el entorno local ya pasa la comprobacion.

## 5. Certificados y descargas

La red de desarrollo intercepta TLS. Antes de cualquier descarga de PlatformIO,
LVGL o paquetes Python, definir el CA bundle corporativo:

```powershell
$caBundle = "C:\Claude\PROJECTS\ESP32\win-ca-bundle.pem"
$env:PIP_CERT = $caBundle
$env:SSL_CERT_FILE = $caBundle
$env:REQUESTS_CA_BUNDLE = $caBundle
```

No usar Git Bash para instalar el toolchain: pioarduino rechaza MSYS/Mingw
con `MSys/Mingw is not supported`. Si PlatformIO queda descargando durante
varios minutos, revisar la red antes de borrar fuentes o modificar el codigo.

## 6. Compilar

Con las variables de certificados definidas:

```powershell
& $projectPython -m platformio run -e esp32-s3-display
```

El ultimo build verificado genero correctamente `firmware.elf` y uso:

- RAM: 51.024 de 327.680 bytes (15,6 %).
- Flash de aplicacion: 4.042.734 de 6.553.600 bytes (61,7 %).

No considerar suficiente el mensaje `Successfully created ESP32S3 image.`:
puede corresponder solo al bootloader. La validacion real es el codigo de
salida cero y la existencia de `.pio/build/esp32-s3-display/firmware.elf`.

## 7. Verificar el puerto y cargar

Antes de cargar, cerrar cualquier monitor serie: `COM8` solo puede estar
abierto por un proceso.

```powershell
& $projectPython -m platformio device list
```

Debe aparecer `COM8` con USB VID:PID `303A:1001`. Para cargar:

```powershell
# Evita que la barra de progreso Unicode rompa la consola cp1252.
$env:PYTHONIOENCODING = "utf-8"
$env:PYTHONUTF8 = "1"

& $projectPython -m platformio run -e esp32-s3-display -t upload
```

Una carga valida termina con `[SUCCESS]`, `Hash of data verified` para cada
imagen y `Hard resetting via RTS pin`. El texto `Connected to ESP32-S3` por si
solo no confirma que el firmware se haya escrito completo.

En esta maquina el primer intento de carga se bloqueo por un
`UnicodeEncodeError` de cp1252 al imprimir la barra de progreso. Repetir con
`PYTHONIOENCODING=utf-8` y `PYTHONUTF8=1` resolvio el problema; la segunda carga
escribio 4.043.136 bytes, verifico el hash y termino correctamente.

## 8. Monitor serie

Abrirlo sin dejar RTS ni DTR activos, porque esas lineas pueden resetear el
chip:

```powershell
& $projectPython -m platformio device monitor `
  --port COM8 --baud 115200 --rts 0 --dtr 0 `
  --encoding UTF-8 --no-reconnect
```

Salir con `Ctrl+C` antes de volver a cargar. El monitor iniciado despues de un
reset puede no mostrar mensajes de arranque si estos ya ocurrieron; la
ausencia de texto no demuestra por si sola que la pantalla no haya arrancado.
La comprobacion independiente es:

```powershell
& $projectPython -m esptool --port COM8 chip-id
```

El ESP32-S3 no tiene un chip ID numerico en esptool 5; el comando confirma la
respuesta leyendo la MAC.

## 9. Ajustes de hardware que no se deben quitar

Estos valores de `platformio.ini` son necesarios para esta placa:

```ini
board_build.arduino.memory_type = qio_opi
board_build.flash_mode = dio
board_build.flash_size = 16MB
board_upload.flash_size = 16MB
board_upload.maximum_size = 16777216
board_build.f_cpu = 240000000L
board_build.partitions = default_16MB.csv

-DARDUINO_USB_CDC_ON_BOOT=1
-DARDUINO_USB_MODE=1
-DBOARD_HAS_PSRAM
```

Quitar `qio_opi` puede producir un bucle de arranque por PSRAM. Quitar
`board_upload.flash_size = 16MB` puede grabar una cabecera de 8 MB junto a una
tabla de particiones de 16 MB y producir otro bucle de arranque. Quitar
`ARDUINO_USB_CDC_ON_BOOT=1` deja el programa funcionando pero sin monitor serie.

## 10. Librerias y puntos de entrada

- `platformio.ini`: entorno, plataforma, framework, memoria, puerto y
  dependencias.
- `vendor/lvgl9/`: LVGL 9.5.0 local, no versionado.
- `vendor/lvgl9-library.json`: manifest parcheado con `srcDir` y `srcFilter`;
  debe conservarse.
- `src/lv_port.c` y `src/lv_port.h`: port de LVGL y flush al ST77922.
- `src/esp_lcd_st77922.*`: driver del panel.
- `src/esp_lcd_touch.*`: tactil.
- `src/esp_bsp.*`: inicializacion de placa y brillo.
- `src/clock_ui.c`, `src/weather_ui.c` y `src/pomodoro_ui.c`: pantallas.
- `src/weather_src.cpp`: datos y API meteorologica.
- `src/assets/` y `tools/gen_assets.py`: sprites.

Para un cambio de UI, empezar en el `*_ui.c` correspondiente. Para cambios
de compilacion o librerias, empezar en `platformio.ini`, `vendor/` y
`tools/setup_lvgl.py` antes de tocar el codigo de aplicacion.

## 11. Problemas conocidos

| Sintoma | Causa o solucion |
|---|---|
| `No Python at ...` | Verificar el Python base y usar PowerShell nativo; `.venv` depende de el. |
| `MSys/Mingw is not supported` | Se ejecuto PlatformIO desde Git Bash; cambiar a PowerShell. |
| `CERTIFICATE_VERIFY_FAILED` | Definir las tres variables de certificado de la seccion 5. |
| `ParamType.get_metavar() ... ctx` | Version incompatible de `click`; el entorno historico requiere `click==8.1.8`. |
| Carga bloqueada con `UnicodeEncodeError` cp1252 | Definir `PYTHONIOENCODING=utf-8` y `PYTHONUTF8=1`, y repetir la carga completa. |
| `COM8` ocupado | Cerrar `platformio device monitor` y cualquier terminal serie. |
| Pantalla reiniciandose | Revisar `qio_opi`, `board_upload.flash_size` y `ARDUINO_USB_CDC_ON_BOOT`. |
| LVGL no compila | Ejecutar `tools/setup_lvgl.py` y comprobar `vendor/lvgl9-library.json`. |
| Build parece colgado descargando | Revisar proxy/TLS y que PlatformIO no este intentando descargar una dependencia. |

## 12. Respaldo y restauracion de fabrica

El respaldo esta fuera del repositorio para no inflarlo:

```text
C:\Claude\PROJECTS\ESP32\backup\factory_full_16MB.bin
```

Datos verificados:

- Tamano: 16.777.216 bytes.
- SHA-256: `FCC55C4E70A93709AE8F60F244BA8391E6734B120767295577119F6AC0A3478D`.
- Script de restauracion: `C:\Claude\PROJECTS\ESP32\backup\restore_factory.ps1`.
- Puerto de restauracion: `COM8`.

Comprobar el hash antes de cualquier restauracion:

```powershell
Get-FileHash `
  "C:\Claude\PROJECTS\ESP32\backup\factory_full_16MB.bin" `
  -Algorithm SHA256
```

No borrar este respaldo al limpiar `.pio/` o al reparar `.venv`.

## 13. Flujo recomendado para futuros cambios

Regla de despliegue del proyecto: todo cambio solicitado se compila y se carga
automaticamente en `COM8`, salvo que se indique expresamente que no se debe
cargar.

1. Leer esta guia, `CLAUDE.md` y el `platformio.ini`.
2. Confirmar que el entorno Python y LVGL pasan la comprobacion rapida.
3. Modificar solo las fuentes necesarias.
4. Ejecutar `git diff --check`.
5. Compilar el entorno `esp32-s3-display`.
6. Verificar `COM8`, cerrar el monitor y cargar con UTF-8.
7. Confirmar hashes, reinicio y respuesta del chip.
8. Revisar `git status` para no incluir `.pio/`, `.venv/` ni `vendor/lvgl9/`.
