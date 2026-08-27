#!/usr/bin/env python3
"""Traduce la pila de un crash report de Atmosphere a nombres de funcion.

Atmosphere escribe la pila como desplazamientos dentro del modulo:

    ReturnAddress[00]:       0000000020e87a38 (gamehubnx + 0x211a38)

Sin el binario con simbolos eso no dice nada. Este script empareja cada
desplazamiento con el gamehubnx.elf del MISMO build --el que publica el CI junto
al .nro-- y saca funcion y, si el ELF trae tablas de linea (-g1, que es lo que
compila el CI), fichero y numero de linea.

    scripts/symbolize_crash.py crash.log gamehubnx.elf

El .elf tiene que ser el del build que corria la consola: los desplazamientos
de otro build apuntan a otro sitio y el resultado seria plausible y falso.
Compara la fecha de 'build=' en gamehubnx.log con la del artefacto.
"""
import re
import shutil
import subprocess
import sys

FRAME = re.compile(r"(ReturnAddress\[\d+\]|User Exception Address|PC):\s+"
                   r"[0-9a-fA-F]+(?:\s+\(gamehubnx \+ (0x[0-9a-fA-F]+)\))?")


def addr2line_binary() -> str:
    for name in ("aarch64-none-elf-addr2line", "aarch64-linux-gnu-addr2line",
                 "addr2line"):
        if shutil.which(name):
            return name
    sys.exit("no encuentro addr2line (prueba con devkitA64 en el PATH)")


def main() -> int:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    report, elf = sys.argv[1], sys.argv[2]
    tool = addr2line_binary()

    with open(report, encoding="utf-8", errors="replace") as fh:
        text = fh.read()

    offsets = [m.group(2) for m in FRAME.finditer(text) if m.group(2)]
    if not offsets:
        sys.exit("el informe no trae marcos 'gamehubnx + 0x...'")

    out = subprocess.run([tool, "-f", "-C", "-i", "-p", "-e", elf, *offsets],
                         capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit(out.stderr.strip() or "addr2line fallo")

    lines = [l for l in out.stdout.splitlines() if l.strip()]
    print(f"{len(offsets)} marcos, de arriba (donde revento) hacia abajo:\n")
    for i, (off, sym) in enumerate(zip(offsets, lines)):
        print(f"  #{i:<2} {off:>10}  {sym}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
