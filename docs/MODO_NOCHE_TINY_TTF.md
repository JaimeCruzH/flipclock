# Modo noche y Tiny TTF

Registro de la implementación y de las pruebas realizadas el **2026-08-28**.

## Resultado adoptado

El modo noche se probó con Tiny TTF y la pantalla mostró los dígitos nítidos.
La placa quedó flasheada con la variante `esp32-s3-night-tiny`.

Tiny TTF usa el archivo TTF y lo rasteriza en el dispositivo al tamaño elegido.
No se habilitó FreeType ni un renderizador SVG. La fuente usada es la misma
familia visual Montserrat que ya estaba disponible en:

```text
vendor/lvgl9/scripts/built_in_font/Montserrat-Medium.ttf
```

El repositorio conserva dos perfiles de compilación para poder identificar el
coste de la alternativa:

- `esp32-s3-display`: perfil normal, sin Tiny TTF.
- `esp32-s3-night-tiny`: perfil de prueba, con `NIGHT_TTF_USE=1` y
  `LV_USE_TINY_TTF=1`.

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
| `esp32-s3-display` | 51.064 B (15,6%) | 4.043.738 B (61,7%) | Bitmap existente, sin Tiny TTF |
| `esp32-s3-night-tiny` | 51.096 B (15,6%) | 4.304.774 B (65,7%) | Tiny TTF y pruebas de benchmark |

La diferencia medida es de **+32 B de RAM estática** y **+261.036 B de
flash**, aproximadamente **6,46%** respecto del perfil normal. El archivo TTF
aporta `243.180 B`; el resto corresponde al motor Tiny TTF y a la
instrumentación del perfil de prueba. La RAM estática del enlazado no mide toda
la caché dinámica de glifos que Tiny TTF puede reservar durante el uso.

La variante bitmap es más rápida y predecible porque sus glifos ya están
convertidos a píxeles. Su tamaño es fijo: ampliar la fuente bitmap de 48 px
produce bordes borrosos o pixelados. Tiny TTF necesita más trabajo al crear y
cachear glifos, pero permite rasterizar directamente al tamaño nativo de 171 px.

Una fuente bitmap nueva, generada nativamente a 171 px y limitada a los
caracteres `0..9` y `:`, sería otra alternativa posible. No forma parte de la
decisión adoptada.

## Pruebas realizadas

### Compilación normal

```powershell
& 'C:\Users\jaime\.platformio\penv\Scripts\python.exe' -m platformio run -e esp32-s3-display
```

Resultado: código `0`, `firmware.elf` generado.

### Compilación y carga Tiny TTF

```powershell
& 'C:\Users\jaime\.platformio\penv\Scripts\python.exe' -m platformio run -e esp32-s3-night-tiny

$env:PYTHONIOENCODING='utf-8'
$env:PYTHONUTF8='1'
& 'C:\Users\jaime\.platformio\penv\Scripts\python.exe' -m platformio run -e esp32-s3-night-tiny -t upload --upload-port COM8
```

Resultado: código `0`, hash verificado y reinicio por RTS en `COM8`.

La prueba serie de apertura devolvió:

```text
[bench] ttf_size=171 ttf_line=208
```

La pantalla fue inspeccionada y los números se vieron nítidos. El comando de
diagnóstico de rendimiento no devolvió una medición fiable después del primer
render de Noche; por tanto, no se afirma aquí un porcentaje de CPU. Las cifras
de flash y RAM anteriores sí son resultados de compilación reproducibles.

## Archivos involucrados

- `src/night_ui.c` y `src/night_ui.h`: pantalla, temporizadores, salida por
  pulsación larga y selección de fuente.
- `src/assets/night_font.S` y `src/assets/night_font.h`: inclusión del TTF en
  flash solo en la variante Tiny TTF.
- `src/settings_ui.c`: botón, roller y disposición de la pestaña `PANTALLA`.
- `src/prefs.cpp` y `src/prefs.h`: persistencia y límites del brillo nocturno.
- `src/lv_conf.h`: configuración Tiny TTF sobreescribible por flags de build.
- `platformio.ini`: perfiles normal, benchmark y Tiny TTF.
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
