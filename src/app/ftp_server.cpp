#include "ftp_server.hpp"

#include "ftp_session.hpp"

extern "C" {
#include "../core/net.h"
#include "../core/util.h"
}

#include "app_paths.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace gamehubnx {
namespace {

// Una sola conexion a la vez. No es una limitacion que moleste --esto lo usa
// una persona para sacar un fichero-- y evita tener que pensar en varias
// sesiones compartiendo la tarjeta.
constexpr int kBacklog = 1;
constexpr size_t kChunk = 64 * 1024;
// Si no llega nada en este tiempo se corta: un cliente que se va sin decir
// adios dejaria el servidor ocupado para siempre.
constexpr int kIdleSeconds = 120;

// El registro de la aplicacion, que necesita un camino propio para leerse.
const char* const kLogPath = GHNX_PATH("gamehubnx.log");

bool sendAll(socket_t fd, const char* data, size_t size,
             const std::atomic<bool>& stopping) {
    size_t sent = 0;
    int idle = 0;
    while (sent < size) {
        if (stopping.load())
            return false;
        const ssize_t n = send(fd, data + sent, size - sent, 0);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            idle = 0;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // El socket es no bloqueante: que no acepte ahora es normal, no un
            // error. Se espera un poco y se reintenta, con tope.
            if (++idle > kIdleSeconds * 100)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        return false;
    }
    return true;
}

bool sendLine(socket_t fd, const std::string& line,
              const std::atomic<bool>& stopping) {
    const std::string framed = line + "\r\n";
    return sendAll(fd, framed.data(), framed.size(), stopping);
}

// Listado en el formato de ls que esperan los clientes de FTP. El tamano y el
// tipo salen de stat; la fecha no se publica --la tarjeta no guarda una util-- y
// se pone una fija, que es mejor que inventar uno distinto en cada listado.
std::string buildListing(const std::string& path) {
    std::string out;
    DIR* dir = opendir(path.c_str());
    if (!dir)
        return out;
    while (struct dirent* item = readdir(dir)) {
        const std::string name = item->d_name;
        if (name == "." || name == "..")
            continue;
        struct stat st {};
        const std::string full =
            path.back() == '/' ? path + name : path + "/" + name;
        if (stat(full.c_str(), &st) != 0)
            continue;
        char line[512];
        std::snprintf(line, sizeof(line),
                      "%crw-rw-rw-   1 switch switch %12llu Jan  1 1970 %s\r\n",
                      S_ISDIR(st.st_mode) ? 'd' : '-',
                      S_ISDIR(st.st_mode)
                          ? 0ull
                          : static_cast<unsigned long long>(st.st_size),
                      name.c_str());
        out += line;
    }
    closedir(dir);
    return out;
}

} // namespace

FtpServer::FtpServer(std::string root, std::string password)
    : root_(std::move(root)), password_(std::move(password)) {}

FtpServer::~FtpServer() { stop(); }

void FtpServer::setPassword(std::string password) {
    password_ = std::move(password);
}

bool FtpServer::start(uint16_t port, std::string& error) {
    if (running_.load())
        return true;
    listener_ = net_tcp_listen(port, kBacklog);
    if (listener_ < 0) {
        error = "No se pudo abrir el puerto " + std::to_string(port);
        return false;
    }
    boundPort_.store(net_local_port(listener_));
    stopping_.store(false);
    running_.store(true);
    thread_ = std::thread([this] { serve(); });
    return true;
}

void FtpServer::stop() {
    if (!running_.load() && !thread_.joinable())
        return;
    stopping_.store(true);
    if (thread_.joinable())
        thread_.join();
    if (listener_ >= 0) {
        net_close(listener_);
        listener_ = -1;
    }
    running_.store(false);
    boundPort_.store(0);
}

void FtpServer::serve() {
    while (!stopping_.load()) {
        const socket_t control = net_accept(listener_, nullptr);
        if (control < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        FtpSession session(root_, password_);
        sendLine(control, "220 GameHubNX", stopping_);

        std::string pending;
        socket_t dataListener = -1;
        int idleTicks = 0;
        bool alive = true;

        while (alive && !stopping_.load()) {
            char buffer[512];
            const ssize_t n = recv(control, buffer, sizeof(buffer), 0);
            if (n == 0)
                break;                       // el cliente cerro
            if (n < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                    break;
                if (++idleTicks > kIdleSeconds * 100)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            idleTicks = 0;
            pending.append(buffer, static_cast<size_t>(n));

            size_t eol;
            while ((eol = pending.find('\n')) != std::string::npos) {
                const std::string line = pending.substr(0, eol);
                pending.erase(0, eol + 1);

                const FtpSession::Reply reply = session.handle(line);

                if (reply.action == FtpSession::Action::OpenPassive) {
                    if (dataListener >= 0)
                        net_close(dataListener);
                    dataListener = net_tcp_listen(0, 1);
                    const uint16_t dataPort =
                        dataListener < 0 ? 0 : net_local_port(dataListener);
                    if (dataPort == 0) {
                        sendLine(control, "425 Sin canal de datos", stopping_);
                        continue;
                    }
                    // La direccion que se anuncia es la DEL SOCKET DE
                    // CONTROL, y es una correccion.
                    //
                    // Antes iba 0,0,0,0 dando por hecho que el cliente
                    // reutilizaria la direccion por la que ya esta hablando.
                    // curl lo hace; FileZilla NO: intenta conectarse a
                    // 0.0.0.0 literalmente y muere con WSAEADDRNOTAVAIL sin
                    // llegar a listar nada. Es el mismo tropiezo que con la
                    // ruta de PWD --curl era demasiado permisivo y tapaba el
                    // fallo--, asi que aqui se anuncia una direccion de
                    // verdad.
                    //
                    // getsockname sobre la conexion de control da justo la
                    // que el cliente esta mirando, asi que sigue siendo
                    // correcta con varias interfaces o con VPN, que era lo
                    // que el 0,0,0,0 pretendia resolver.
                    std::string text;
                    if (!formatPasvReply(net_local_ipv4(control), dataPort,
                                         text)) {
                        // Sin direccion no hay PASV que valga: mejor decirlo
                        // que anunciar una invalida y dejar al cliente
                        // esperando.
                        sendLine(control, "425 Sin direccion local", stopping_);
                        continue;
                    }
                    sendLine(control, text.c_str(), stopping_);
                    continue;
                }

                if (reply.action == FtpSession::Action::ReportSize) {
                    struct stat st {};
                    if (stat(session.path().c_str(), &st) != 0 ||
                        !S_ISREG(st.st_mode)) {
                        sendLine(control, "550 No se puede medir", stopping_);
                    } else {
                        char text[64];
                        std::snprintf(text, sizeof(text), "213 %llu",
                                      static_cast<unsigned long long>(
                                          st.st_size));
                        sendLine(control, text, stopping_);
                    }
                    continue;
                }

                if (reply.action == FtpSession::Action::SendListing ||
                    reply.action == FtpSession::Action::SendFile) {
                    if (dataListener < 0) {
                        sendLine(control, "425 Abre antes el canal de datos",
                                 stopping_);
                        continue;
                    }
                    sendLine(control, reply.text, stopping_);

                    socket_t data = -1;
                    for (int i = 0; i < kIdleSeconds * 100 && data < 0 &&
                                    !stopping_.load(); ++i) {
                        data = net_accept(dataListener, nullptr);
                        if (data < 0)
                            std::this_thread::sleep_for(
                                std::chrono::milliseconds(10));
                    }
                    net_close(dataListener);
                    dataListener = -1;
                    if (data < 0) {
                        sendLine(control, "425 El canal de datos no llego",
                                 stopping_);
                        continue;
                    }

                    bool ok = true;
                    if (reply.action == FtpSession::Action::SendListing) {
                        const std::string listing = buildListing(
                            session.path());
                        ok = sendAll(data, listing.data(), listing.size(),
                                     stopping_);
                    } else if (session.path() == kLogPath) {
                        // El registro de la aplicacion NO se puede abrir con
                        // fopen: en la consola, un segundo fopen de un fichero
                        // que este proceso ya tiene abierto devuelve NULL, y el
                        // registro esta abierto todo el rato. Esta documentado
                        // en core/util.c, que por eso ofrece log_read_tail para
                        // leerlo por el mismo descriptor.
                        //
                        // Merece el caso especial: sacar el registro es
                        // justamente para lo que existe este servidor, y era el
                        // unico fichero que no podia dar.
                        std::vector<char> texto(4 * 1024 * 1024);
                        const size_t leidos =
                            log_read_tail(texto.data(), texto.size());
                        ok = sendAll(data, texto.data(), leidos, stopping_);
                    } else {
                        std::FILE* file =
                            std::fopen(session.path().c_str(), "rb");
                        if (!file) {
                            ok = false;
                        } else {
                            std::vector<char> chunk(kChunk);
                            size_t read = 0;
                            while ((read = std::fread(chunk.data(), 1,
                                                      chunk.size(), file)) > 0) {
                                if (!sendAll(data, chunk.data(), read,
                                             stopping_)) {
                                    ok = false;
                                    break;
                                }
                            }
                            std::fclose(file);
                        }
                    }
                    net_close(data);
                    sendLine(control,
                             ok ? "226 Hecho" : "550 No se pudo leer",
                             stopping_);
                    continue;
                }

                if (!reply.text.empty())
                    sendLine(control, reply.text, stopping_);
                if (reply.action == FtpSession::Action::Close)
                    alive = false;
            }
        }

        if (dataListener >= 0)
            net_close(dataListener);
        net_close(control);
    }
}

} // namespace gamehubnx
