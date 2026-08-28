#include "app/ftp_session.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using gamehubnx::FtpSession;

namespace {

FtpSession open(const std::string& password = "") {
    FtpSession s("sdmc:/", password);
    s.handle("USER quien-sea");
    if (!password.empty())
        s.handle("PASS " + password);
    return s;
}

void test_entrada() {
    // Sin clave configurada se entra directo: el usuario da igual, lo que
    // autoriza es el PIN cuando lo hay.
    FtpSession abierta("sdmc:/", "");
    assert(abierta.handle("USER pepe").text.rfind("230", 0) == 0);
    assert(abierta.authenticated());

    // Con clave, hasta que no se da no se hace nada.
    FtpSession cerrada("sdmc:/", "1234");
    assert(cerrada.handle("USER pepe").text.rfind("331", 0) == 0);
    assert(!cerrada.authenticated());
    assert(cerrada.handle("LIST").text.rfind("530", 0) == 0);
    assert(cerrada.handle("PASS mal").text.rfind("530", 0) == 0);
    assert(!cerrada.authenticated());
    assert(cerrada.handle("PASS 1234").text.rfind("230", 0) == 0);
    assert(cerrada.authenticated());

    // QUIT se atiende sin haber entrado: cerrar siempre se puede.
    FtpSession otra("sdmc:/", "1234");
    assert(otra.handle("QUIT").action == FtpSession::Action::Close);
}

void test_navegar() {
    FtpSession s = open();
    assert(s.cwd() == "sdmc:/");
    assert(s.handle("CWD switch").text.rfind("250", 0) == 0);
    assert(s.cwd() == "sdmc:/switch");
    assert(s.handle("CWD gamehubnx").text.rfind("250", 0) == 0);
    assert(s.cwd() == "sdmc:/switch/gamehubnx");
    assert(s.handle("PWD").text == "257 \"sdmc:/switch/gamehubnx\"");
    s.handle("CDUP");
    assert(s.cwd() == "sdmc:/switch");
    // Una ruta absoluta se resuelve desde la raiz.
    assert(s.handle("CWD /switch/gamehubnx/downloads").text.rfind("250", 0) == 0);
    assert(s.cwd() == "sdmc:/switch/gamehubnx/downloads");
}

void test_no_se_sale_del_arbol() {
    FtpSession s("sdmc:/switch", "");
    s.handle("USER x");

    // Subir desde la raiz permitida no sube: se queda donde esta.
    s.handle("CDUP");
    assert(s.cwd() == "sdmc:/switch");

    // Y pedirlo con ".." es un ERROR, no un no-op silencioso: quien lo intenta
    // tiene que oir que no.
    assert(s.handle("CWD ..").text.rfind("550", 0) == 0);
    assert(s.handle("CWD ../../etc").text.rfind("550", 0) == 0);
    assert(s.handle("RETR ../../secreto").text.rfind("550", 0) == 0);
    assert(s.cwd() == "sdmc:/switch");

    // Ni bajando primero para subir despues de mas.
    assert(s.handle("CWD gamehubnx").text.rfind("250", 0) == 0);
    assert(s.handle("CWD ../..").text.rfind("550", 0) == 0);
    assert(s.cwd() == "sdmc:/switch/gamehubnx");

    // Una barra invertida no vale como separador: es una forma de escapar.
    assert(s.handle("RETR ..\\..\\secreto").text.rfind("550", 0) == 0);

    // Una ruta ABSOLUTA es relativa a la raiz servida, no a la tarjeta: es
    // como funciona FTP y es lo que hace que la raiz sea una jaula. "/x" es
    // "<raiz>/x", nunca "sdmc:/x".
    FtpSession t("sdmc:/switch", "");
    t.handle("USER x");
    assert(t.handle("CWD /gamehubnx").text.rfind("250", 0) == 0);
    assert(t.cwd() == "sdmc:/switch/gamehubnx");
    // Y por eso pedir la raiz de la tarjeta por su nombre tampoco sale de la
    // jaula: cae dentro.
    assert(t.handle("CWD /sdmc:").text.rfind("250", 0) == 0);
    assert(t.cwd() == "sdmc:/switch/sdmc:");
}

void test_solo_lectura() {
    FtpSession s = open();
    // Todo lo que escribe se rechaza DICIENDO que es de solo lectura, no con
    // un "no implementado": la respuesta es no, no una carencia.
    for (const char* c : {"STOR x", "STOU", "APPE x", "DELE x", "RMD x",
                          "MKD x", "RNFR x", "RNTO y"}) {
        const FtpSession::Reply r = s.handle(c);
        assert(r.text.rfind("550", 0) == 0);
        assert(r.text.find("solo lectura") != std::string::npos);
    }
}

void test_acciones() {
    FtpSession s = open();
    assert(s.handle("PASV").action == FtpSession::Action::OpenPassive);

    const FtpSession::Reply listado = s.handle("LIST");
    assert(listado.action == FtpSession::Action::SendListing);
    assert(s.path() == "sdmc:/");

    // LIST con una opcion de las que mandan algunos clientes ("-la") no se
    // toma por un nombre de fichero.
    FtpSession::Reply conOpcion = s.handle("LIST -la");
    assert(conOpcion.action == FtpSession::Action::SendListing);
    assert(s.path() == "sdmc:/");

    s.handle("CWD switch/gamehubnx");
    const FtpSession::Reply fichero = s.handle("RETR gamehubnx.log");
    assert(fichero.action == FtpSession::Action::SendFile);
    assert(s.path() == "sdmc:/switch/gamehubnx/gamehubnx.log");

    // RETR sin nombre no manda a ninguna parte.
    assert(s.handle("RETR").text.rfind("550", 0) == 0);

    // El modo texto se acepta pero se transfiere en binario igual: traducir
    // saltos de linea corromperia un NRO.
    s.handle("TYPE A");
    assert(!s.binary());
    s.handle("TYPE I");
    assert(s.binary());
}

} // namespace

int main() {
    test_entrada();
    test_navegar();
    test_no_se_sale_del_arbol();
    test_solo_lectura();
    test_acciones();
    std::printf("ftp session tests passed\n");
    return 0;
}
