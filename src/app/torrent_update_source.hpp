#pragma once

#include <cctype>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <borealis/extern/nlohmann/json.hpp>

#include "catalog_service.hpp"
#include "game_update_service.hpp"
#include "gamehub_catalog.hpp"
#include "shop_update_source.hpp"
#include "update_source.hpp"

namespace gamehubnx {

// Fuente de versiones para GameUpdateService: la tienda y, ademas, las
// actualizaciones que traen los torrents de los juegos. Un torrent no se puede
// leer desde la pestana (hay que resolver el magnet y eso son peers y segundos),
// asi que se sondea aparte y lo que se encuentra se guarda aqui y en disco.
//
// Un juego sin tienda y sin torrent probado no tiene fuente: el estado es el de
// siempre, "sin fuente". Nunca se inventa una version.
class TorrentUpdateSource : public IUpdateMetadataSource {
public:
    TorrentUpdateSource(const CatalogService* catalog, std::string cachePath)
        : catalog_(catalog), cachePath_(std::move(cachePath)) {}

    bool collectLatestVersions(const std::string& titleId,
                               std::vector<std::string>& out) const override {
        bool any = false;
        if (catalog_ && collectShopUpdateVersions(catalog_->entries(), titleId, out))
            any = true;
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = versions_.find(gamehub::baseTitleIdOf(upper(titleId)));
        if (it != versions_.end()) {
            out.insert(out.end(), it->second.begin(), it->second.end());
            any = true;
        }
        return any;
    }

    // El magnet de la entrada de torrent de la tienda para el juego base, o vacio.
    std::string magnetFor(const std::string& titleId) const {
        if (!catalog_)
            return {};
        const std::string base = gamehub::baseTitleIdOf(upper(titleId));
        if (base.empty())
            return {};
        for (const CatalogEntry& entry : catalog_->entries()) {
            if (!entry.directUrl.empty() || entry.shopMagnet.empty())
                continue;
            if (gamehub::baseTitleIdOf(upper(entry.titleId)) == base)
                return entry.shopMagnet;
        }
        return {};
    }

    // Guarda lo que un sondeo encontro. Una lista vacia tambien cuenta: el juego
    // se sondeo y no trae actualizaciones.
    void setVersions(const std::string& titleId, std::vector<std::string> versions) {
        std::lock_guard<std::mutex> lock(mutex_);
        versions_[gamehub::baseTitleIdOf(upper(titleId))] = std::move(versions);
    }

    bool save(std::string& error) const {
        nlohmann::json root;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& entry : versions_)
                root["titles"][entry.first] = entry.second;
        }
        const std::string text = root.dump();
        const std::string tmp = cachePath_ + ".tmp";
        std::FILE* file = std::fopen(tmp.c_str(), "wb");
        if (!file) {
            error = "No se pudo guardar las actualizaciones de torrent.";
            return false;
        }
        const bool ok = std::fwrite(text.data(), 1, text.size(), file) == text.size();
        std::fclose(file);
        if (!ok || std::rename(tmp.c_str(), cachePath_.c_str()) != 0) {
            error = "No se pudo guardar las actualizaciones de torrent.";
            return false;
        }
        return true;
    }

    // Carga lo guardado. Un fichero que no existe o no se entiende es una cache
    // vacia, no un error: se vuelve a sondear.
    void load() {
        std::FILE* file = std::fopen(cachePath_.c_str(), "rb");
        if (!file)
            return;
        std::string text;
        char buffer[4096];
        size_t got = 0;
        while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
            text.append(buffer, got);
        std::fclose(file);
        const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
        if (root.is_discarded() || !root.contains("titles") || !root["titles"].is_object())
            return;
        std::lock_guard<std::mutex> lock(mutex_);
        versions_.clear();
        for (const auto& item : root["titles"].items()) {
            if (!item.value().is_array())
                continue;
            std::vector<std::string> list;
            for (const auto& version : item.value())
                if (version.is_string())
                    list.push_back(version.get<std::string>());
            versions_[item.key()] = std::move(list);
        }
    }

private:
    static std::string upper(std::string text) {
        for (char& c : text)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return text;
    }

    const CatalogService* catalog_;
    std::string cachePath_;
    mutable std::mutex mutex_;
    std::map<std::string, std::vector<std::string>> versions_;
};

} // namespace gamehubnx
