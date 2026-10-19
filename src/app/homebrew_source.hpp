#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gamehubnx {

// Una version publicada de una app homebrew: un release de GitHub con su .nro.
//
// La app no guarda ningun catalogo de homebrew. Lee los releases del repo
// directamente, asi que una version nueva aparece en cuanto se publica en
// GitHub, sin tocar nada en nuestro servidor.
struct HomebrewRelease {
    std::string tag;        // tal cual en GitHub: "v4.0.5"
    std::string version;    // el tag sin la "v" inicial, para comparar
    std::string nroName;    // nombre del asset: "nextendo.nro"
    std::string nroUrl;     // enlace de descarga directa del asset
    uint64_t nroSize = 0;
    std::string publishedAt; // ISO 8601 tal cual lo publica GitHub
    std::string notes;      // cuerpo del release (markdown)
    bool prerelease = false;

    // Paquetes de HB App Store: un zip en vez de un .nro suelto. El zip se
    // extrae en la raiz de la SD, y `binaryPath` es donde queda el .nro.
    std::string archiveUrl;  // zips/<nombre>.zip del repo
    std::string archiveMd5;  // md5 publicado en el repo.json
    std::string binaryPath;  // "/switch/app/app.nro", relativa a sdmc:

    bool isArchive() const { return !archiveUrl.empty(); }
};

// Una app del catalogo: un repo de GitHub y sus releases, mas recientes primero.
struct HomebrewApp {
    std::string repo;       // "owner/name" en GitHub; el nombre del paquete en HB
    std::string title;      // nombre legible (solo HB App Store)
    std::string author;
    std::string description;
    std::string pageUrl;    // https://github.com/owner/name
    std::vector<HomebrewRelease> releases;

    // Lo que se muestra: el titulo si lo hay, si no el identificador del repo.
    std::string displayName() const { return title.empty() ? repo : title; }

    // Release que se ofrece por defecto: el mas reciente que no sea prerelease;
    // si todos lo son, el primero. Nulo si el repo no tiene ninguno usable.
    const HomebrewRelease* recommended() const;
};

// Compara dos versiones numericamente ("1.10.0" > "1.9.2"), ignorando una "v"
// inicial. Negativo si a < b, cero si son iguales, positivo si a > b. Lo que
// sigue a un guion o a un espacio (sufijos de prerelease) no se compara.
int compareHomebrewVersions(const std::string& a, const std::string& b);

// Pide la respuesta cruda de /repos/{owner}/{repo}/releases, sin interpretarla.
// Es lo que guarda la cache en disco.
bool downloadGithubReleases(const std::string& repo, std::string& body,
                            std::string& error);

// Interpreta la respuesta de /repos/{owner}/{repo}/releases. Descarta los drafts
// y los releases sin ningun asset .nro. Devuelve false si el JSON no es una
// lista de releases.
bool parseGithubReleases(const std::string& json,
                         std::vector<HomebrewRelease>& out,
                         std::string& error);

// Repos que la app consulta de serie. El servidor de GameHub puede añadir mas.
std::vector<std::string> defaultHomebrewRepos();

// Lista de repos que publica GameHub (/api/shop/homebrew). La app la usa en vez
// de defaultHomebrewRepos() cuando la consigue; si no, se queda con la fija.
std::string homebrewReposUrl();
bool downloadHomebrewRepoList(std::string& body, std::string& error);
// Acepta solo "owner/name" con caracteres de GitHub; lo demas se descarta.
bool parseHomebrewRepoList(const std::string& json,
                           std::vector<std::string>& out, std::string& error);

// Pide los releases de un repo a la API de GitHub. Sin autenticar, GitHub
// limita a 60 peticiones por hora y por IP; por eso el llamador debe cachear.
bool fetchGithubReleases(const std::string& repo, HomebrewApp& app,
                         std::string& error);

// Repo Switch de HB App Store: su repo.json esta en <url>/repo.json y los zips
// en <url>/zips/<nombre>.zip.
std::string homebrewSwitchRepoUrl();

// Interpreta el repo.json de HB App Store. Cada paquete pasa a ser una app con
// un unico release: su version, con el zip y el md5 que publica el repo.
bool parseHbRepo(const std::string& json, const std::string& baseUrl,
                 std::vector<HomebrewApp>& out, std::string& error);

// Descarga el repo.json en crudo, sin interpretarlo.
bool downloadHbRepo(const std::string& baseUrl, std::string& body,
                    std::string& error);

// Descarga y interpreta el repo.json completo (unos 1,6 MB para el repo Switch).
bool fetchHbRepo(const std::string& baseUrl, std::vector<HomebrewApp>& out,
                 std::string& error);

// Ruta de instalacion de un release: sdmc:/switch/<nombre>/<nombre>.nro, la
// convencion del Homebrew Menu para que la app aparezca en su lista.
std::string homebrewNroPath(const HomebrewRelease& release);

// Version instalada de un .nro, leida del fichero de version que deja la app
// al instalarlo. Vacia si la app no la instalo nosotros.
std::string installedHomebrewVersion(const std::string& nroPath);

// Descarga el .nro del release a su ruta de instalacion. Escribe primero a un
// fichero temporal y lo renombra al final: un corte a mitad no deja un .nro roto
// donde el Homebrew Menu lo busque.
bool installHomebrewRelease(const HomebrewRelease& release,
                            std::string& installedPath, std::string& error);

} // namespace gamehubnx
