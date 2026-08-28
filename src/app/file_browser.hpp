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
// El borrado de carpetas SI arrastra su contenido, por peticion expresa. Es lo
// mas peligroso que hace esta aplicacion, asi que va con tres frenos que no son
// negociables: se cuenta antes lo que se va a llevar por delante y se ensena,
// se prohibe la raiz de un volumen --nadie quiere vaciar la tarjeta entera, y
// ahi no hay vuelta atras-- y hay un tope de profundidad para que un arbol
// enlazado en circulo no se coma la pila.

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

// Cuantos ficheros y cuantos bytes hay bajo una carpeta, contando hacia dentro.
//
// Es lo que se le ensena al usuario ANTES de borrar. Un "¿seguro?" a secas no
// dice nada; "234 ficheros, 18,4 GB" si.
struct TreeCount {
    uint64_t files = 0;
    uint64_t directories = 0;
    uint64_t bytes = 0;
};

inline constexpr int kMaxTreeDepth = 32;

inline bool countTree(const std::string& path, TreeCount& out,
                      int depth = 0) {
    if (depth > kMaxTreeDepth || !pathWithinLimit(path))
        return false;
    DIR* dir = opendir(path.c_str());
    if (!dir)
        return false;
    bool ok = true;
    while (struct dirent* item = readdir(dir)) {
        const std::string name = item->d_name;
        if (name == "." || name == "..")
            continue;
        const std::string child = joinPath(path, name);
        struct stat st {};
        if (stat(child.c_str(), &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            ++out.directories;
            if (!countTree(child, out, depth + 1))
                ok = false;
        } else {
            ++out.files;
            out.bytes += static_cast<uint64_t>(st.st_size);
        }
    }
    closedir(dir);
    return ok;
}

// Borra una carpeta y todo lo que hay dentro.
//
// La raiz de un volumen no se toca ni pidiendolo: vaciar la tarjeta entera no
// es nunca lo que alguien queria hacer, y no hay papelera de la que sacarlo.
inline bool removeDirectoryTree(const std::string& path, std::string& error,
                                int depth = 0) {
    if (!pathWithinLimit(path)) {
        error = "Ruta demasiado larga";
        return false;
    }
    if (depth == 0 && isRootPath(path)) {
        error = "No se puede borrar la raiz de la tarjeta";
        return false;
    }
    if (depth > kMaxTreeDepth) {
        error = "Carpetas demasiado anidadas";
        return false;
    }
    struct stat st {};
    if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        error = "No es una carpeta";
        return false;
    }
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        error = "No se pudo abrir " + path;
        return false;
    }
    std::vector<std::string> children;
    while (struct dirent* item = readdir(dir)) {
        const std::string name = item->d_name;
        if (name == "." || name == "..")
            continue;
        children.push_back(name);
    }
    closedir(dir);
    // Se cierra el directorio ANTES de tocarlo: borrar mientras se recorre con
    // readdir es comportamiento indefinido, y en FAT32 se salta entradas.
    for (const std::string& name : children) {
        const std::string child = joinPath(path, name);
        struct stat cst {};
        if (stat(child.c_str(), &cst) != 0)
            continue;
        if (S_ISDIR(cst.st_mode)) {
            if (!removeDirectoryTree(child, error, depth + 1))
                return false;
        } else if (unlink(child.c_str()) != 0) {
            error = "No se pudo borrar " + child;
            return false;
        }
    }
    if (rmdir(path.c_str()) != 0) {
        error = "No se pudo borrar " + path;
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

// Renombra o mueve tambien carpetas.
//
// Para una carpeta solo vale rename(): copiar un arbol entre volumenes seria
// otra cosa --y con la SD de por medio, otra cosa que puede tardar horas--. Si
// falla se dice, en vez de simularlo a medias.
inline bool moveEntry(const std::string& from, const std::string& to,
                      bool directory, std::string& error) {
    if (!directory)
        return moveFile(from, to, error);
    if (!pathWithinLimit(from) || !pathWithinLimit(to)) {
        error = "Ruta demasiado larga";
        return false;
    }
    if (from == to)
        return true;
    struct stat dst {};
    if (stat(to.c_str(), &dst) == 0) {
        error = "Ya hay algo con ese nombre";
        return false;
    }
    // Mover una carpeta DENTRO de si misma dejaria el arbol colgando de ninguna
    // parte. rename() lo rechaza, pero decirlo aqui da un motivo entendible.
    if (to.size() > from.size() && to.compare(0, from.size(), from) == 0 &&
        to[from.size()] == '/') {
        error = "No se puede mover una carpeta dentro de si misma";
        return false;
    }
    if (rename(from.c_str(), to.c_str()) != 0) {
        error = "No se pudo mover la carpeta";
        return false;
    }
    return true;
}

} // namespace gamehubnx::files
