#include "ui/downloads/downloads_view.hpp"

#include "app/download_kind.hpp"
#include "ui/i18n.hpp"

#include <algorithm>

namespace gamehubnx::ui {

void DownloadDataSource::setTasks(std::vector<DownloadTask> tasks,
                                  std::string activeDeployTask) {
    sections_.clear();
    // Secciones por tipo de contenido (juego, actualizacion, DLC), como en "Que
    // instalar". Dentro de cada una, lo que esta en marcha va primero: activa,
    // en cola, pausada, terminada y con error.
    auto rank = [&activeDeployTask](const DownloadTask& task) {
        if (task.id == activeDeployTask)
            return 0;
        switch (task.status) {
        case DownloadStatus::Checking:
        case DownloadStatus::Fetching:
        case DownloadStatus::Downloading:
        case DownloadStatus::Installing:
        case DownloadStatus::Committing:
        case DownloadStatus::Verifying:
        case DownloadStatus::Removing:
            return 0;
        case DownloadStatus::Queued:
            return 1;
        case DownloadStatus::Paused:
            return 2;
        case DownloadStatus::Completed:
        case DownloadStatus::Installed:
            return 3;
        case DownloadStatus::Error:
            return 4;
        }
        return 5;
    };
    const struct {
        DownloadKind kind;
        const char* key;
    } groups[] = {
        {DownloadKind::Game, "gamehubnx/addons/kind_game"},
        {DownloadKind::Update, "gamehubnx/addons/kind_update"},
        {DownloadKind::Dlc, "gamehubnx/addons/kind_dlc"},
    };
    for (const auto& group : groups) {
        Section section;
        section.title = tr(group.key);
        for (const auto& task : tasks)
            if (downloadKindOf(task.name) == group.kind)
                section.tasks.push_back(task);
        std::stable_sort(section.tasks.begin(), section.tasks.end(),
                         [&rank](const DownloadTask& a, const DownloadTask& b) {
                             return rank(a) < rank(b);
                         });
        if (!section.tasks.empty())
            sections_.push_back(std::move(section));
    }
    if (sections_.empty())
        sections_.push_back({"", {}});
}

const DownloadTask* DownloadDataSource::taskAt(brls::IndexPath index) const {
    if (index.section >= sections_.size())
        return nullptr;
    const Section& section = sections_[index.section];
    if (index.row < 0 ||
        static_cast<size_t>(index.row) >= section.tasks.size())
        return nullptr;
    return &section.tasks[static_cast<size_t>(index.row)];
}

std::string DownloadDataSource::taskIdAt(brls::IndexPath index) const {
    const DownloadTask* task = taskAt(index);
    return task ? task->id : std::string{};
}

brls::IndexPath DownloadDataSource::indexForTask(
    const std::string& taskId) const {
    if (!taskId.empty()) {
        for (size_t section = 0; section < sections_.size(); ++section) {
            for (size_t row = 0; row < sections_[section].tasks.size(); ++row) {
                if (sections_[section].tasks[row].id == taskId)
                    return brls::IndexPath(section, row);
            }
        }
    }
    return brls::IndexPath(0, 0);
}

int DownloadDataSource::numberOfSections(brls::RecyclerFrame*) {
    return static_cast<int>(sections_.size());
}

// Los cuatro overrides de abajo acotan el indice igual que taskAt(). No es
// paranoia: refresh() tiene un camino --el de overlay-- que publica un
// sections_ nuevo sin llamar a reloadData(), de modo que el RecyclerFrame se
// queda con el reparto anterior y puede pedir una fila que ya no existe. Con
// una tarea de la tienda pasa facil, porque al reanudarse cambia de estado
// varias veces seguidas (Queued -> Checking -> Fetching -> Installing).
int DownloadDataSource::numberOfRows(brls::RecyclerFrame*, int section) {
    if (section < 0 || static_cast<size_t>(section) >= sections_.size())
        return 0;
    return sections_[section].tasks.empty()
        ? 1
        : static_cast<int>(sections_[section].tasks.size());
}

std::string DownloadDataSource::titleForHeader(brls::RecyclerFrame*,
                                                int section) {
    if (section < 0 || static_cast<size_t>(section) >= sections_.size())
        return {};
    return sections_[section].title;
}

brls::RecyclerCell* DownloadDataSource::cellForRow(
    brls::RecyclerFrame* recycler, brls::IndexPath index) {
    const DownloadTask* task = taskAt(index);
    if (!task)
        return recycler->dequeueReusableCell("Message");
    auto* cell = static_cast<DownloadCell*>(
        recycler->dequeueReusableCell("Download"));
    cell->setTask(*task, owner_->metadataService(),
                  &owner_->deploySnapshot());
    return cell;
}

void DownloadDataSource::didSelectRowAt(brls::RecyclerFrame*,
                                         brls::IndexPath index) {
    // Sobre la fila de relleno no se hace nada. Antes abria el importador de
    // .torrent, que cfaf746 dio por inalcanzable al quitar su boton: pulsarla
    // llevaba a un selector de ficheros que ya no pinta nada en este fork.
    if (const DownloadTask* task = taskAt(index))
        owner_->openRowMenu(task->id);
}

}  // namespace gamehubnx::ui
