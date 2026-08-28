#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace gamehubnx {

// La conversacion de FTP, separada de los sockets.
//
// Aqui vive lo que se puede equivocar de verdad --resolver rutas, no dejar
// salir del arbol permitido, decidir que se puede pedir sin haber entrado-- y
// se puede probar en el PC. El transporte (escuchar, canal de datos, hilos) va
// aparte y no toma ninguna decision.
//
// SOLO LECTURA, y es deliberado. Lo que hacia falta era poder sacar los
// registros y los ficheros de la consola; un servicio de red que ademas pueda
// sobrescribir la tarjeta es una superficie mucho mayor, y para escribir ya
// esta el explorador de ficheros de la propia aplicacion, que pide confirmacion
// y no esta expuesto a la red. Si algun dia hace falta subir, se anade a
// proposito y no de rebote.
class FtpSession {
public:
    // Que tiene que hacer el transporte despues de responder.
    enum class Action {
        None,
        OpenPassive,   // abrir el canal de datos y anunciarlo
        SendListing,   // volcar el listado de `path()` por el canal de datos
        SendFile,      // volcar el fichero de `path()` por el canal de datos
        ReportSize,    // responder "213 <bytes>" de `path()`
        Close,         // cerrar la conexion tras responder
    };

    struct Reply {
        std::string text;             // linea de respuesta, sin el salto
        Action action = Action::None;
    };

    // root: raiz permitida, siempre con prefijo de volumen ("sdmc:").
    // password: vacia = no se pide. Con PIN puesto se exige.
    FtpSession(std::string root, std::string password)
        : root_(normalizeRoot(std::move(root))),
          password_(std::move(password)),
          cwd_(root_) {}

    const std::string& cwd() const { return cwd_; }

    // La ruta TAL Y COMO SE LE ENSENA AL CLIENTE: relativa a la raiz servida y
    // empezando por "/".
    //
    // Dentro se trabaja con "sdmc:/...", que es lo que entiende la consola,
    // pero eso es un detalle del aparato y no puede salir por el cable. Un
    // cliente de FTP espera una ruta de servidor: FileZilla recibia
    // 257 "sdmc:/" y contestaba "Error al analizar la ruta devuelta", y se
    // quedaba sin poder listar nada. curl lo toleraba, que es justamente por lo
    // que no salio antes.
    std::string displayPath(const std::string& internal) const {
        if (internal.size() <= root_.size())
            return "/";
        std::string rest = internal.substr(root_.size());
        if (rest.empty())
            return "/";
        return rest[0] == '/' ? rest : "/" + rest;
    }
    const std::string& path() const { return path_; }
    bool authenticated() const { return authenticated_; }
    bool binary() const { return binary_; }

    Reply handle(const std::string& rawLine) {
        const std::string line = trim(rawLine);
        const size_t space = line.find(' ');
        std::string verb = line.substr(0, space);
        const std::string arg = space == std::string::npos
            ? std::string() : trim(line.substr(space + 1));
        for (char& c : verb)
            c = static_cast<char>(std::toupper(
                static_cast<unsigned char>(c)));

        if (verb == "USER") {
            // El usuario da igual: lo que autoriza es el PIN, si lo hay. Se
            // acepta cualquiera para que valga cualquier cliente.
            if (password_.empty()) {
                authenticated_ = true;
                return {"230 Adelante"};
            }
            return {"331 Hace falta la clave"};
        }
        if (verb == "PASS") {
            if (password_.empty() || arg == password_) {
                authenticated_ = true;
                return {"230 Adelante"};
            }
            // Sin pistas sobre por que falla.
            return {"530 No"};
        }
        if (verb == "QUIT")
            return {"221 Hasta luego", Action::Close};
        if (!authenticated_)
            return {"530 Entra primero"};

        if (verb == "SYST")
            return {"215 UNIX Type: L8"};
        if (verb == "FEAT") {
            // UTF8 se anuncia porque los nombres van en UTF-8 tal cual salen de
            // la tarjeta. Sin anunciarlo, FileZilla avisa de que "el servidor
            // no permite caracteres no ASCII" y puede estropear los nombres con
            // tildes, que en una biblioteca en espanol son muchos.
            return {"211-Extensiones\r\n SIZE\r\n UTF8\r\n211 Fin"};
        }
        if (verb == "OPTS") {
            std::string opcion = arg;
            for (char& c : opcion)
                c = static_cast<char>(std::toupper(
                    static_cast<unsigned char>(c)));
            // Ya se sirve en UTF-8, asi que activarlo es aceptar y seguir. No
            // se acepta apagarlo: no hay otra codificacion que ofrecer.
            if (opcion == "UTF8 ON" || opcion == "UTF8")
                return {"200 Hecho"};
            return {"501 Opcion desconocida"};
        }
        if (verb == "NOOP")
            return {"200 Aqui sigo"};
        if (verb == "TYPE") {
            // El modo texto traduciria saltos de linea y corromperia un NRO.
            // Se acepta la peticion pero se transfiere siempre en binario.
            binary_ = !(arg == "A" || arg == "a");
            return {"200 Modo fijado"};
        }
        if (verb == "PWD")
            return {"257 \"" + displayPath(cwd_) + "\""};
        if (verb == "CWD") {
            std::string target;
            if (!resolve(arg, target))
                return {"550 Ruta no permitida"};
            cwd_ = target;
            return {"250 Hecho"};
        }
        if (verb == "CDUP") {
            cwd_ = parentOf(cwd_);
            return {"250 Hecho"};
        }
        if (verb == "PASV")
            return {"", Action::OpenPassive};
        if (verb == "LIST" || verb == "NLST") {
            std::string target = cwd_;
            if (!arg.empty() && arg[0] != '-' && !resolve(arg, target))
                return {"550 Ruta no permitida"};
            path_ = target;
            return {"150 Listado", Action::SendListing};
        }
        if (verb == "RETR" || verb == "SIZE") {
            std::string target;
            if (arg.empty() || !resolve(arg, target))
                return {"550 Ruta no permitida"};
            path_ = target;
            // El tamano lo mide el transporte, que es quien toca el disco. La
            // sesion no puede inventarselo: antes devolvia la palabra "SIZE"
            // como si fuera un numero y el cliente se quedaba sin saber cuanto
            // iba a bajar.
            if (verb == "SIZE")
                return {"", Action::ReportSize};
            return {"150 Enviando", Action::SendFile};
        }
        // Todo lo que escribe se rechaza explicitamente, en vez de con un
        // "comando desconocido": asi el que lo intente sabe que la respuesta es
        // no, no que le falte soporte.
        if (verb == "STOR" || verb == "STOU" || verb == "APPE" ||
            verb == "DELE" || verb == "RMD" || verb == "MKD" ||
            verb == "RNFR" || verb == "RNTO")
            return {"550 Este servidor es de solo lectura"};
        return {"502 No implementado"};
    }

private:
    static std::string trim(const std::string& value) {
        const size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};
        const size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    // La raiz se guarda sin barra final salvo que sea la del volumen.
    static std::string normalizeRoot(std::string value) {
        while (value.size() > 1 && value.back() == '/' &&
               !(value.size() >= 2 && value[value.size() - 2] == ':'))
            value.pop_back();
        return value;
    }

    std::string parentOf(const std::string& path) const {
        if (path == root_)
            return root_;
        const size_t slash = path.find_last_of('/');
        if (slash == std::string::npos)
            return root_;
        const std::string up = path.substr(0, slash);
        // Nunca por encima de la raiz, aunque el cliente insista.
        return within(up) ? up : root_;
    }

    bool within(const std::string& path) const {
        if (path == root_)
            return true;
        if (path.size() <= root_.size())
            return false;
        if (path.compare(0, root_.size(), root_) != 0)
            return false;
        // Que "sdmc:/switchXX" no cuele como hijo de "sdmc:/switch".
        return path[root_.size()] == '/' || root_.back() == '/';
    }

    // Convierte lo que pide el cliente en una ruta absoluta dentro de la raiz.
    // Devuelve false --y no toca `out`-- si se sale, de la forma que sea.
    bool resolve(const std::string& request, std::string& out) const {
        if (request.empty())
            return false;
        std::string base = request[0] == '/' ? root_ : cwd_;
        std::string rest = request[0] == '/' ? request.substr(1) : request;

        std::vector<std::string> parts;
        // Se parte del punto de arranque para que ".." no pueda subir mas alla:
        // se resuelve sobre segmentos, no pegando cadenas.
        std::string prefix = base;
        size_t start = 0;
        while (start <= rest.size()) {
            const size_t slash = rest.find('/', start);
            const std::string piece = rest.substr(
                start, slash == std::string::npos ? std::string::npos
                                                  : slash - start);
            if (piece == "..") {
                if (prefix == root_)
                    return false;   // intentar salir es un error, no un no-op
                prefix = parentOf(prefix);
            } else if (!piece.empty() && piece != ".") {
                // Una barra invertida o un cero dentro del nombre no se
                // negocian: son intentos de escapar del arbol.
                if (piece.find('\\') != std::string::npos ||
                    piece.find('\0') != std::string::npos)
                    return false;
                prefix = prefix.back() == '/' ? prefix + piece
                                              : prefix + "/" + piece;
            }
            if (slash == std::string::npos)
                break;
            start = slash + 1;
        }
        if (!within(prefix))
            return false;
        out = prefix;
        return true;
    }

    std::string root_;
    std::string password_;
    std::string cwd_;
    std::string path_;
    bool authenticated_ = false;
    bool binary_ = true;
};

} // namespace gamehubnx
