#include "VersionManager.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <mz.h>
#include <mz_strm.h>      // mz_stream_write_cb / mz_stream_read_cb 타입 먼저
#include <mz_zip.h>
#include <mz_zip_rw.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>

namespace GK {

// ─── libcurl write callback ───────────────────────────────────
static size_t curlWrite(void* data, size_t sz, size_t n, std::string* out) {
    out->append((char*)data, sz * n);
    return sz * n;
}
static size_t curlWriteFile(void* data, size_t sz, size_t n, FILE* f) {
    return fwrite(data, sz, n, f);
}

bool VersionManager::fetchManifest(std::string& outJson, std::string& outErr) {
    CURL* c = curl_easy_init();
    if (!c) { outErr = "curl_easy_init failed"; return false; }

    // ★ GK_MANIFEST_URL은 CMakeLists.txt 에서 주입
    curl_easy_setopt(c, CURLOPT_URL, GK_MANIFEST_URL);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &outJson);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(c);
    curl_easy_cleanup(c);

    if (res != CURLE_OK) { outErr = curl_easy_strerror(res); return false; }
    return true;
}

bool VersionManager::parseManifest(const std::string& json, std::string& outErr) {
    try {
        auto j = nlohmann::json::parse(json);
        m_versions.clear();
        for (auto& v : j["versions"]) {
            EngineVersion ev;
            ev.tag          = v.value("tag","");
            ev.displayName  = v.value("name", "GK Engine " + ev.tag);
            ev.downloadUrl  = v.value("download","");
            ev.releaseNotes = v.value("notes","");
            ev.installed    = isInstalled(ev.tag);
            m_versions.push_back(ev);
        }
        return true;
    } catch(const std::exception& e) { outErr = e.what(); return false; }
}

void VersionManager::checkForUpdates(ProgressCallback onProg,
    std::function<void(bool,const std::string&)> onDone)
{
    onProg(0.1f, "Connecting...");
    std::string json, err;
    if (!fetchManifest(json, err)) { onDone(false, err); return; }
    onProg(0.7f, "Parsing...");
    if (!parseManifest(json, err)) { onDone(false, err); return; }
    scanInstalled();
    onProg(1.0f, "Done");
    onDone(true, "");
}

void VersionManager::installVersion(const EngineVersion& ver,
    ProgressCallback onProg,
    std::function<void(bool,const std::string&)> onDone)
{
    std::string err;
    if (!downloadAndExtract(ver, onProg, err)) { onDone(false, err); return; }
    scanInstalled();
    onDone(true, "");
}

bool VersionManager::downloadAndExtract(const EngineVersion& ver,
    ProgressCallback onProg, std::string& outErr)
{
    auto tmp = std::filesystem::temp_directory_path() / ("gkengine_" + ver.tag + ".zip");
    auto dst = Paths::editorsDir() / ver.tag;
    std::filesystem::create_directories(dst);

    // ── Download ──────────────────────────────────────────────
    onProg(0.1f, "Downloading " + ver.displayName + "...");
    FILE* f = fopen(tmp.string().c_str(), "wb");
    if (!f) { outErr = "Cannot open temp file"; return false; }

    CURL* c = curl_easy_init();
    curl_easy_setopt(c, CURLOPT_URL, ver.downloadUrl.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curlWriteFile);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    CURLcode res = curl_easy_perform(c);
    curl_easy_cleanup(c);
    fclose(f);

    if (res != CURLE_OK) { outErr = curl_easy_strerror(res); return false; }

    // ── Extract ───────────────────────────────────────────────
    onProg(0.7f, "Extracting...");
    void* reader = mz_zip_reader_create();
    if (mz_zip_reader_open_file(reader, tmp.string().c_str()) == MZ_OK) {
        mz_zip_reader_save_all(reader, dst.string().c_str());
        mz_zip_reader_close(reader);
    }
    mz_zip_reader_delete(&reader);

    std::filesystem::remove(tmp);
    onProg(1.0f, "Installed!");
    return true;
}

void VersionManager::scanInstalled() {
    auto base = Paths::editorsDir();
    for (auto& v : m_versions)
        v.installed = std::filesystem::exists(base / v.tag / "GK_Engine.exe");
}

bool VersionManager::isInstalled(const std::string& tag) const {
    return std::filesystem::exists(Paths::editorsDir() / tag / "GK_Engine.exe");
}

std::filesystem::path VersionManager::engineExePath(const std::string& tag) const {
    return Paths::editorsDir() / tag / "GK_Engine.exe";
}

} // namespace GK
