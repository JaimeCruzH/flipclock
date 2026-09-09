# Instrucciones específicas del proyecto

## PlatformIO y consola

- Ejecuta las tareas de PlatformIO desde PowerShell con `PYTHONIOENCODING=utf-8` y `PYTHONUTF8=1` para evitar fallos al imprimir progreso Unicode.
- Para cargar un firmware, no des la carga por terminada hasta comprobar `SUCCESS` en la salida de PlatformIO. Si un intento deja `COM8` ocupado, identifica y cierra solo la cadena de procesos de ese intento antes de reintentar.
