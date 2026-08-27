#!/usr/bin/env python3
"""Traduce la pila de un crash report de Atmosphere a nombres de funcion.

Atmosphere escribe la pila como desplazamientos dentro del modulo:

    ReturnAddress[00]:       0000000020e87a38 (gamehubnx + 0x211a38)

Sin el binario con simbolos eso no dice nada. Este script empareja cada
desplazamiento con el gamehubnx.elf del MISMO build --el que publica el CI junto
al .nro-- y saca funcion y, si el ELF trae tablas de linea (-g1, que es lo que
compila el CI), fichero y numero de linea.

    scripts/symbolize_crash.py crash.log gamehubnx.elf [--todos-los-hilos]

Por defecto solo se simboliza el hilo que revento. El informe trae la pila de
todos los hilos vivos, que son cien y pico marcos de ruido alrededor del unico
que importa.

El ELF tiene que ser el del build que corria la consola. No hay que fiarse de
la memoria: el informe trae el Module Id, que es el build ID del ELF, y aqui se
comparan. Si no coinciden el script se planta, porque los desplazamientos de
otro build dan un resultado plausible y falso, que es peor que ninguno.
"""
import re
import shutil
import subprocess
import sys

# Acepta el nombre viejo del modulo: los informes guardados de antes del
# renombrado siguen diciendo "pipensx".
FRAME = re.compile(r"(?:ReturnAddress\[\d+\]|User Exception Address|PC):\s+"
                   r"[0-9a-fA-F]+(?:\s+\((?:gamehubnx|pipensx) \+ "
                   r"(0x[0-9a-fA-F]+)\))?")
MODULE_ID = re.compile(r"Module Id:\s+([0-9A-Fa-f]{40})")


def tool_named(*names: str) -> str:
    for name in names:
        if shutil.which(name):
            return name
    sys.exit("no encuentro %s (prueba con devkitA64 en el PATH)" % names[0])


def elf_build_id(readelf: str, elf: str) -> str:
    out = subprocess.run([readelf, "-n", elf], capture_output=True, text=True)
    found = re.search(r"([0-9a-f]{40})", out.stdout)
    return found.group(1).lower() if found else ""


def crashed_thread_section(text: str) -> str:
    """El trozo del informe que va del hilo que revento al siguiente bloque.

    Atmosphere pone primero 'Crashed Thread Info' y despues la lista del resto
    de hilos; cortando ahi nos quedamos con la pila que importa."""
    start = text.find("Crashed Thread Info")
    if start < 0:
        return text
    end = text.find("Stack Dump", start)
    return text[start:end if end > 0 else len(text)]


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    todos = "--todos-los-hilos" in sys.argv[1:]
    if len(args) != 2:
        sys.exit(__doc__)
    report, elf = args[0], args[1]
    addr2line = tool_named("aarch64-none-elf-addr2line",
                           "aarch64-linux-gnu-addr2line", "addr2line")
    readelf = tool_named("aarch64-none-elf-readelf",
                         "aarch64-linux-gnu-readelf", "readelf")

    with open(report, encoding="utf-8", errors="replace") as fh:
        text = fh.read()

    # El Module Id del informe son 40 hex del build ID mas relleno a cero.
    reported = MODULE_ID.search(text)
    if reported:
        want = reported.group(1).lower()
        have = elf_build_id(readelf, elf)
        if not have:
            print("aviso: el ELF no trae .note.gnu.build-id, no puedo "
                  "comprobar que sea el build correcto\n", file=sys.stderr)
        elif have != want:
            sys.exit("el ELF no es el del crash:\n"
                     "  informe: %s\n  elf:     %s\n"
                     "Coge el gamehubnx.elf del mismo build." % (want, have))

    scope = text if todos else crashed_thread_section(text)
    offsets = [m.group(1) for m in FRAME.finditer(scope) if m.group(1)]
    if not offsets:
        sys.exit("el informe no trae marcos 'gamehubnx + 0x...'")

    # Una llamada por direccion. Con -i addr2line emite VARIAS lineas por
    # direccion (una por marco inline), asi que pedirlas todas de golpe y
    # emparejarlas por posicion descuadra la pila entera: sale un stack
    # plausible y equivocado. Preguntando de una en una, todo lo que vuelve
    # pertenece a esa direccion y no hay nada que adivinar.
    print("%d marcos%s, de arriba (donde revento) hacia abajo:\n"
          % (len(offsets), "" if todos else " del hilo que revento"))
    for index, offset in enumerate(offsets):
        out = subprocess.run(
            [addr2line, "-f", "-C", "-i", "-p", "-e", elf, offset],
            capture_output=True, text=True)
        if out.returncode != 0:
            sys.exit(out.stderr.strip() or "addr2line fallo")
        lines = [l.strip() for l in out.stdout.splitlines() if l.strip()]
        head = lines[0] if lines else "??"
        print("  #%-2d %10s  %s" % (index, offset, head))
        # El resto son marcos inline dentro de esa misma direccion.
        for inlined in lines[1:]:
            print("  %-14s  %s" % ("", inlined))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
