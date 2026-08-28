#pragma once

#include <memory>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "app/file_browser.hpp"
#include "ui/catalog/catalog_helpers.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/i18n.hpp"
#include "ui/theme.hpp"

namespace gamehubnx::ui {

class FileBrowserActivity;

// Fila del explorador. La carpeta se distingue por la barra final y por el
// color, no por un icono: un glifo mas en una lista de cientos de nombres solo
// le quita sitio al que importa, que es el nombre.
class FileCell : public brls::RecyclerCell {
public:
    FileCell() {
        setFocusable(true);
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::CENTER);
        setPadding(6, 20, 6, 20);
        setHeight(58);

        name_ = new brls::Label();
        name_->setSingleLine(true);
        name_->setAutoAnimate(false);
        name_->setFontSize(theme::kFontBody);
        name_->setGrow(1);
        addView(name_);

        size_ = new brls::Label();
        size_->setFontSize(theme::kFontCaption);
        size_->setTextColor(theme::textTertiary());
        size_->setMarginLeft(16);
        addView(size_);
    }

    void setEntry(const files::Entry& entry, bool marked) {
        setTextIfChanged(name_,
                         entry.directory ? entry.name + "/" : entry.name);
        name_->setTextColor(entry.directory ? theme::accent()
                                            : theme::textPrimary());
        // Lo marcado para mover se dice en su propia fila: si no, al navegar a
        // otra carpeta se pierde de vista que hay algo a medio mover.
        if (marked)
            setTextIfChanged(size_, tr("gamehubnx/browser/marked"));
        else
            setTextIfChanged(size_, entry.directory ? std::string()
                                                    : formatBytes(entry.size));
        size_->setTextColor(marked ? theme::accent() : theme::textTertiary());
    }

private:
    brls::Label* name_;
    brls::Label* size_;
};

class FileDataSource : public brls::RecyclerDataSource {
public:
    explicit FileDataSource(FileBrowserActivity* owner) : owner_(owner) {}

    void setEntries(std::vector<files::Entry> entries) {
        entries_ = std::move(entries);
    }
    const std::vector<files::Entry>& entries() const { return entries_; }
    void setMarked(std::string path) { marked_ = std::move(path); }
    const std::string& marked() const { return marked_; }

    int numberOfRows(brls::RecyclerFrame*, int) override {
        return static_cast<int>(entries_.size());
    }
    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler,
                                   brls::IndexPath index) override;
    float heightForRow(brls::RecyclerFrame*, brls::IndexPath) override {
        return 58.0f;
    }
    void didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath index) override;

private:
    FileBrowserActivity* owner_;
    std::vector<files::Entry> entries_;
    std::string marked_;
};

// Explorador de la tarjeta: listar, entrar, mover, renombrar y borrar.
//
// Existe porque la aplicacion ESCRIBE en la tarjeta --descarga ROMs, las lleva
// a la carpeta del emulador, deja carpetas de trabajo-- y hasta ahora no daba
// ninguna forma de mirar lo que hay ni de recolocarlo sin un ordenador y FTP.
class FileBrowserActivity : public brls::Activity {
public:
    explicit FileBrowserActivity(std::string startPath)
        : path_(std::move(startPath)) {
        auto* content = new brls::Box(brls::Axis::COLUMN);
        content->setGrow(1);
        content->setPadding(18, 38, 18, 34);

        pathLabel_ = new brls::Label();
        pathLabel_->setFontSize(theme::kFontSmall);
        pathLabel_->setTextColor(theme::textSecondary());
        content->addView(pathLabel_);

        // Lo que hay marcado para mover se ve SIEMPRE, en cualquier carpeta: es
        // lo unico que explica por que la accion de traerlo esta disponible.
        markedLabel_ = new brls::Label();
        markedLabel_->setFontSize(theme::kFontCaption);
        markedLabel_->setTextColor(theme::accent());
        markedLabel_->setMarginTop(4);
        content->addView(markedLabel_);

        status_ = new brls::Label();
        status_->setFontSize(theme::kFontCaption);
        status_->setTextColor(theme::textTertiary());
        status_->setMarginTop(8);
        status_->setMarginBottom(10);
        content->addView(status_);

        recycler_ = new brls::RecyclerFrame();
        recycler_->setGrow(1);
        recycler_->estimatedRowHeight = 58;
        recycler_->registerCell("File", [] { return new FileCell(); });
        dataSource_ = new FileDataSource(this);
        recycler_->setDataSource(dataSource_);
        recyclerHost_ = recyclerHost(recycler_);
        content->addView(recyclerHost_);

        frame_ = new brls::AppletFrame(content);
        frame_->setTitle(tr("gamehubnx/browser/title"));
        reload();
    }

    brls::View* createContentView() override { return frame_; }

    const std::string& currentPath() const { return path_; }

    void onContentAvailable() override {
        registerAction(tr("gamehubnx/browser/up"), brls::BUTTON_B,
                       [this](brls::View*) {
                           // B sube una carpeta y solo cierra desde la raiz.
                           // Salir de golpe estando tres niveles adentro es la
                           // forma mas rapida de perder donde estabas.
                           if (files::isRootPath(path_)) {
                               brls::Application::popActivity();
                               return true;
                           }
                           path_ = files::parentPath(path_);
                           reload();
                           return true;
                       });
        registerAction(tr("gamehubnx/browser/actions"), brls::BUTTON_Y,
                       [this](brls::View*) {
                           openActions(SIZE_MAX);
                           return true;
                       });
    }

    void activate(size_t index) {
        const std::vector<files::Entry>& entries = dataSource_->entries();
        if (index >= entries.size())
            return;
        if (!entries[index].directory) {
            // Un fichero no se "abre": esta aplicacion no sabe que hacer con
            // el. Se ofrecen las acciones, que es lo unico que puede hacer.
            openActions(index);
            return;
        }
        path_ = files::joinPath(path_, entries[index].name);
        reload();
    }

private:
    enum Action { Paste, Mark, Rename, DeleteFile, DeleteDirectory };

    void openActions(size_t forcedIndex) {
        const std::vector<files::Entry>& entries = dataSource_->entries();
        const size_t index = forcedIndex == SIZE_MAX
            ? static_cast<size_t>(recycler_->getDefaultFocusedIndex())
            : forcedIndex;

        std::vector<std::string> labels;
        std::vector<Action> actions;

        // Traerlo aqui va primero cuando hay algo marcado: es justamente la
        // accion por la que el usuario ha navegado hasta esta carpeta.
        if (!dataSource_->marked().empty()) {
            labels.push_back(tr("gamehubnx/browser/paste"));
            actions.push_back(Paste);
        }
        const bool valid = index < entries.size();
        if (valid && !entries[index].directory) {
            labels.push_back(tr("gamehubnx/browser/mark"));
            actions.push_back(Mark);
            labels.push_back(tr("gamehubnx/browser/rename"));
            actions.push_back(Rename);
            labels.push_back(tr("gamehubnx/browser/delete"));
            actions.push_back(DeleteFile);
        } else if (valid) {
            labels.push_back(tr("gamehubnx/browser/delete_empty"));
            actions.push_back(DeleteDirectory);
        }
        if (labels.empty())
            return;

        const std::string name = valid ? entries[index].name : std::string();
        auto* dropdown = new brls::Dropdown(
            tr("gamehubnx/browser/actions"), labels,
            [this, actions, name](int chosen) {
                if (chosen < 0 || chosen >= static_cast<int>(actions.size()))
                    return;
                switch (actions[static_cast<size_t>(chosen)]) {
                    case Paste: paste(); break;
                    case Mark: mark(name); break;
                    case Rename: rename(name); break;
                    case DeleteFile: confirmDeleteFile(name); break;
                    case DeleteDirectory: deleteEmptyDirectory(name); break;
                }
            },
            0);
        brls::Application::pushActivity(new brls::Activity(dropdown));
    }

    void mark(const std::string& name) {
        dataSource_->setMarked(files::joinPath(path_, name));
        refreshLabels();
        recycler_->reloadData();
    }

    void paste() {
        const std::string from = dataSource_->marked();
        if (from.empty())
            return;
        const size_t slash = from.find_last_of('/');
        const std::string base = slash == std::string::npos
            ? from : from.substr(slash + 1);
        std::string error;
        if (!files::moveFile(from, files::joinPath(path_, base), error)) {
            brls::Application::notify(error);
            return;
        }
        dataSource_->setMarked(std::string());
        reload();
    }

    void rename(const std::string& name) {
        brls::Application::getImeManager()->openForText(
            [this, name](std::string text) {
                // Una barra convertiria un renombrado en un movimiento a otra
                // carpeta sin que el usuario lo haya pedido.
                if (text.empty() || text.find('/') != std::string::npos)
                    return;
                std::string error;
                if (!files::moveFile(files::joinPath(path_, name),
                                     files::joinPath(path_, text), error)) {
                    brls::Application::notify(error);
                    return;
                }
                reload();
            },
            tr("gamehubnx/browser/rename"), /*subText=*/"", /*maxLength=*/200,
            /*initialText=*/name, brls::KEYBOARD_DISABLE_NONE);
    }

    void confirmDeleteFile(const std::string& name) {
        // Borrar pregunta siempre: no hay papelera en la tarjeta, lo que se
        // quita aqui no vuelve.
        auto* dialog = new brls::Dialog(
            tr("gamehubnx/browser/delete_confirm", name));
        dialog->addButton(tr("gamehubnx/common/cancel"), [] {});
        dialog->addButton(tr("gamehubnx/browser/delete"), [this, name] {
            std::string error;
            if (!files::removeFile(files::joinPath(path_, name), error)) {
                brls::Application::notify(error);
                return;
            }
            reload();
        });
        dialog->open();
    }

    void deleteEmptyDirectory(const std::string& name) {
        std::string error;
        if (!files::removeEmptyDirectory(files::joinPath(path_, name),
                                         error)) {
            brls::Application::notify(error);
            return;
        }
        reload();
    }

    void refreshLabels() {
        pathLabel_->setText(path_);
        const std::string& marked = dataSource_->marked();
        markedLabel_->setText(
            marked.empty() ? std::string()
                           : tr("gamehubnx/browser/marked_hint", marked));
        markedLabel_->setVisibility(marked.empty()
                                        ? brls::Visibility::GONE
                                        : brls::Visibility::VISIBLE);
    }

    void reload() {
        std::vector<files::Entry> entries;
        std::string error;
        // Se lee en el hilo de la interfaz a proposito: una carpeta de la SD se
        // lista en milisegundos, y sacarlo a otro hilo traeria toda la danza de
        // comprobar que la pantalla sigue viva al volver. Si algun dia aparece
        // una carpeta que lo note, se mueve; hoy seria complejidad sin sintoma.
        if (!files::listDirectory(path_, entries, error)) {
            status_->setText(error);
            recyclerHost_->setVisibility(brls::Visibility::GONE);
            refreshLabels();
            return;
        }
        const size_t count = entries.size();
        dataSource_->setEntries(std::move(entries));
        status_->setText(count == 0 ? tr("gamehubnx/browser/empty")
                                    : tr("gamehubnx/browser/count", count));
        recyclerHost_->setVisibility(count == 0 ? brls::Visibility::GONE
                                                : brls::Visibility::VISIBLE);
        refreshLabels();
        recycler_->reloadData();
        if (count > 0)
            brls::Application::giveFocus(recycler_);
    }

    std::string path_;
    brls::AppletFrame* frame_ = nullptr;
    brls::Label* pathLabel_ = nullptr;
    brls::Label* markedLabel_ = nullptr;
    brls::Label* status_ = nullptr;
    brls::RecyclerFrame* recycler_ = nullptr;
    brls::Box* recyclerHost_ = nullptr;
    FileDataSource* dataSource_ = nullptr;
};

inline brls::RecyclerCell* FileDataSource::cellForRow(
    brls::RecyclerFrame* recycler, brls::IndexPath index) {
    auto* cell = static_cast<FileCell*>(recycler->dequeueReusableCell("File"));
    const size_t i = static_cast<size_t>(index.row);
    if (i < entries_.size()) {
        // Se compara la ruta entera, no el nombre: dos carpetas pueden tener un
        // fichero que se llame igual y marcarian los dos.
        const bool marked =
            !marked_.empty() &&
            marked_ == files::joinPath(owner_->currentPath(), entries_[i].name);
        cell->setEntry(entries_[i], marked);
    }
    return cell;
}

inline void FileDataSource::didSelectRowAt(brls::RecyclerFrame*,
                                           brls::IndexPath index) {
    owner_->activate(static_cast<size_t>(index.row));
}

} // namespace gamehubnx::ui
