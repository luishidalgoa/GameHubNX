#include "gamehub_provider.hpp"

#include "curl_https.hpp"
#include "gamehub_catalog.hpp"

#include <curl/curl.h>

#include <string>

namespace pipensx {

namespace {

// El identificador de cliente es un dato (vive en gamehub_catalog, sin curl);
// aplicarlo a un handle es transporte y vive aqui, donde curl ya esta incluido.
// Mantenerlos separados deja que las pruebas compilen el catalogo sin necesitar
// las cabeceras de libcurl.
curl_slist* applyClientIdentity(CURL* curl) {
    curl_slist* headers = curl_slist_append(
        nullptr, (std::string("X-GameHub-Client: ") + gamehub::kClientId).c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, gamehub::kClientId);
    return headers;
}

// A HEAD against the shop's download route. GameHub answers it with the real
// Content-Length and advertises byte ranges, so the transfer layer can fetch in
// pieces afterwards without a second discovery step.
bool headContentLength(const std::string& url, uint64_t& bytes,
                       std::string& error) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "gamehub: curl init failed";
        return false;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    // Identificarse ante GameHub: asi la descarga queda registrada como de la
    // app de Switch y no como un cliente generico.
    struct curl_slist* headers = applyClientIdentity(curl);
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curlPinHttpsOnly(curl);
    curlPinScheme(curl, url);
    curlApplyTrustedSsl(curl);

    const CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        error = std::string("gamehub: HEAD failed: ") + curl_easy_strerror(rc);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return false;
    }

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (status < 200 || status >= 300) {
        error = "gamehub: HEAD returned HTTP " + std::to_string(status);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return false;
    }

    curl_off_t len = -1;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &len);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (len <= 0) {
        error = "gamehub: no Content-Length";
        return false;
    }
    bytes = static_cast<uint64_t>(len);
    return true;
}

} // namespace

GameHubProvider::SizeProbe GameHubProvider::curlSizeProbe() {
    return [](const std::string& url, uint64_t& bytes, std::string& error) {
        return headContentLength(url, bytes, error);
    };
}

GameHubProvider::GameHubProvider() : probe_(curlSizeProbe()) {}

GameHubProvider::GameHubProvider(SizeProbe probe) : probe_(std::move(probe)) {}

bool GameHubProvider::validate(std::string& error) {
    // Nothing to validate: there is no account, no API key and no quota. The
    // section either answers when the catalogue is refreshed or it does not,
    // and that failure is reported there with far better context than here.
    (void)error;
    return true;
}

bool GameHubProvider::createFromMagnet(const std::string& magnet,
                                       std::string& id, std::string& error) {
    if (magnet.empty()) {
        error = "gamehub: empty download URL";
        return false;
    }
    if (!gamehub::isTrustedUrl(magnet)) {
        error = "gamehub: refusing a URL outside the configured host";
        return false;
    }
    id = magnet;
    return true;
}

bool GameHubProvider::createFromFile(const std::string& torrentPath,
                                     std::string& id, std::string& error) {
    (void)torrentPath;
    (void)id;
    error = "gamehub: entries are direct downloads, not torrent files";
    return false;
}

bool GameHubProvider::fetchInfo(const std::string& id, DebridInfo& info,
                                std::string& error) {
    if (!gamehub::isTrustedUrl(id)) {
        error = "gamehub: refusing a URL outside the configured host";
        return false;
    }

    info.name = gamehub::fileNameFromUrl(id);
    info.rawState = "ready";

    uint64_t bytes = 0;
    std::string probeError;
    // A failed probe is not a failed transfer: the size only drives the
    // progress bar, and the fetch itself learns the length anyway.
    if (probe_ && probe_(id, bytes, probeError)) info.bytes = bytes;

    DebridFile file;
    file.id = id;
    file.path = info.name;
    file.bytes = info.bytes;
    info.files.clear();
    info.files.push_back(file);
    info.links.clear();
    info.links.push_back(id);

    // Ready from the first poll: the file is already sitting on a server the
    // user owns. There is no queue to wait behind and no cache to warm.
    info.phase = DebridInfo::Phase::Ready;
    info.progress = 1.0;
    return true;
}

bool GameHubProvider::selectFiles(const std::string& id,
                                  const std::vector<std::string>& fileIds,
                                  std::string& error) {
    (void)id;
    (void)fileIds;
    (void)error;
    return true;
}

bool GameHubProvider::resolveDownloadUrl(const std::string& id,
                                         const DebridInfo& info,
                                         size_t kthSelected,
                                         const DebridFile& file,
                                         std::string& url, std::string& error) {
    (void)info;
    (void)kthSelected;
    (void)file;
    if (!gamehub::isTrustedUrl(id)) {
        error = "gamehub: refusing a URL outside the configured host";
        return false;
    }
    url = id;
    return true;
}

bool GameHubProvider::remove(const std::string& id, std::string& error) {
    (void)id;
    (void)error;
    return true;
}

const char* GameHubProvider::name() const { return gamehub::kSourceLabel; }

} // namespace pipensx
