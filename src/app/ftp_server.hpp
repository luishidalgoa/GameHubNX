#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace gamehubnx {

// Servidor FTP de SOLO LECTURA sobre la tarjeta.
//
// Existe para poder sacar de la consola lo que hace falta para diagnosticar
// --el registro, un volcado, una descarga a medias-- sin depender de otro
// homebrew. Sustituye al acompanante web de :8080, que hacia otra cosa
// (mirar la cola desde el movil) y ya no se usaba.
//
// Las decisiones de protocolo NO estan aqui: viven en FtpSession, que se puede
// probar en el PC. Esto es solo transporte, y por eso puede permitirse ser
// aburrido.
class FtpServer {
public:
    // El puerto por defecto no es el 21: en la consola los puertos bajos dan
    // problemas y ademas no conviene chocar con otro servidor de ficheros que
    // el usuario ya tenga levantado.
    static constexpr uint16_t kDefaultPort = 5001;

    // root: raiz que se sirve. password: vacia = entra cualquiera.
    FtpServer(std::string root, std::string password);
    ~FtpServer();

    FtpServer(const FtpServer&) = delete;
    FtpServer& operator=(const FtpServer&) = delete;

    bool start(uint16_t port, std::string& error);
    void stop();

    bool running() const { return running_.load(); }
    uint16_t boundPort() const { return boundPort_.load(); }

    // Se puede cambiar en caliente desde Ajustes. Solo afecta a las sesiones
    // que empiecen despues: cortarle la conexion a quien ya esta descargando
    // seria peor que dejarle terminar.
    void setPassword(std::string password);

private:
    void serve();

    std::string root_;
    std::string password_;
    std::atomic<bool> running_ {false};
    std::atomic<bool> stopping_ {false};
    std::atomic<uint16_t> boundPort_ {0};
    std::thread thread_;
    int listener_ = -1;
};

} // namespace gamehubnx
