# Entorno de desarrollo, compilacion y carga

Guia operativa del proyecto `flipclock`. La ultima verificacion de las rutas,
versiones y comandos de esta guia se hizo el **2026-09-04**.

La guia vive dentro del repositorio para que un clon de GitHub indique donde
estan las herramientas y que hacer cuando una instalacion local no funciona.

## 1. Mapa de rutas

Las rutas del repositorio son relativas a su raiz. Desde PowerShell, obtener la
raiz real antes de ejecutar comandos:

```powershell
$projectRoot = (Resolve-Path .).Path
$projectPython = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
$workspaceRoot = Split-Path $projectRoot -Parent
$caBundle = Join-Path $workspaceRoot 'win-ca-bundle.pem'
$factoryBackup = Join-Path $workspaceRoot 'backup\factory_full_16MB.bin'
```

| Recurso | Ruta valida | Estado |
|---|---|---|
| Proyecto | `.` / `$projectRoot` | Versionado |
| Configuracion de PlatformIO | `platformio.ini` | Versionada; fuente de verdad del build |
| Codigo de aplicacion y BSP | `src/` | Versionado |
| LVGL usado por este proyecto | `vendor/lvgl9/` | Local, ignorado por Git; se regenera |
| Fuente TTF usada por Noche | `vendor/lvgl9/scripts/built_in_font/Montserrat-Medium.ttf` | Local, generada junto con LVGL |
| Manifest parcheado de LVGL | `vendor/lvgl9-library.json` | Versionado; necesario para compilar |
| Script para traer LVGL | `tools/setup_lvgl.py` | Versionado |
| Assets embebidos | `src/assets/` | Versionados |
| Generador de assets | `tools/gen_assets.py` | Versionado |
| Artefactos de compilacion | `.pio/` | Local, ignorado por Git |
| Python de PlatformIO | `$projectPython` | Local; verificado en esta maquina |
| Certificado del proxy | `$caBundle` | Auxiliar local; no se versiona |
| Respaldo de fabrica | `$factoryBackup` | Auxiliar local; no se versiona |

`vendor/lvgl9/`, `.pio/` y `preview/` son rutas locales generadas. No deben
subirse a GitHub. El certificado y el respaldo son opcionales para un clon:
solo se necesitan para descargas detrás del proxy y restauración de fábrica.
No se usan otras copias de LVGL, proyectos hermanos ni rutas absolutas.

## 2. Versiones verificadas

El entorno funcional esta en:

- Python 3.11.7.
- PlatformIO Core 6.1.19, ejecutado como modulo Python.
- `tool-esptoolpy` 5.0.0-dev1, gestionado por PlatformIO durante la carga.
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

El `.gitignore` del proyecto excluye deliberadamente `.pio/`, `vendor/lvgl9/` y
`preview/`. Por tanto, un upload a GitHub no debe contener los artefactos de
compilacion ni la copia local de LVGL.

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
$projectRoot = (Resolve-Path .).Path
$projectPython = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'

Test-Path $projectPython
& $projectPython --version
& $projectPython -m platformio --version
& $projectPython -m platformio pkg list -e esp32-s3-display
```

La comprobacion verificada devuelve `True`, Python 3.11.7, PlatformIO Core
6.1.19 y una unica libreria declarada por el proyecto: LVGL 9.5.0.
`tool-esptoolpy` aparece como herramienta de PlatformIO, no como modulo Python
independiente.

## 5. Certificados y descargas

La red de desarrollo intercepta TLS. Antes de cualquier descarga de PlatformIO
o LVGL, usar el certificado local solo si existe:

```powershell
$caBundle = Join-Path (Split-Path $projectRoot -Parent) 'win-ca-bundle.pem'
if (Test-Path $caBundle) {
    $env:PIP_CERT = $caBundle
    $env:SSL_CERT_FILE = $caBundle
    $env:REQUESTS_CA_BUNDLE = $caBundle
}
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

- RAM: 51.616 de 327.680 bytes (15,8 %).
- Flash de aplicacion: 4.319.010 de 6.553.600 bytes (65,9 %).

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

La carga de produccion verificada escribio 4.319.408 bytes, verifico el hash de
cada imagen y termino correctamente con reinicio por RTS.

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
& $projectPython -m platformio device list
```

La lista debe mostrar `COM8` con USB VID:PID `303A:1001`. La carga usa la
version de `tool-esptoolpy` administrada por PlatformIO.

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

### Librerias que si sirven

| Dependencia | Donde se declara | Uso comprobado |
|---|---|---|
| LVGL 9.5.0 | `lib_deps = symlink://./vendor/lvgl9` | UI, display, tactil y Tiny TTF |
| Arduino-ESP32 3.2.1 | `framework = arduino` | `Arduino.h`, `Preferences`, WiFi, HTTP y cJSON |
| Componentes ESP-IDF/FreeRTOS | incluidos por Arduino-ESP32 | GPIO, ADC, I2C, SPI, LCD, tareas y semaforos |
| Pillow y numpy | imports de `tools/gen_assets.py` | Solo regeneracion de sprites en el PC |

El resultado de `platformio pkg list -e esp32-s3-display` confirma que la
unica libreria declarada por el proyecto es LVGL 9.5.0. `Preferences`, `WiFi`,
`HTTPClient`, `WiFiClientSecure`, `cJSON` y FreeRTOS vienen del framework y no
deben duplicarse como `lib_deps`.

### Componentes que se conservan

- `platformio.ini`: entornos, plataforma, framework, memoria, puerto y
  dependencias.
- `vendor/lvgl9/`: copia local de LVGL; la recrea `tools/setup_lvgl.py` y no se
  versiona.
- `vendor/lvgl9-library.json`: manifest parcheado; debe conservarse.
- `src/lv_port.*`, `src/esp_lcd_st77922.*`, `src/esp_lcd_touch.*` y
  `src/esp_bsp.*`: display, tactil y placa.
- `src/*_ui.*`, `src/*_src.*`, `src/assets/` y `tools/`: funcionalidad de la
  aplicacion y generacion de recursos.

No existen `lib/` ni `include/`, y no hay entradas `lib_deps` sin uso que
eliminar. No se borra `vendor/lvgl9/`: es la unica dependencia de libreria que
el build necesita.

Para un cambio de UI, empezar en el `*_ui.c` correspondiente. Para cambios de
compilacion o librerias, empezar en `platformio.ini`, `vendor/lvgl9-library.json`
y `tools/setup_lvgl.py` antes de tocar el codigo de aplicacion.

## 11. Problemas conocidos

| Sintoma | Causa o solucion |
|---|---|
| `No Python at ...` | Verificar `Test-Path $projectPython` y usar el entorno derivado de `$env:USERPROFILE`. |
| `MSys/Mingw is not supported` | Se ejecuto PlatformIO desde Git Bash; cambiar a PowerShell. |
| `CERTIFICATE_VERIFY_FAILED` | Definir las tres variables de certificado de la seccion 5. |
| `ParamType.get_metavar() ... ctx` | Version incompatible de `click`; el entorno historico requiere `click==8.1.8`. |
| Carga bloqueada con `UnicodeEncodeError` cp1252 | Definir `PYTHONIOENCODING=utf-8` y `PYTHONUTF8=1`, y repetir la carga completa. |
| `COM8` ocupado | Cerrar `platformio device monitor` y cualquier terminal serie. |
| Pantalla reiniciandose | Revisar `qio_opi`, `board_upload.flash_size` y `ARDUINO_USB_CDC_ON_BOOT`. |
| LVGL no compila | Ejecutar `tools/setup_lvgl.py` y comprobar `vendor/lvgl9-library.json`. |
| Build parece colgado descargando | Revisar proxy/TLS y que PlatformIO no este intentando descargar una dependencia. |

## 12. Respaldo y restauracion de fabrica

El respaldo esta fuera del repositorio para no inflarlo. Resolverlo desde la
raiz del proyecto:

```powershell
$factoryBackup = Join-Path (Split-Path $projectRoot -Parent) 'backup\factory_full_16MB.bin'
$factoryRestore = Join-Path (Split-Path $projectRoot -Parent) 'backup\restore_factory.ps1'
Test-Path $factoryBackup
Test-Path $factoryRestore
```

En la maquina de desarrollo ambas rutas devuelven `True`. Datos verificados:

- Tamano: 16.777.216 bytes.
- SHA-256: `FCC55C4E70A93709AE8F60F244BA8391E6734B120767295577119F6AC0A3478D`.
- Script de restauracion: `$factoryRestore`.
- Puerto de restauracion: `COM8`.

Comprobar el hash antes de cualquier restauracion:

```powershell
Get-FileHash `
  $factoryBackup `
  -Algorithm SHA256
```

No borrar este respaldo al limpiar `.pio/`.

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
8. Revisar `git status` para no incluir `.pio/`, `preview/` ni `vendor/lvgl9/`.
