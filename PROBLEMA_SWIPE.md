# RESUELTO: el deslizamiento (swipe) no funcionaba

Cerrado el 23/08/2026. Se deja escrito porque el diagnostico que figuraba aqui
antes era **incorrecto**, y la unica forma de dar con la causa fue medir en el
dispositivo en vez de razonar sobre el codigo.

## Sintoma original

- Deslizar a la izquierda en el reloj abria el tiempo, pero solo a veces.
- Deslizar a la derecha en el tiempo no funcionaba nunca.
- Los toques simples y la pulsacion larga funcionaban siempre.

## Las DOS causas reales

### 1. La tarea de LVGL dormia medio segundo cuando mas ocupada estaba

En `src/lv_port.c`, `lvgl_port_task()`, codigo del fabricante:

```c
task_delay_ms = lv_timer_handler();
if ((task_delay_ms > task_max_sleep_ms) || (1 == task_delay_ms)) {
    task_delay_ms = task_max_sleep_ms;      /* 500 ms */
}
```

Cuando `lv_timer_handler()` contesta "vuelve en 1 ms" -es decir, cuando hay
trabajo pendiente- el port se dormia `task_max_sleep_ms`, que vale 500.

Consecuencia medida por Serial: el lector del tactil se llamaba **2 o 3 veces
por segundo** en vez de cada 16 ms, con huecos limpios de 510 ms. Un gesto
entero se resolvia con dos muestras: el punto inicial y el final, y el final
llegaba con la coordenada del principio. Ningun detector de deslizamiento podia
funcionar con eso. La animacion del flip tambien estaba estrangulada.

Corregido: el maximo solo se aplica cuando de verdad no hay trabajo
(`LV_NO_TIMER_READY`), con un suelo de `LVGL_PORT_TASK_MIN_DELAY_MS` = 2 ms.
Tras el cambio, el tactil se lee cada 20 ms con coordenadas suaves.

### 2. El detector estaba enganchado al widget equivocado

`swipe_attach()` se colgaba de un objeto de fondo. Pero LVGL entrega
`LV_EVENT_PRESSED` / `RELEASED` **solo al objeto pulsado**, y no los hace subir
al padre. Si el dedo empezaba sobre una tarjeta del reloj o sobre la franja del
tiempo, el detector ni se enteraba. `LV_OBJ_FLAG_GESTURE_BUBBLE` no ayuda:
sube el `GESTURE`, no el `PRESSED`.

Se vio en la captura: los unicos deslizamientos detectados empezaban en x=5 y
x=9, el borde izquierdo, lo unico que no tapan las tarjetas.

Corregido: `src/swipe.c` se engancha ahora al **dispositivo de entrada** con
`lv_indev_add_event_cb()`. LVGL manda `PRESSED`/`RELEASED` al indev **antes**
que al objeto y siempre (`send_event()` en `lv_indev.c`), asi que el gesto se ve
caiga donde caiga el dedo. Se registra una pantalla con su callback y el gesto
se entrega solo si esa pantalla es la activa.

## El diagnostico anterior, y por que era falso

La version anterior de este documento daba por probada esta causa: que
`lvgl_port_touchpad_read()` no escribia en `data` cuando no habia interrupcion,
y que como LVGL hace `lv_memzero(data, ...)` antes de llamarlo, cada lectura sin
interrupcion se leia como `RELEASED`.

**El razonamiento es correcto y el fallo era real**, asi que la correccion se
mantiene (ahora se escribe `data` siempre, conservando el ultimo estado). Pero
no era la causa del sintoma: con el lector llamandose 2 veces por segundo daba
igual lo que escribiera.

Un intento intermedio -sondear el tactil en cada lectura, ignorando la
interrupcion- **empeoro** las cosas de "a veces" a 1 de cada 10, porque leia
mientras el controlador escribia el reporte y devolvia coordenadas rotas. La
interrupcion no es un capricho: es la senal de "reporte completo y estable". El
lector actual lee solo tras interrupcion, con una red de seguridad de 200 ms por
si se perdiera la del "dedo fuera".

## Leccion

Tres hipotesis razonadas sobre el codigo fueron a peor o a ningun sitio. Lo que
resolvio el caso fue instrumentar y capturar por Serial: primero las
coordenadas crudas del bus I2C, y despues -decisivo- **marcas de tiempo de cada
llamada al lector**, que enseguida enseñaron los huecos de 510 ms.

## Estado

Deslizamiento a izquierda y derecha: 10 de 10 en ambos sentidos. Pulsacion
larga, ajustes, botones, deslizador y teclado sin regresiones. Un toque simple
no cambia de pantalla. Flash 61,6 %, RAM 14,8 %.

Pendiente: **Fase 4**, pestaña de ajustes para cambiar de comuna (busqueda por
nombre con el geocoding de Open-Meteo y guardado en NVS).

## Compilar y subir

Desde **PowerShell**, nunca Git Bash:

```powershell
$projectRoot = (Resolve-Path .).Path
$projectPython = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
$caBundle = Join-Path (Split-Path $projectRoot -Parent) 'win-ca-bundle.pem'
$env:PIP_CERT=$caBundle
$env:SSL_CERT_FILE=$env:PIP_CERT; $env:REQUESTS_CA_BUNDLE=$env:PIP_CERT
& $projectPython -m platformio run -d $projectRoot -t upload
```
