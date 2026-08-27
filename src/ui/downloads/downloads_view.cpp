#include "ui/downloads/downloads_view.hpp"
#include "ui/i18n.hpp"

namespace pipensx::ui {

void DownloadDataSource::setTasks(std::vector<DownloadTask> tasks,
                                  std::string activeDeployTask) {
    sections_.clear();
    // std::string, not const char*: the titles are now resolved per-locale at
    // call time rather than being string literals with static storage.
    const struct {
        std::string title;
        bool (*matches)(DownloadStatus);
    } groups[] = {
        {tr("pipensx/downloads/section_active"), [](DownloadStatus s) {
            // Fetching faltaba, y no era inocuo: solo lo produce el camino
            // debrid/GameHub --un torrent jamas lo toma-- asi que una descarga
            // de la tienda reanudada al abrir la app se caia de las cinco
            // secciones y desaparecia de la lista mientras duraba ese estado.
            // La pantalla se quedaba con la fila de relleno mientras el resto
            // del codigo seguia creyendo que habia tareas.
            return s == DownloadStatus::Checking ||
                   s == DownloadStatus::Fetching ||
                   s == DownloadStatus::Downloading ||
                   s == DownloadStatus::Installing ||
                   s == DownloadStatus::Committing ||
                   s == DownloadStatus::Verifying ||
                   s == DownloadStatus::Removing;
        }},
        {tr("pipensx/downloads/section_queue"), [](DownloadStatus s) {
            return s == DownloadStatus::Queued;
        }},
        {tr("pipensx/downloads/section_paused"), [](DownloadStatus s) {
            return s == DownloadStatus::Paused;
        }},
        {tr("pipensx/downloads/section_completed"), [](DownloadStatus s) {
            return s == DownloadStatus::Completed ||
                   s == DownloadStatus::Installed;
        }},
        {tr("pipensx/downloads/section_errors"), [](DownloadStatus s) {
            return s == DownloadStatus::Error;
        }},
    };
    for (const auto& group : groups) {
        Section section;
        section.title = group.title;
        for (const auto& task : tasks) {
            const bool deploying = task.id == activeDeployTask;
            const bool activeGroup = group.matches(DownloadStatus::Checking);
            const bool completedGroup = group.matches(DownloadStatus::Completed);
            if ((deploying && activeGroup) ||
                (!deploying && group.matches(task.status)) ||
                (deploying && !activeGroup && !completedGroup &&
                 group.matches(task.status)))
                section.tasks.push_back(task);
        }
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

}  // namespace pipensx::ui
