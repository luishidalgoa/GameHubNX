#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include <borealis.hpp>

#include "app/game_metadata_service.hpp"

namespace pipensx::ui {

struct ImageRequestState {
    std::atomic<uint64_t> generation {0};
    std::atomic<bool> pending {false};
};

class AsyncRgbaImage;

struct AsyncImageLifetime {
    std::mutex mutex;
    AsyncRgbaImage* image = nullptr;
};

// ── Apagado ─────────────────────────────────────────────────────────────────
// brls::Image::~Image llama a nvgDeleteImage(Application::getNVGContext(), ...),
// y getNVGContext() desreferencia Application::platform. Ese puntero se borra al
// final de Application::exit() y NUNCA se pone a nullptr, asi que cualquier
// imagen destruida despues --o destruida dos veces-- hace una llamada virtual
// sobre memoria liberada y salta a una direccion basura. En la consola eso es un
// Instruction Abort saltando a 0x0, que es el crash que se veia al cerrar.
//
// getExitEvent() se dispara como PRIMERA sentencia de exit(), antes de que se
// destruya vista alguna. Es la misma señal que usa el TextureCache de borealis
// para limpiarse a si mismo, asi que el patron no es un invento nuestro.
//
// A partir de ahi la textura se suelta sin tocar nvg: el contexto se la lleva
// por delante igual, y no queda nadie a quien devolversela.
inline bool& asyncImageShuttingDown() {
    static bool value = false;
    return value;
}

inline void watchAsyncImageShutdown() {
    static const bool once = [] {
        brls::Application::getExitEvent()->subscribe(
            [] { asyncImageShuttingDown() = true; });
        return true;
    }();
    (void)once;
}

class AsyncRgbaImage : public brls::Image {
public:
    AsyncRgbaImage() : lifetime_(std::make_shared<AsyncImageLifetime>()) {
        lifetime_->image = this;
        watchAsyncImageShutdown();
    }

    ~AsyncRgbaImage() override {
        {
            std::lock_guard<std::mutex> lock(lifetime_->mutex);
            lifetime_->image = nullptr;
        }
        if (asyncImageShuttingDown()) {
            freeTexture = false;
            texture = 0;
        }
    }

    // UI_PLAN F6: synchronous upload for memory-cache hits. UI thread only
    // (needs the live NVG context) — the cover paints in the same frame,
    // so catalog re-entry shows no placeholder flash.
    void setRgbaNow(const uint8_t* pixels, int width, int height) {
        if (!pixels || width <= 0 || height <= 0)
            return;
        NVGcontext* vg = brls::Application::getNVGContext();
        innerSetImage(nvgCreateImageRGBA(vg, width, height, 0, pixels));
    }

    void setRgbaAsync(std::function<void(std::function<void(
        std::shared_ptr<const std::vector<uint8_t>>, int, int)>)> provider) {
        std::weak_ptr<AsyncImageLifetime> weakLifetime = lifetime_;
        provider([weakLifetime](
            std::shared_ptr<const std::vector<uint8_t>> pixels,
            int width, int height) {
            brls::sync([weakLifetime, pixels = std::move(pixels),
                        width, height] {
                auto lifetime = weakLifetime.lock();
                if (!lifetime)
                    return;
                std::lock_guard<std::mutex> lock(lifetime->mutex);
                if (!lifetime->image || !pixels || pixels->empty() ||
                    width <= 0 || height <= 0)
                    return;
                NVGcontext* vg = brls::Application::getNVGContext();
                lifetime->image->innerSetImage(nvgCreateImageRGBA(
                    vg, width, height, 0, pixels->data()));
            });
        });
    }

private:
    std::shared_ptr<AsyncImageLifetime> lifetime_;
};

inline void loadImageInto(AsyncRgbaImage* image, GameMetadataService* service,
                   const std::string& url,
                   const std::shared_ptr<ImageRequestState>& state,
                   uint64_t generation,
                   int maxDim = GameMetadataService::kImageDimCard) {
    if (!image)
        return;
    if (!service || url.empty()) {
        image->clear();
        state->pending = false;
        return;
    }
    // UI_PLAN F6: memory-cache hit → texture in the first frame, skipping
    // the worker queue (disk read + decode) and the placeholder flash.
    if (GameMetadataService::ImageData cached =
            service->cachedImage(url, maxDim)) {
        state->pending = false;
        image->setRgbaNow(cached->pixels.data(), cached->width,
                          cached->height);
        return;
    }
    image->clear();
    state->pending = true;
    image->setRgbaAsync([service, url, state, generation, maxDim](
        std::function<void(std::shared_ptr<const std::vector<uint8_t>>,
                           int, int)> done) {
        service->requestImage(url, [done, state, generation](
            GameMetadataService::ImageData bytes) {
            if (state->generation.load() != generation) {
                // A superseded request must not leave the recycled card marked
                // pending, or its current same-URL binding can be skipped.
                state->pending = false;
                done(nullptr, 0, 0);
                return;
            }
            state->pending = false;
            if (!bytes || bytes->pixels.empty()) {
                done(nullptr, 0, 0);
                return;
            }
            std::shared_ptr<const std::vector<uint8_t>> pixels(
                bytes, &bytes->pixels);
            done(std::move(pixels), bytes->width, bytes->height);
        }, maxDim);
    });
}

inline void loadImageInto(AsyncRgbaImage* image, GameMetadataService* service,
                   const std::string& url,
                   int maxDim = GameMetadataService::kImageDimCard) {
    auto state = std::make_shared<ImageRequestState>();
    uint64_t generation = ++state->generation;
    loadImageInto(image, service, url, state, generation, maxDim);
}

inline void setArtworkUrl(AsyncRgbaImage* image, GameMetadataService* service,
                   const std::string& url, std::string& currentUrl,
                   const std::shared_ptr<ImageRequestState>& state,
                   int maxDim = GameMetadataService::kImageDimCard) {
    if (currentUrl == url &&
        (image->getTexture() != 0 || state->pending.load()))
        return;
    currentUrl = url;
    uint64_t generation = ++state->generation;
    loadImageInto(image, service, url, state, generation, maxDim);
}

}  // namespace pipensx::ui
