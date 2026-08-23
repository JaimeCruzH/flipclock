#!/usr/bin/env python3
"""Recrea vendor/lvgl9, la copia de LVGL que necesita este proyecto.

vendor/lvgl9 no esta en el repositorio: son 99 MB de codigo de terceros. Lo que
si esta versionado es vendor/lvgl9-library.json, el manifiesto parcheado sin el
cual PlatformIO no compila los fuentes de la libreria (necesita srcDir "." y el
srcFilter; el library.json que trae LVGL de serie no sirve aqui).

    python tools/setup_lvgl.py

Si el proxy de la red rompe la descarga, baja a mano
https://github.com/lvgl/lvgl/archive/refs/tags/v9.5.0.zip, descomprimelo en
vendor/lvgl9 (sin el nivel "lvgl-9.5.0" intermedio) y vuelve a lanzar el script:
detectara los fuentes y se limitara a colocar el library.json.
"""

import io
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

LVGL_VERSION = "9.5.0"
URL = f"https://github.com/lvgl/lvgl/archive/refs/tags/v{LVGL_VERSION}.zip"

ROOT = Path(__file__).resolve().parent.parent
DEST = ROOT / "vendor" / "lvgl9"
PATCHED_MANIFEST = ROOT / "vendor" / "lvgl9-library.json"


def download_and_extract() -> None:
    print(f"Descargando LVGL {LVGL_VERSION}...")
    with urllib.request.urlopen(URL) as resp:
        data = resp.read()
    print(f"  {len(data) / 1e6:.1f} MB. Descomprimiendo en {DEST}...")

    with zipfile.ZipFile(io.BytesIO(data)) as zf:
        prefix = f"lvgl-{LVGL_VERSION}/"
        for member in zf.infolist():
            if not member.filename.startswith(prefix) or member.is_dir():
                continue
            target = DEST / member.filename[len(prefix):]
            target.parent.mkdir(parents=True, exist_ok=True)
            with zf.open(member) as src, open(target, "wb") as dst:
                shutil.copyfileobj(src, dst)


def main() -> int:
    if not PATCHED_MANIFEST.is_file():
        print(f"ERROR: falta {PATCHED_MANIFEST}", file=sys.stderr)
        return 1

    if (DEST / "lvgl.h").is_file():
        print(f"{DEST} ya existe, no se descarga nada.")
    else:
        try:
            download_and_extract()
        except Exception as exc:  # red, proxy, certificados...
            print(f"ERROR al descargar: {exc}", file=sys.stderr)
            print(__doc__.split("Si el proxy")[1], file=sys.stderr)
            return 1

    shutil.copyfile(PATCHED_MANIFEST, DEST / "library.json")
    print("library.json parcheado colocado. Listo para compilar.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
