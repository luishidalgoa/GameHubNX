#include "homebrew_installed.hpp"

#include "app/homebrew_source.hpp"
#include "app/nro_metadata.hpp"

#include <algorithm>
#include <cctype>

#include <dirent.h>
#include <sys/stat.h>

namespace gamehubnx {
namespace {

std::string lower(std::string text) {
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool isNro(const std::string& name) {
    const std::string l = lower(name);
    return l.size() > 4 && l.compare(l.size() - 4, 4, ".nro") == 0;
}

bool isDirectory(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Nombres de un directorio, sin "." ni "..". Vacio si no se puede abrir.
std::vector<std::string> listNames(const std::string& dir) {
    std::vector<std::string> names;
    DIR* handle = opendir(dir.c_str());
    if (!handle)
        return names;
    while (dirent* entry = readdir(handle)) {
        const std::string name = entry->d_name;
        if (name != "." && name != "..")
            names.push_back(name);
    }
    closedir(handle);
    std::sort(names.begin(), names.end());
    return names;
}

// Rellena nombre, autor y version de un .nro. El tag que dejo la app tiene
// prioridad sobre la version del NACP: es lo que comparamos con las releases.
void describe(InstalledNro& nro) {
    NroInfo info;
    std::string error;
    if (readNroInfo(localNroReader(nro.path), info, error)) {
        nro.name = info.name;
        nro.author = info.author;
        nro.version = info.displayVersion;
    }
    if (nro.name.empty())
        nro.name = nro.fileName.substr(0, nro.fileName.size() - 4);
    if (!nro.dirName.empty()) {
        // La version del instalador solo existe en las carpetas que creamos
        // nosotros; en la raiz de switch/ no hay un fichero que sea nuestro.
        const std::string installed = installedHomebrewVersion(nro.path);
        if (!installed.empty()) {
            nro.version = installed;
            nro.versionFromInstaller = true;
        }
    }
}

} // namespace

std::vector<InstalledNro> scanInstalledNros(const std::string& switchRoot) {
    std::vector<InstalledNro> out;
    for (const std::string& entry : listNames(switchRoot)) {
        const std::string path = switchRoot + "/" + entry;
        if (!entry.empty() && entry[0] == '.')
            continue;
        if (!isDirectory(path)) {
            if (!isNro(entry))
                continue;
            InstalledNro nro;
            nro.path = path;
            nro.fileName = entry;
            describe(nro);
            out.push_back(std::move(nro));
            continue;
        }
        for (const std::string& file : listNames(path)) {
            if (!isNro(file))
                continue;
            InstalledNro nro;
            nro.path = path + "/" + file;
            nro.dirName = entry;
            nro.fileName = file;
            describe(nro);
            out.push_back(std::move(nro));
        }
    }
    return out;
}

const InstalledNro* findInstalledNro(const std::vector<InstalledNro>& list,
                                     const std::string& dirName,
                                     const std::string& fileName) {
    const std::string wantFile = lower(fileName);
    const std::string wantDir = lower(dirName);
    for (const InstalledNro& nro : list)
        if (lower(nro.dirName) == wantDir && lower(nro.fileName) == wantFile)
            return &nro;
    for (const InstalledNro& nro : list)
        if (lower(nro.fileName) == wantFile)
            return &nro;
    return nullptr;
}

} // namespace gamehubnx
