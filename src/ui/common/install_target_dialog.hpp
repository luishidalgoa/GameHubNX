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
    auto* dialog = new brls::Dialog(tr("gamehubnx/install_target/question"));
    auto addChoice = [&](install::InstallStorageTarget target) {
        std::string label = installDestinationLabel(target);
        // El espacio libre va en el propio boton: es justo el dato que hace
        // falta para responder, y pedirlo aqui evita que el usuario tenga que
        // acordarse de cuanto le queda en cada sitio.
        const StorageSpaceSnapshot space =
            queryInstallStorageSpace(target, rootPath);
        if (space.available)
            label += tr("gamehubnx/install_target/free",
                        formatBytes(space.freeBytes));
        dialog->addButton(label, [onPick, target] {
            if (onPick)
                onPick(target);
        });
    };
    addChoice(install::InstallStorageTarget::SdCard);
    addChoice(install::InstallStorageTarget::Nand);
    dialog->open();
}

} // namespace gamehubnx::ui
