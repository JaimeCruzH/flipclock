# Medicion y estimacion de bateria

## Fuente de datos

La placa usada es la **Elecrow 3.5" ESP32-S3 Display**. Su entrada de bateria
Li-Po llega al ESP32 mediante un divisor resistivo y se lee por `GPIO8`. La
[especificacion oficial de Elecrow](https://www.elecrow.com/download/product/DLE06235B/3.5inch_IPS_ESP32-S3_Specification.pdf)
describe la entrada de bateria y el canal de medicion.

El firmware reconstruye el voltaje de la bateria duplicando el voltaje medido
en el ADC, porque el divisor reduce la senal antes de llegar al GPIO. El
porcentaje usa la misma aproximacion lineal del ejemplo del fabricante:

- 2.500 mV o menos: 0 %.
- 4.200 mV o mas: 100 %.
- Entre ambos: `(mV - 2500) / 17`.

La placa no proporciona al firmware una medicion directa de corriente en mA.
Por eso la autonomia no se presenta como consumo electrico medido.

## Lo que muestra WIFI

La pantalla muestra tres lineas:

```text
Bateria: 78%  (3,91 V)
Tendencia: [flecha o circulo] Descargando
Autonomia: ~5h 20m  (-15%/h)
```

El voltaje y el porcentaje visibles usan una media movil de las quince ultimas
lecturas validas. Al abrir WIFI se inicia una ventana nueva; durante los
primeros segundos se promedian las muestras disponibles hasta completar quince.

- Flecha verde hacia arriba: el voltaje subio desde la ultima muestra; es
  indicio de carga, no una señal directa `CHRG` del cargador.
- Flecha roja hacia abajo: el voltaje bajo.
- Circulo azul: la variacion esta dentro de la banda estable de 12 mV.

## Como se calcula

1. Se toma una muestra inicial al arrancar.
2. Una tarea de fondo toma otra muestra cada 10 minutos, aunque WIFI no este
   visible.
3. Las muestras que suben reinician el historial de descarga. Las muestras que
   bajan conservan hasta seis puntos descendentes recientes.
4. Con al menos dos puntos descendentes y una caida total de 10 mV o mas, se
   calcula la caida en mV por hora y se convierte a porcentaje por hora.
5. La autonomia se estima prolongando esa caida hasta el limite de 2.500 mV.

El historial vive en RAM y se reinicia al reiniciar el dispositivo. En un
arranque nuevo la primera estimacion necesita como minimo la siguiente muestra
de 10 minutos; si las lecturas son estables, tardara mas.

## Limites de la estimacion

El voltaje de una Li-Po cambia con la carga instantanea, la temperatura, la
resistencia interna y el estado de carga. La pantalla del reloj tambien puede
cambiar su consumo. Por eso el valor es una extrapolacion orientativa para la
carga actual, no una garantia de horas restantes.

Para medir duracion de forma comparable:

1. Desconectar USB y dejar conectado solo el acumulador.
2. Mantener el reloj en el uso habitual.
3. Abrir **Configuracion -> WIFI** despues de 10 a 20 minutos.
4. Comparar la autonomia estimada con el tiempo real transcurrido.

Si se necesita corriente real en mA, hace falta añadir un sensor de corriente
o modificar el hardware para llevar una señal de monitorizacion al ESP32.
