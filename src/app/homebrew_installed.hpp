#pragma once

#include <string>
#include <vector>

namespace gamehubnx {

// Un .nro que hay en la SD bajo switch/, lo hayamos instalado nosotros o no.
struct InstalledNro {
    std::string path;      // sdmc:/switch/<carpeta>/<archivo>.nro
    std::string dirName;   // carpeta bajo switch/; vacia si esta en la raiz
    std::string fileName;  // "tico.nro"
    std::string name;      // nombre del NACP, o el nombre del fichero sin .nro
    std::string author;
    std::string version;   // tag que dejo la app, o la version del NACP
    bool versionFromInstaller = false;
};

// Recorre switch/: los .nro sueltos en la raiz y los .nro de cada carpeta (un
// nivel). Un .nro sin cabecera NRO valida se lista igual, con el nombre del
// fichero. Las carpetas que empiezan por punto se ignoran.
std::vector<InstalledNro> scanInstalledNros(const std::string& switchRoot);

// Busca un .nro instalado por carpeta y nombre de fichero (sin distinguir
// mayusculas). Si no esta en esa carpeta, cualquier .nro con ese nombre cuenta.
const InstalledNro* findInstalledNro(const std::vector<InstalledNro>& list,
                                     const std::string& dirName,
                                     const std::string& fileName);

} // namespace gamehubnx
