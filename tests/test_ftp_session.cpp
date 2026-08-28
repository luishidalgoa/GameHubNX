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
    // Lo que se le ensena al cliente es una ruta de SERVIDOR, sin el prefijo
    // del aparato. FileZilla recibia 257 "sdmc:/..." y contestaba "Error al
    // analizar la ruta devuelta"; curl lo toleraba y por eso no salio antes.
    assert(s.handle("PWD").text == "257 \"/switch/gamehubnx\"");
    s.handle("CDUP");
    assert(s.cwd() == "sdmc:/switch");
    // Una ruta absoluta se resuelve desde la raiz.
    assert(s.handle("CWD /switch/gamehubnx/downloads").text.rfind("250", 0) == 0);
    assert(s.cwd() == "sdmc:/switch/gamehubnx/downloads");
}

void test_ruta_que_se_ensena() {
    FtpSession s = open();
    // En la raiz servida, "/".
    assert(s.handle("PWD").text == "257 \"/\"");
    s.handle("CWD switch");
    assert(s.handle("PWD").text == "257 \"/switch\"");
    // Y con una raiz que no es la del volumen, sigue siendo relativa a ella.
    FtpSession t("sdmc:/switch", "");
    t.handle("USER x");
    assert(t.handle("PWD").text == "257 \"/\"");
    t.handle("CWD gamehubnx");
    assert(t.handle("PWD").text == "257 \"/gamehubnx\"");
}

void test_utf8() {
    FtpSession s = open();
    // Se anuncia UTF8: sin eso, FileZilla avisa de que el servidor no admite
    // caracteres no ASCII y puede estropear los nombres con tildes.
    const std::string feat = s.handle("FEAT").text;
    assert(feat.find("UTF8") != std::string::npos);
    assert(feat.find("SIZE") != std::string::npos);
    // Y activarlo se acepta, porque ya se sirve asi.
    assert(s.handle("OPTS UTF8 ON").text.rfind("200", 0) == 0);
    assert(s.handle("OPTS utf8 on").text.rfind("200", 0) == 0);
    // Cualquier otra opcion se rechaza en vez de fingir que se aplico.
    assert(s.handle("OPTS MLST size").text.rfind("501", 0) == 0);
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

    // SIZE deja que el transporte ponga el numero: la sesion no toca el disco.
    // Antes devolvia la palabra "SIZE" como si fuera un tamano y el cliente se
    // quedaba sin saber cuanto iba a bajar.
    const FtpSession::Reply tam = s.handle("SIZE gamehubnx.log");
    assert(tam.action == FtpSession::Action::ReportSize);
    assert(tam.text.empty());
    assert(s.path() == "sdmc:/switch/gamehubnx/gamehubnx.log");

    // El modo texto se acepta pero se transfiere en binario igual: traducir
    // saltos de linea corromperia un NRO.
    s.handle("TYPE A");
    assert(!s.binary());
    s.handle("TYPE I");
    assert(s.binary());
}

// La respuesta a PASV tiene que llevar una direccion a la que el cliente pueda
// conectarse de verdad.
//
// Este test existe porque no existia: se anunciaba 0.0.0.0 confiando en que el
// cliente reutilizase la direccion de la conexion de control. curl lo hace, y
// por eso todo parecia bien; FileZilla se conecta a lo que se le diga y moria
// con WSAEADDRNOTAVAIL antes de poder listar nada.
void test_pasv() {
    std::string out;
    assert(gamehubnx::formatPasvReply(0xC0A80010u, 28615, out));
    // 28615 = 111*256 + 199
    assert(out == "227 Entering Passive Mode (192,168,0,16,111,199)");

    // El puerto alto y el bajo no se pueden confundir.
    assert(gamehubnx::formatPasvReply(0x0A000001u, 256, out));
    assert(out == "227 Entering Passive Mode (10,0,0,1,1,0)");

    // Lo que no sirve para conectarse se rechaza en vez de anunciarse: es
    // justo el caso que rompia FileZilla.
    assert(!gamehubnx::formatPasvReply(0, 28615, out));
    assert(!gamehubnx::formatPasvReply(0xC0A80010u, 0, out));
}

} // namespace

int main() {
    test_entrada();
    test_navegar();
    test_ruta_que_se_ensena();
    test_utf8();
    test_no_se_sale_del_arbol();
    test_solo_lectura();
    test_acciones();
    test_pasv();
    std::printf("ftp session tests passed\n");
    return 0;
}
