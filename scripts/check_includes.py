#!/usr/bin/env python3
"""Cabeceras propias que ya no existen.

Nace de un fallo concreto: al borrar el acompanante web quedo un
`#include "app/web_server.hpp"` en main_switch.cpp. Nada de lo que se corre en
el PC lo veia --ese fichero no se compila aqui, le falta medio devkitPro-- asi
que el error se fue al CI y costo una vuelta entera.

Solo mira los includes ENTRE COMILLAS que apuntan a rutas del proyecto. Los de
angulos son del sistema o de vendor y no son asunto suyo.
"""
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIRS = ["src", "tools", "tests"]
# Bases desde las que se resuelve un include entre comillas, ademas del
# directorio del propio fichero.
# Las mismas rutas de inclusion que usan las dos compilaciones. Sin ellas
# saldrian falsos positivos --"../core/dht.h" desde src/ui/settings, "utp.h" en
# el nucleo-- y una comprobacion con ruido no la mira nadie.
BASES = ["src", "src/ui", "tests", "vendor/borealis/library/include",
         "vendor", "vendor/libutp"]

INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)


def resuelve(fichero, destino):
    candidatos = [os.path.join(os.path.dirname(fichero), destino)]
    candidatos += [os.path.join(RAIZ, base, destino) for base in BASES]
    return any(os.path.isfile(c) for c in candidatos)


def main():
    problemas = []
    for d in DIRS:
        for actual, _, ficheros in os.walk(os.path.join(RAIZ, d)):
            for nombre in ficheros:
                if not nombre.endswith((".c", ".h", ".cpp", ".hpp")):
                    continue
                ruta = os.path.join(actual, nombre)
                with open(ruta, encoding="utf-8", errors="replace") as f:
                    texto = f.read()
                for m in INCLUDE.finditer(texto):
                    destino = m.group(1)
                    if resuelve(ruta, destino):
                        continue
                    linea = texto[: m.start()].count("\n") + 1
                    problemas.append(
                        "%s:%d: incluye '%s', que no existe"
                        % (os.path.relpath(ruta, RAIZ), linea, destino))

    for p in problemas:
        print("includes: " + p)
    if problemas:
        print("includes: %d problema(s)" % len(problemas))
        return 1
    print("includes: todas las cabeceras propias existen")
    return 0


if __name__ == "__main__":
    sys.exit(main())
