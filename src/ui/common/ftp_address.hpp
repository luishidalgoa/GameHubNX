#pragma once

// La direccion del FTP, para el pie de la barra lateral y para Ajustes.
//
// Sustituye a lo que hacia web_qr.hpp con el acompanante web. No hay QR: un
// codigo QR servia para que un movil abriera una pagina, y aqui lo que se
// necesita es teclear una direccion en un cliente de FTP desde un ordenador.
// Un QR que nadie va a escanear es adorno.

#include <string>

#include <borealis.hpp>

#include "app/ftp_server.hpp"

namespace gamehubnx::ui {

// "ftp://<ip>:<puerto>" con el servidor levantado y la consola con direccion,
// "" en cualquier otro caso. Un servidor nulo --el arnes de capturas-- da una
// direccion falsa fija, para que las imagenes de referencia nunca lleven la IP
// real de quien las genera.
inline std::string ftpAddress(gamehubnx::FtpServer* server, bool enabled) {
    if (!enabled)
        return "";
    if (!server)
        return "ftp://192.168.1.2:5001";
    if (!server->running())
        return "";
    const std::string ip =
        brls::Application::getPlatform()->getIpAddress();
    if (ip.empty() || ip == "0.0.0.0")
        return "";
    return "ftp://" + ip + ":" + std::to_string(server->boundPort());
}

} // namespace gamehubnx::ui
