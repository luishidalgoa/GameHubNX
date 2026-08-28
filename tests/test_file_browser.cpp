#include "app/file_browser.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <sys/stat.h>

using namespace gamehubnx;

namespace {

const std::string kRoot = "/tmp/gamehubnx-file-browser-test";

void writeFile(const std::string& path, const std::string& body) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << body;
}

void test_rutas() {
    assert(files::joinPath("sdmc:/tico", "roms") == "sdmc:/tico/roms");
    // Sin duplicar la barra cuando la carpeta ya termina en una.
    assert(files::joinPath("sdmc:/", "tico") == "sdmc:/tico");

    assert(files::parentPath("sdmc:/tico/roms") == "sdmc:/tico");
    assert(files::parentPath("sdmc:/tico") == "sdmc:/");
    // Por encima de la raiz no hay nada: se devuelve la misma raiz en vez de
    // dejar la pantalla apuntando a ninguna parte.
    assert(files::parentPath("sdmc:/") == "sdmc:/");
    assert(files::isRootPath("sdmc:/"));
    assert(!files::isRootPath("sdmc:/tico"));
    // Una barra sobrante al final no cambia a donde se sube.
    assert(files::parentPath("sdmc:/tico/roms/") == "sdmc:/tico");

    // El tope de ruta de la consola se comprueba antes de tocar nada: pasarse
    // no da un error claro, falla sin mas o se trunca.
    assert(files::pathWithinLimit("sdmc:/algo"));
    assert(!files::pathWithinLimit(std::string(files::kMaxPathBytes, 'a')));
    assert(!files::pathWithinLimit(""));
}

void test_listar() {
    assert(system(("rm -rf " + kRoot).c_str()) != -1);
    assert(mkdir(kRoot.c_str(), 0755) == 0);
    assert(mkdir((kRoot + "/zeta").c_str(), 0755) == 0);
    assert(mkdir((kRoot + "/alfa").c_str(), 0755) == 0);
    writeFile(kRoot + "/Mario.nds", "12345");
    writeFile(kRoot + "/animal.nds", "123");

    std::vector<files::Entry> entries;
    std::string error;
    assert(files::listDirectory(kRoot, entries, error));
    assert(entries.size() == 4);

    // Primero las carpetas y luego los ficheros, cada grupo por orden y sin
    // distinguir mayusculas: es como espera verlo quien navega, no el orden en
    // que los devuelve el sistema de ficheros, que no tiene ninguno.
    assert(entries[0].name == "alfa" && entries[0].directory);
    assert(entries[1].name == "zeta" && entries[1].directory);
    assert(entries[2].name == "animal.nds" && !entries[2].directory);
    assert(entries[3].name == "Mario.nds" && !entries[3].directory);
    assert(entries[3].size == 5);
    // Una carpeta no reporta tamano: el de la entrada de directorio no dice
    // nada de lo que hay dentro y ensenarlo confundiria.
    assert(entries[0].size == 0);

    // Una carpeta que no existe se dice, no se peta.
    assert(!files::listDirectory(kRoot + "/no-existe", entries, error));
    assert(!error.empty());
}

void test_borrar() {
    std::string error;
    writeFile(kRoot + "/borrame.txt", "x");
    assert(files::removeFile(kRoot + "/borrame.txt", error));
    struct stat st {};
    assert(stat((kRoot + "/borrame.txt").c_str(), &st) != 0);

    // Una carpeta NO se borra con esto, ni aunque este vacia: el borrado de
    // carpetas tiene su propia puerta y no se llega a el por descuido.
    assert(!files::removeFile(kRoot + "/alfa", error));
    assert(stat((kRoot + "/alfa").c_str(), &st) == 0);

    // Y lo que no existe se dice.
    assert(!files::removeFile(kRoot + "/fantasma", error));
}

void test_mover_y_renombrar() {
    std::string error;
    assert(mkdir((kRoot + "/destino").c_str(), 0755) == 0);

    // Mover a otra carpeta.
    assert(files::moveFile(kRoot + "/Mario.nds",
                           kRoot + "/destino/Mario.nds", error));
    struct stat st {};
    assert(stat((kRoot + "/destino/Mario.nds").c_str(), &st) == 0);
    assert(st.st_size == 5);
    assert(stat((kRoot + "/Mario.nds").c_str(), &st) != 0);

    // Renombrar es el mismo movimiento dentro de la carpeta.
    assert(files::moveFile(kRoot + "/destino/Mario.nds",
                           kRoot + "/destino/Mario Kart DS.nds", error));
    assert(stat((kRoot + "/destino/Mario Kart DS.nds").c_str(), &st) == 0);

    // No se pisa nada. Machacar un fichero en silencio es justo lo que no
    // puede hacer un gestor de ficheros.
    writeFile(kRoot + "/otro.nds", "nuevo");
    assert(!files::moveFile(kRoot + "/otro.nds",
                            kRoot + "/destino/Mario Kart DS.nds", error));
    assert(!error.empty());
    // Y el original sigue donde estaba, entero.
    assert(stat((kRoot + "/otro.nds").c_str(), &st) == 0);
    assert(st.st_size == 5);
    // El destino tambien: 5 bytes de "12345", no los de "nuevo".
    assert(stat((kRoot + "/destino/Mario Kart DS.nds").c_str(), &st) == 0);

    // Una carpeta no se mueve con esto.
    assert(!files::moveFile(kRoot + "/destino", kRoot + "/otra", error));

    // Mover algo a si mismo no hace nada y no falla.
    assert(files::moveFile(kRoot + "/otro.nds", kRoot + "/otro.nds", error));
    assert(stat((kRoot + "/otro.nds").c_str(), &st) == 0);
}

void test_borrar_carpeta_con_contenido() {
    assert(system(("rm -rf " + kRoot).c_str()) != -1);
    assert(mkdir(kRoot.c_str(), 0755) == 0);
    const std::string tree = kRoot + "/arbol";
    assert(mkdir(tree.c_str(), 0755) == 0);
    assert(mkdir((tree + "/dentro").c_str(), 0755) == 0);
    writeFile(tree + "/uno.nds", "12345");
    writeFile(tree + "/dentro/dos.nds", "123");
    writeFile(kRoot + "/fuera.nds", "no me toques");

    // Antes de borrar se cuenta, que es lo que se le ensena al usuario. Un
    // "seguro?" a secas no dice nada; "2 ficheros, 8 B" si.
    files::TreeCount count;
    assert(files::countTree(tree, count));
    assert(count.files == 2);
    assert(count.directories == 1);
    assert(count.bytes == 8);

    std::string error;
    assert(files::removeDirectoryTree(tree, error));
    struct stat st {};
    assert(stat(tree.c_str(), &st) != 0);
    // Y lo de al lado sigue donde estaba.
    assert(stat((kRoot + "/fuera.nds").c_str(), &st) == 0);

    // La raiz de un volumen no se toca ni pidiendolo: vaciar la tarjeta entera
    // no es nunca lo que alguien queria hacer, y no hay papelera.
    assert(!files::removeDirectoryTree("sdmc:/", error));
    assert(!error.empty());

    // Un fichero tampoco se borra por esta puerta.
    assert(!files::removeDirectoryTree(kRoot + "/fuera.nds", error));
    assert(stat((kRoot + "/fuera.nds").c_str(), &st) == 0);
}

void test_mover_carpetas() {
    std::string error;
    const std::string a = kRoot + "/carpeta";
    assert(mkdir(a.c_str(), 0755) == 0);
    writeFile(a + "/rom.nds", "x");

    // Renombrar una carpeta se lleva su contenido con ella.
    assert(files::moveEntry(a, kRoot + "/renombrada", /*directory=*/true,
                            error));
    struct stat st {};
    assert(stat((kRoot + "/renombrada/rom.nds").c_str(), &st) == 0);

    // Dentro de si misma, no: dejaria el arbol colgando de ninguna parte.
    assert(!files::moveEntry(kRoot + "/renombrada",
                             kRoot + "/renombrada/otra", true, error));
    assert(!error.empty());

    // Y no se pisa lo que ya hay.
    assert(mkdir((kRoot + "/ocupada").c_str(), 0755) == 0);
    assert(!files::moveEntry(kRoot + "/renombrada", kRoot + "/ocupada", true,
                             error));
    assert(stat((kRoot + "/renombrada/rom.nds").c_str(), &st) == 0);
}

} // namespace

int main() {
    test_rutas();
    test_listar();
    test_borrar();
    test_mover_y_renombrar();
    test_borrar_carpeta_con_contenido();
    test_mover_carpetas();
    assert(system(("rm -rf " + kRoot).c_str()) != -1);
    std::printf("file browser tests passed\n");
    return 0;
}
