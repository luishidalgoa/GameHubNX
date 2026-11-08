#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace gamehubnx {

// Servidor FTP con acceso completo a la tarjeta (leer, subir, borrar, carpetas).
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
    // Estado de una tanda de arranque (entre start y stop). Cada sesion guarda su
    // propia copia: si stop() no alcanza a esperarlas, siguen con su estado y no
    // tocan el servidor, que puede haber desaparecido ya. Un arranque nuevo crea
    // otro estado, asi que las sesiones viejas no vuelven a la vida.
    struct RunState {
        std::atomic<bool> stopping {false};
        std::atomic<int> active {0};
    };

    void serve(std::shared_ptr<RunState> run);
    // Atiende una conexion de control entera, en su propio hilo. Asi el cliente
    // que abre una segunda conexion (FileZilla lo hace para las transferencias)
    // recibe su saludo al momento en vez de esperar a que acabe la primera.
    static void runSession(std::shared_ptr<RunState> run, int control,
                           std::string root, std::string password);

    std::string root_;
    std::string password_;
    // Protege password_: setPassword llega desde Ajustes mientras hay sesiones.
    std::mutex passwordMutex_;
    std::atomic<bool> running_ {false};
    std::atomic<uint16_t> boundPort_ {0};
    std::shared_ptr<RunState> run_;
    std::thread thread_;
    int listener_ = -1;
};

} // namespace gamehubnx
