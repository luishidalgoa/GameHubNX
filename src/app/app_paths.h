/* Identidad de GameHubNX en la tarjeta.
 *
 * Antes de esto habia 40+ literales "sdmc:/switch/pipensx" repetidos por diez
 * ficheros, y renombrar la app dejaba la mitad apuntando al proyecto del que
 * viene este fork: la carpeta que se creaba al arrancar seguia siendo la suya,
 * el auto-actualizador escribia sobre su NRO, y una instalacion antigua
 * compartia datos con la nueva. Una sola definicion evita que vuelva a pasar.
 *
 * Cabecera C, no C++, a proposito: la incluyen src/update_helper.c,
 * src/probe/probe_main.c y src/platform/switch_crashlog.c, que son C puro y
 * binarios aparte. Y son macros, no const char*, porque switch_crashlog.c y
 * daemon_main.cpp las usan en inicializadores estaticos y en concatenacion de
 * literales, donde una variable no vale.
 */
#ifndef GAMEHUBNX_APP_PATHS_H
#define GAMEHUBNX_APP_PATHS_H

/* Nombre de la carpeta y del binario. hbmenu lista switch/<nombre>/<nombre>.nro,
   asi que estos dos van juntos y tienen que coincidir con lo que empaqueta
   .github/workflows/switch-release.yml. */
#define GHNX_APP_DIR_NAME "gamehubnx"
#define GHNX_NRO_NAME     GHNX_APP_DIR_NAME ".nro"

/* Raiz de datos: ajustes, favoritos, cola de descargas, cache del catalogo. */
#define GHNX_SWITCH_ROOT "sdmc:/switch"
#define GHNX_APP_ROOT    GHNX_SWITCH_ROOT "/" GHNX_APP_DIR_NAME

/* Un fichero colgando de la raiz: GHNX_PATH("settings.json"). */
#define GHNX_PATH(name) GHNX_APP_ROOT "/" name

/* Sufijo de los temporales de descarga. Se ve en las carpetas del usuario
   cuando una descarga queda a medias, asi que lleva el nombre del fork. */
#define GHNX_PART_SUFFIX "." GHNX_APP_DIR_NAME "-part"

/* La carpeta que dejaba la app de la que procede este fork. Solo se usa para
   detectarla y avisar; nunca para escribir. */
#define GHNX_LEGACY_APP_ROOT GHNX_SWITCH_ROOT "/pipensx"

#endif /* GAMEHUBNX_APP_PATHS_H */
