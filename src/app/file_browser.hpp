#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace gamehubnx::files {

// Recorrer y ordenar la tarjeta: listar, mover, renombrar y borrar.
//
// El nucleo va aparte de la pantalla para poder probarlo en el PC, que es donde
// se pueden montar arboles de mentira y comprobar los casos raros. La pantalla
// solo pinta lo que sale de aqui.
//
// Deliberadamente NO hay borrado recursivo. Un gestor de ficheros con permiso
// para vaciar carpetas en la raiz de la SD es lo mas peligroso que podria tener
// esta aplicacion, y el caso de uso que lo pedia --recolocar ROMs que cayeron
// en la carpeta equivocada-- se resuelve con ficheros sueltos.

// Tope de ruta de la consola. Pasarse no da un error claro: la llamada falla
// sin mas, o peor, se trunca. Se comprueba antes de tocar nada.
inline constexpr size_t kMaxPathBytes = 768;

struct Entry {
    std::string name;
    bool directory = false;
    uint64_t size = 0;
};

inline bool pathWithinLimit(const std::string& path) {
    return !path.empty() && path.size() < kMaxPathBytes;
}

// Une carpeta y nombre sin duplicar la barra. "sdmc:/" + "tico" -> "sdmc:/tico"
inline std::string joinPath(const std::string& directory,
                            const std::string& name) {
    if (directory.empty())
        return name;
    if (directory.back() == '/')
        return directory + name;
    return directory + "/" + name;
}

// Carpeta que contiene a esta. En la raiz de un volumen se devuelve la misma
// raiz: por encima de "sdmc:/" no hay nada a lo que subir, y devolver ""
// dejaria la pantalla apuntando a ninguna parte.
inline std::string parentPath(const std::string& path) {
    if (path.empty())
        return path;
    std::string trimmed = path;
    while (trimmed.size() > 1 && trimmed.back() == '/')
        trimmed.pop_back();
    const size_t colon = trimmed.find(':');
    const size_t slash = trimmed.find_last_of('/');
    if (slash == std::string::npos)
        return path;
    // "sdmc:/algo" -> el corte cae justo tras los dos puntos: es la raiz.
    if (colon != std::string::npos && slash <= colon + 1)
        return trimmed.substr(0, slash + 1);
    if (slash == 0)
        return "/";
    return trimmed.substr(0, slash);
}

// Cierto cuando ya no se puede subir mas.
inline bool isRootPath(const std::string& path) {
    return parentPath(path) == path;
}

// Comparacion sin distinguir mayusculas, que es como espera verlo cualquiera
// que mire una lista de ficheros.
inline bool nameLess(const std::string& a, const std::string& b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const unsigned char ca =
            static_cast<unsigned char>(std::tolower(
                static_cast<unsigned char>(a[i])));
        const unsigned char cb =
            static_cast<unsigned char>(std::tolower(
                static_cast<unsigned char>(b[i])));
        if (ca != cb)
            return ca < cb;
    }
    return a.size() < b.size();
}

// Contenido de una carpeta: primero las carpetas y luego los ficheros, cada
// grupo por orden alfabetico. Es el orden que espera quien navega, no el que
// devuelve el sistema de ficheros, que no tiene ninguno.
inline bool listDirectory(const std::string& path, std::vector<Entry>& out,
                          std::string& error) {
    out.clear();
    if (!pathWithinLimit(path)) {
        error = "Ruta demasiado larga";
        return false;
    }
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        error = "No se pudo abrir " + path;
        return false;
    }
    while (struct dirent* item = readdir(dir)) {
        const std::string name = item->d_name;
        if (name == "." || name == "..")
            continue;
        Entry entry;
        entry.name = name;
        struct stat st {};
        // Se pregunta a stat y no a d_type: no todos los sistemas de ficheros
        // rellenan ese campo, y una carpeta tomada por fichero se quedaria sin
        // poder abrirse.
        if (stat(joinPath(path, name).c_str(), &st) == 0) {
            entry.directory = S_ISDIR(st.st_mode);
            entry.size = entry.directory
                ? 0 : static_cast<uint64_t>(st.st_size);
        }
        out.push_back(std::move(entry));
    }
    closedir(dir);
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.directory != b.directory)
            return a.directory;
        return nameLess(a.name, b.name);
    });
    return true;
}

// Borra UN fichero. Nunca una carpeta y nunca en cascada: ver la nota de
// arriba. Si el destino no es un fichero normal se dice y no se toca.
inline bool removeFile(const std::string& path, std::string& error) {
    if (!pathWithinLimit(path)) {
        error = "Ruta demasiado larga";
        return false;
    }
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) {
        error = "No existe " + path;
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        error = "Solo se pueden borrar ficheros";
        return false;
    }
    if (unlink(path.c_str()) != 0) {
        error = "No se pudo borrar " + path;
        return false;
    }
    return true;
}

// Borra una carpeta VACIA. Es la unica forma de carpeta que se puede quitar sin
// riesgo de llevarse por delante algo que no se ha visto.
inline bool removeEmptyDirectory(const std::string& path, std::string& error) {
    if (!pathWithinLimit(path)) {
        error = "Ruta demasiado larga";
        return false;
    }
    struct stat st {};
    if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        error = "No es una carpeta";
        return false;
    }
    if (rmdir(path.c_str()) != 0) {
        error = "La carpeta no esta vacia";
        return false;
    }
    return true;
}

// Mueve o renombra un fichero.
//
// rename() primero: dentro de la misma tarjeta es instantaneo y no duplica
// bytes, que en una ROM de gigabytes es la diferencia entre un parpadeo y
// llenar la SD. Solo si falla --tipicamente porque origen y destino estan en
// volumenes distintos-- se copia y se borra el original.
//
// Y no se pisa nada: si en el destino ya hay algo, se dice y se para. Machacar
// un fichero en silencio es justo lo que no puede hacer un gestor de ficheros.
inline bool moveFile(const std::string& from, const std::string& to,
                     std::string& error) {
    if (!pathWithinLimit(from) || !pathWithinLimit(to)) {
        error = "Ruta demasiado larga";
        return false;
    }
    if (from == to)
        return true;
    struct stat src {};
    if (stat(from.c_str(), &src) != 0 || !S_ISREG(src.st_mode)) {
        error = "Solo se pueden mover ficheros";
        return false;
    }
    struct stat dst {};
    if (stat(to.c_str(), &dst) == 0) {
        error = "Ya hay algo con ese nombre";
        return false;
    }
    if (rename(from.c_str(), to.c_str()) == 0)
        return true;
    std::ifstream in(from, std::ios::binary);
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!in || !out) {
        error = "No se pudo escribir " + to;
        return false;
    }
    out << in.rdbuf();
    out.flush();
    if (!out.good()) {
        error = "Se corto la copia";
        out.close();
        // La copia a medias no se queda: seria un fichero roto con nombre de
        // bueno.
        unlink(to.c_str());
        return false;
    }
    in.close();
    out.close();
    unlink(from.c_str());
    return true;
}

} // namespace gamehubnx::files
