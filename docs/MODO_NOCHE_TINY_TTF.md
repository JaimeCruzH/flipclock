# Modo noche y Tiny TTF

Registro de la implementación y de las pruebas iniciales realizadas el **2026-09-04**.
Validación funcional del arreglo: **2026-09-08**.
Corrección del primer renderizado y carga del firmware: **2026-09-09**.
Corrección de reservas de glifos y carga del firmware: **2026-09-14**.

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

## Corrección del renderizado en entrada y cambio de minuto

Tiny TTF crea las imágenes de los glifos durante el dibujo. En una entrada o
un cambio de minuto ocasional al modo Noche, el dibujo podía omitir los glifos
de los minutos aunque las horas ya estuvieran visibles. El arreglo anterior
solo cubría la entrada; por eso el mismo síntoma podía repetirse al comenzar
otro minuto. La segunda versión también reintentaba después del cambio, pero
el síntoma siguió apareciendo.

`src/night_ui.c` programa ahora un temporizador de una sola ejecución a **100
ms** al cargar la pantalla y después de cada cambio de minuto. Si Noche sigue
activa, el temporizador invalida la etiqueta `HH:MM` y fuerza un segundo dibujo.
Esto deja terminar la eliminación asíncrona de la pantalla de Ajustes y permite
que Tiny TTF reintente la creación de los glifos faltantes en ambos casos. El
cambio no altera la fuente, la hora ni la pantalla normal.

La causa que quedaba sin cubrir eran las reservas dinámicas durante el dibujo.
Con `NIGHT_TTF_CACHE_COUNT` en **0**, cada bitmap A8 se creaba y destruía para
cada glifo. Una reserva fallida hacía que LVGL omitiera el glifo y, como los
minutos se dibujan después de las horas, dejaba visible `HH:`. El dígito `4`
no tiene un tratamiento distinto; su coincidencia en varios casos se explica
por el orden en que se dibujan los caracteres.

La corrección actual fija `NIGHT_TTF_CACHE_COUNT` en **11**, precarga después
del cálculo del tamaño final los caracteres `0123456789:` y conserva sus
bitmaps para todo el uso del modo Noche. Además, `HH:MM` se actualiza en un
buffer estático con `lv_label_set_text_static`, eliminando la reserva de texto
en cada cambio de minuto. El reintento de 100 ms se conserva como protección,
pero ya no es el mecanismo que crea los glifos faltantes.

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
| `esp32-s3-display` | 51.696 B (15,8%) | 4.327.854 B (66,0%) | Producción con Tiny TTF y caché precargada |
| `esp32-s3-night-bench` | 51.720 B (15,8%) | 4.328.322 B (66,0%) | Tiny TTF e instrumentación |

La RAM estática del enlazado no mide los bitmaps que Tiny TTF reserva en la
caché durante la creación de Noche. La caché ahora contiene solo los 11
caracteres que puede mostrar este reloj. El perfil de diagnóstico agrega la
instrumentación del benchmark sobre la misma configuración de producción.

La variante bitmap es más rápida y predecible porque sus glifos ya están
convertidos a píxeles. Su tamaño es fijo: ampliar la fuente bitmap de 48 px
produce bordes borrosos o pixelados. Tiny TTF necesita más trabajo al crear y
cachear glifos, pero permite rasterizar directamente al tamaño nativo de 171 px.

Una fuente bitmap nueva, generada nativamente a 171 px y limitada a los
caracteres `0..9` y `:`, sigue siendo otra alternativa posible. No forma parte
de la decisión adoptada.

## Pruebas realizadas

### Compilación de producción

```powershell
$projectPython = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
$env:PYTHONIOENCODING='utf-8'
$env:PYTHONUTF8=1
& $projectPython -m platformio run -e esp32-s3-display
```

Resultado verificado: código `0`, `firmware.elf` generado, 51.696 B de RAM
estática y 4.327.854 B de flash de aplicación.

### Diagnóstico y carga de producción

```powershell
$projectPython = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
$env:PYTHONIOENCODING='utf-8'
$env:PYTHONUTF8='1'
& $projectPython -m platformio run -e esp32-s3-night-bench

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

## Validación funcional posterior

En la placa con el firmware corregido se confirmó que:

- el reloj nocturno cambia al avanzar los minutos;
- mantener cualquier zona de la pantalla durante 2 segundos vuelve al modo
  normal;
- el `RESET` físico ya no es necesario para salir del modo Noche.

El firmware que precarga los 11 glifos, usa texto estático y conserva el
reintento en la entrada y en cada cambio de minuto se compiló y se cargó
correctamente en `COM8` el **2026-09-14**. Falta repetir la prueba visual
durante varios cambios de minuto para confirmar que los minutos aparecen
siempre.

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
