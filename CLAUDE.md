# Notas para Claude Code en este proyecto

## Cómo escribir ficheros aquí (leer antes de tocar nada)

Esto no son buenas prácticas genéricas: es el recuento de los fallos de escritura
que se repitieron durante el desarrollo de este reloj, sacados de los transcripts
de las sesiones (1237 resultados de herramienta analizados). El número entre
paréntesis es las veces que ocurrió cada uno.

### Regla general

**Para crear o modificar un fichero, usa las herramientas `Write` y `Edit`.**
No uses `cat > fichero <<'EOF'`, ni `sed -i`, ni `perl -pi -e`, ni scripts de
parcheo por stdin. Todos los fallos de abajo vienen de haber usado la shell como
editor. La shell es para ejecutar cosas, no para escribir código.

---

### 1. El `\n` que se convierte en un salto de línea real (10 veces)

El más frecuente y el más caro, porque no falla al escribir: falla mucho después,
al compilar, y el mensaje no apunta a la causa.

Al escribir código C con `printf("...\n")` a través de un heredoc, de `sed` o de
un script intermedio, la secuencia `\n` se interpreta en algún punto de la cadena
y se convierte en un salto de línea de verdad. El literal queda partido en dos
líneas y el compilador responde:

```
src/main.cpp:567:17: error: missing terminating " character
src/main.cpp:568:1: error: missing terminating " character
```

Pasó en `main.cpp`, `lv_port.c`, `mod_audio.cpp` y `flip_card.c`. Es un fichero
corrupto en disco, no un error de compilación: hay que volver a abrir la línea y
repararla a mano.

- **Remedio:** escribe esas líneas con `Edit`, donde `\n` viaja literal.
- Si por lo que sea no hay más remedio que generar la línea desde un script,
  compón la barra invertida aparte (`BS = chr(92)` y luego `BS + "n"`), que es
  como se acabó reparando en su día. Es feo; usa `Edit`.
- **Verifica siempre después:** `grep -n 'printf' fichero.c` y comprueba que cada
  literal empieza y termina en la misma línea.

### 2. Heredoc que se come el resto del fichero (5 veces)

Un heredoc largo con comillas, backticks o `EOF` dentro del contenido se corta o
se traga lo que viene detrás. Es el "el editor cortaba la información": el
fichero queda escrito **a medias** y sin aviso. Ocurrió escribiendo
`gen_assets.py` y un documento largo.

- **Remedio:** `Write` para el fichero entero. No hay límite de comillas ni de
  contenido.

### 3. `sed` / `perl` con patrones que llevan `\` o `|` (4 veces)

```
sed: -e expression #1, char 148: unterminated `s' command
Substitution pattern not terminated at -e line 1.
```

Pasó intentando sustituir líneas de macros de `lv_port.h`, que acaban en `\`. El
delimitador y las barras invertidas del patrón chocan entre sí.

- **Remedio:** `Edit` con `old_string` / `new_string` exactos. Sin escapes.

### 4. Parchear con un script cuyo patrón ya no existe (8 veces)

Los scripts de parcheo por stdin que buscaban un texto y fallaban con
`AssertionError`, o peor, que **no encontraban nada y no fallaban**, dejando el
fichero sin tocar mientras yo daba el cambio por hecho.

- **Remedio:** `Edit` falla ruidosamente si el `old_string` no existe o no es
  único. Esa garantía es justamente lo que se quiere.

### 5. Cortar un fichero por número de línea (visto en esta sesión)

`tail -n +19 platformio.ini` para conservar la cola de un fichero se comió la
línea `[env:esp32-s3-display]`, y el resultado siguió pareciendo un `.ini`
válido. Contar líneas a ojo se equivoca en uno con facilidad.

- **Remedio:** `Edit` sobre el bloque concreto. Si de verdad hay que reconstruir
  un fichero, **verifica el resultado**: para `platformio.ini` existe
  `pio project config`, que imprime la configuración ya resuelta.

### 6. El `cd` que sobrevive entre llamadas (4 veces)

El directorio de trabajo de una terminal puede no coincidir con la raíz del
proyecto entre comandos.

- **Remedio:** fija siempre `workdir` en la herramienta o define
  `$projectRoot = (Resolve-Path .).Path` al principio de cada bloque de PowerShell.

### 7. Comprobar después de escribir

Ningún método de escritura garantiza que el contenido sea correcto. Tras tocar un
fichero, léelo o valídalo con la herramienta que corresponda:

| Fichero | Comprobación |
|---|---|
| `platformio.ini` | `pio project config` |
| `tools/*.py` | `python -c "import ast; ast.parse(open(RUTA, encoding='utf-8').read())"` |
| `src/*.c`, `src/*.cpp` | compilar, o al menos `grep -n printf` buscando literales partidos |

---

## Entorno de esta máquina

- **Python:** usa el entorno funcional de PlatformIO, derivado de
  `$env:USERPROFILE`: `Join-Path $env:USERPROFILE
  '.platformio\penv\Scripts\python.exe'`. `pio` no está en el PATH: invócalo
  como `& $projectPython -m platformio`.
- **Compilar desde PowerShell, nunca desde Git Bash**, y con `PIP_CERT`,
  `SSL_CERT_FILE` y `REQUESTS_CA_BUNDLE` apuntando a
  la ruta local verificada por `Test-Path` en la guía de entorno. El proxy TLS de
  esta red rompe las descargas de PlatformIO.
- **PowerShell:** no manipules `$env:Path` con `+=` en un comando compuesto.
  Invoca el ejecutable por su ruta completa con el operador `&`.
- **Git:** el repo está en `autocrlf` de Windows y `.gitattributes` fija
  `eol=lf`. Los avisos de CRLF al hacer `git add` son normales y no indican
  corrupción.

## El proyecto

- `vendor/lvgl9/` no está versionado; se recrea con
  `& $projectPython tools/setup_lvgl.py`.
- Los sprites de `src/assets/` sí están versionados. Se regeneran con
  `python tools/gen_assets.py --emit`, y `--preview` escribe PNGs en `preview/`
  sin tocar la placa. El generador es determinista: misma entrada, mismos bytes.
- Las imágenes del README viven en `docs/`, no en `preview/`.
