# Modo noche y Tiny TTF

Registro de la implementación y de las pruebas realizadas el **2026-09-04**.

## Resultado adoptado

El modo noche usa Tiny TTF en el perfil de producción `esp32-s3-display` y la
placa quedó flasheada con ese perfil. La prueba de diagnóstico se mantiene en
`esp32-s3-night-bench`.

Tiny TTF usa el archivo TTF y lo rasteriza en el dispositivo al tamaño elegido.
No se habilitó FreeType ni un renderizador SVG. La fuente usada es la misma
familia visual Montserrat que ya estaba disponible en:

```text
vendor/lvgl9/scripts/built_in_font/Montserrat-Medium.ttf
```

El repositorio conserva un perfil de producción y perfiles auxiliares:

- `esp32-s3-display`: perfil de producción, con `NIGHT_TTF_USE=1` y
  `LV_USE_TINY_TTF=1`.
- `esp32-s3-night-bench`: perfil de diagnóstico, con Tiny TTF e instrumentación.
- `esp32-s3-night-tiny`: perfil compatible de prueba que conserva esos flags.

## Funcionalidad del modo noche

Desde `PANTALLA`:

- `Noche` está a la izquierda.
- El roller vertical queda debajo y permite seleccionar entre `1%` y `20%`.
- `Volver` está abajo a la derecha.

El valor nocturno inicial es **3%**. Se guarda en NVS con la clave
`night_bright`, separado del brillo normal. Los valores se limitan siempre al
rango `1..20`.

Al entrar en Noche:

- Se muestra solo `HH:MM`, en formato de 24 horas.
- El texto es blanco sobre negro absoluto.
- No se muestran fecha, WiFi, sprites ni animaciones.
- El reloj se actualiza cada 500 ms, pero solo cambia el texto cuando cambia el
  minuto.
- Se conserva el brillo normal configurado y se aplica el porcentaje nocturno.

Para salir, se mantiene pulsada cualquier zona durante **2.000 ms**. Un toque
corto no sale. Al salir se restaura el brillo normal y se espera la liberación
del táctil para evitar un clic accidental en el reloj.

## Cálculo del tamaño TTF

No se escala el objeto en los ejes X o Y. El tamaño se busca con una búsqueda
binaria entre `1` y `512` usando el peor caso `88:88`:

- Ancho disponible: `480 px`.
- Alto disponible: `320 px`.
- Tamaño TTF máximo encontrado: **171 px**.
- Altura de línea reportada por la fuente: **208 px**.

El ancho de `88:88` es el límite dominante. Que la altura de línea sea menor que
320 px no significa que falte tamaño: aumentar más la fuente haría que la hora
completa dejara de caber horizontalmente. El resultado conserva las
proporciones originales de la tipografía y no extrapola artificialmente los
píxeles.

## Comparación de recursos

Las cifras corresponden a los builds verificados por PlatformIO. El tamaño de
flash es el de la región de aplicación reportada por PlatformIO.

| Perfil | RAM estática | Flash | Observación |
|---|---:|---:|---|
| `esp32-s3-display` | 51.616 B (15,8%) | 4.319.010 B (65,9%) | Producción con Tiny TTF |
| `esp32-s3-night-bench` | 51.640 B (15,8%) | 4.319.498 B (65,9%) | Tiny TTF e instrumentación |

La RAM estática del enlazado no mide toda la caché dinámica de glifos que Tiny
TTF puede reservar durante el uso. El perfil de diagnóstico agrega la
instrumentación del benchmark sobre la misma configuración de producción.

La variante bitmap es más rápida y predecible porque sus glifos ya están
convertidos a píxeles. Su tamaño es fijo: ampliar la fuente bitmap de 48 px
produce bordes borrosos o pixelados. Tiny TTF necesita más trabajo al crear y
cachear glifos, pero permite rasterizar directamente al tamaño nativo de 171 px.

Una fuente bitmap nueva, generada nativamente a 171 px y limitada a los
caracteres `0..9` y `:`, sería otra alternativa posible. No forma parte de la
decisión adoptada.

## Pruebas realizadas

### Compilación de producción

```powershell
$projectPython = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
& $projectPython -m platformio run -e esp32-s3-display
```

Resultado verificado: código `0`, `firmware.elf` generado, 51.616 B de RAM
estática y 4.319.010 B de flash de aplicación.

### Diagnóstico y carga de producción

```powershell
$projectPython = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
& $projectPython -m platformio run -e esp32-s3-night-bench

$env:PYTHONIOENCODING='utf-8'
$env:PYTHONUTF8='1'
& $projectPython -m platformio run -e esp32-s3-display -t upload --upload-port COM8
```

Resultado verificado: ambos builds terminaron con código `0`; la carga de
producción verificó el hash de cada imagen y reinició por RTS en `COM8`.

La prueba serie de apertura devolvió `flipclock: listo`. En el firmware de
diagnóstico se entró al modo Noche y luego se envió `r`; no apareció el error de
respaldo bitmap y el comando respondió correctamente.

Los símbolos `lv_tiny_ttf` y `night_font_ttf` están presentes en el ELF de
producción. La confirmación visual final debe hacerse mirando la pantalla al
activar Noche; la consola no captura píxeles.

## Archivos involucrados

- `src/night_ui.c` y `src/night_ui.h`: pantalla, temporizadores, salida por
  pulsación larga y selección de fuente.
- `src/assets/night_font.S` y `src/assets/night_font.h`: inclusión del TTF en
  flash en los perfiles que usan Tiny TTF.
- `src/settings_ui.c`: botón, roller y disposición de la pestaña `PANTALLA`.
- `src/prefs.cpp` y `src/prefs.h`: persistencia y límites del brillo nocturno.
- `src/lv_conf.h`: configuración Tiny TTF sobreescribible por flags de build.
- `platformio.ini`: perfil de producción, benchmark y alias Tiny TTF.
- `src/lv_port.c`, `src/lv_port.h` y `src/main.cpp`: instrumentación aislada
  bajo `NIGHT_TTF_BENCHMARK`.

## Lecciones y decisiones

- No se adoptaron números de siete segmentos: se volvió a la tipografía normal.
- No se amplió artificialmente el bitmap de 48 px para la versión Tiny TTF.
- El tamaño se determina con el texto completo más ancho, no con un dígito
  aislado.
- Tiny TTF fue suficiente para obtener bordes nítidos sin incorporar FreeType.
- El valor nocturno no reemplaza ni modifica el brillo normal guardado.
- Los cambios gráficos se mantuvieron explícitos y verificables; no se añadieron
  cambios visuales adicionales fuera de lo solicitado.
