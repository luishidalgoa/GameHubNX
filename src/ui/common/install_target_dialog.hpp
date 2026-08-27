#pragma once

#include <functional>
#include <string>
#include <utility>

#include <borealis.hpp>

#include "app/install_space.hpp"
#include "install/install_backend.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/i18n.hpp"

namespace gamehubnx::ui {

// Pregunta en un modal donde instalar, justo antes de encolar.
//
// Antes esto era un boton fijo dentro de la pantalla de instalacion por lotes.
// Va mejor como modal por dos razones: la eleccion es puntual --se responde y
// se sigue-- y ademas no deja un control mas ocupando sitio en una pantalla
// que ya tiene cuatro botones apilados.
//
// El modal se cierra con B sin elegir nada (Dialog es cancelable), y en ese
// caso onPick no se llama: no encolar es la respuesta segura.
inline void askInstallTarget(
    const std::string& rootPath,
    std::function<void(install::InstallStorageTarget)> onPick) {
    // El espacio libre va en el TEXTO del modal, una linea por memoria, no
    // dentro del boton: los botones de brls::Dialog tienen ancho fijo y
    // "Tarjeta SD (53,56 GB libres)" se sale por los lados. El dato sigue
    // delante de los ojos al decidir, que es lo que hacia falta.
    const install::InstallStorageTarget targets[] = {
        install::InstallStorageTarget::SdCard,
        install::InstallStorageTarget::Nand,
    };
    std::string text = tr("gamehubnx/install_target/question");
    for (install::InstallStorageTarget target : targets) {
        const StorageSpaceSnapshot space =
            queryInstallStorageSpace(target, rootPath);
        if (!space.available)
            continue;
        text += "\n" + installDestinationLabel(target) +
                tr("gamehubnx/install_target/free",
                   formatBytes(space.freeBytes));
    }

    auto* dialog = new brls::Dialog(text);
    for (install::InstallStorageTarget target : targets) {
        dialog->addButton(installDestinationLabel(target),
                          [onPick, target] {
            if (onPick)
                onPick(target);
        });
    }
    dialog->open();
}

} // namespace gamehubnx::ui
