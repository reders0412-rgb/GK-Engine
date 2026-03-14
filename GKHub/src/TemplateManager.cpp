#include "TemplateManager.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <mz.h>
#include <mz_strm.h>      // mz_stream_write_cb / mz_stream_read_cb 타입 먼저
#include <mz_zip.h>
#include <mz_zip_rw.h>
#include <fstream>
#include <thread>
#include <iostream>

namespace GK {

// ─── libcurl helpers ──────────────────────────────────────────
static size_t curlWriteStr(void* data, size_t sz, size_t n, std::string* out) {
    out->append((char*)data, sz * n); return sz * n;
}
static size_t curlWriteFile(void* data, size_t sz, size_t n, FILE* f) {
    return fwrite(data, sz, n, f);
}

// ─── singleton ────────────────────────────────────────────────
TemplateManager& TemplateManager::instance() {
    static TemplateManager inst; return inst;
}

// ─── parseFromJson ────────────────────────────────────────────
void TemplateManager::parseFromJson(const std::string& versionsJson) {
    std::lock_guard<std::mutex> lk(m_mutex);
    try {
        auto root = nlohmann::json::parse(versionsJson);
        if (!root.contains("templates")) return;

        m_templates.clear();
        for (auto& t : root["templates"]) {
            TemplateInfo ti;
            ti.id             = t.value("id",          "");
            ti.name           = t.value("name",        ti.id);
            ti.nameKo         = t.value("name_ko",     ti.name);
            ti.description    = t.value("description", "");
            ti.descriptionKo  = t.value("description_ko", ti.description);
            ti.author         = t.value("author",      "");
            ti.thumbnailUrl   = t.value("thumbnail",   "");
            ti.downloadUrl    = t.value("download",    "");
            ti.builtin        = t.value("builtin",     false);
            ti.type           = t.value("type",        "3D");
            if (t.contains("tags"))
                for (auto& tag : t["tags"]) ti.tags.push_back(tag.get<std::string>());

            ti.cacheDir = Paths::templateCacheDir(ti.id);
            ti.cachedLocally = !ti.builtin &&
                               std::filesystem::exists(ti.cacheDir) &&
                               !std::filesystem::is_empty(ti.cacheDir);

            m_templates.push_back(std::move(ti));
        }
    } catch (const std::exception& e) {
        std::cerr << "[TemplateManager] parseFromJson: " << e.what() << "\n";
    }
}

// ─── prefetchThumbnails ───────────────────────────────────────
void TemplateManager::prefetchThumbnails() {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (auto& tmpl : m_templates) {
        if (tmpl.thumbnailUrl.empty()) continue;
        // 스레드 분리해서 각각 다운로드
        downloadThumbnailAsync(tmpl);
    }
}

void TemplateManager::downloadThumbnailAsync(TemplateInfo& tmpl) {
    // 썸네일 캐시 경로: Library/thumbnails/<id>.png
    auto thumbDir = Paths::exeDir() / "Library" / "thumbnails";
    std::filesystem::create_directories(thumbDir);
    auto dest = thumbDir / (tmpl.id + ".png");

    // 이미 캐시 있으면 즉시 완료 처리
    if (std::filesystem::exists(dest)) {
        tmpl.thumbnailCachePath = dest;
        tmpl.thumbnailReady = true;
        // 콜백 알림
        for (auto& [id, cb] : m_thumbCbs) cb(tmpl.id);
        return;
    }

    std::string url   = tmpl.thumbnailUrl;
    std::string tmplId = tmpl.id;

    std::thread([this, url, dest, tmplId]() mutable {
        std::string err;
        ProgressCallback dummy = [](float,const std::string&){};
        bool ok = downloadFile(url, dest, dummy, err);

        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& t : m_templates) {
            if (t.id == tmplId) {
                if (ok) {
                    t.thumbnailCachePath = dest;
                    t.thumbnailReady = true;
                }
                break;
            }
        }
        if (ok) {
            for (auto& [id, cb] : m_thumbCbs) cb(tmplId);
        }
    }).detach();
}

// ─── downloadTemplate ─────────────────────────────────────────
void TemplateManager::downloadTemplate(
    const std::string& templateId,
    ProgressCallback onProgress,
    std::function<void(bool ok, const std::string& err)> onDone)
{
    std::string url;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& t : m_templates) {
            if (t.id == templateId) { url = t.downloadUrl; break; }
        }
    }
    if (url.empty()) { onDone(false, "Template not found or no download URL"); return; }

    std::thread([this, templateId, url, onProgress, onDone]() mutable {
        auto tmp  = std::filesystem::temp_directory_path() / ("gktempl_" + templateId + ".zip");
        auto dest = Paths::templateCacheDir(templateId);
        std::filesystem::create_directories(dest);

        onProgress(0.05f, "Connecting...");
        std::string err;
        bool ok = downloadFile(url, tmp, onProgress, err);
        if (!ok) { onDone(false, err); return; }

        onProgress(0.75f, "Extracting...");
        void* reader = mz_zip_reader_create();
        if (mz_zip_reader_open_file(reader, tmp.string().c_str()) == MZ_OK) {
            mz_zip_reader_save_all(reader, dest.string().c_str());
            mz_zip_reader_close(reader);
        }
        mz_zip_reader_delete(&reader);
        std::filesystem::remove(tmp);

        // 캐시 상태 업데이트
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            for (auto& t : m_templates) {
                if (t.id == templateId) {
                    t.cachedLocally = true;
                    t.cacheDir      = dest;
                    break;
                }
            }
        }
        onProgress(1.0f, "Template ready!");
        onDone(true, "");
    }).detach();
}

// ─── scanLocalCache ───────────────────────────────────────────
void TemplateManager::scanLocalCache() {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (auto& t : m_templates) {
        if (t.builtin) continue;
        t.cacheDir      = Paths::templateCacheDir(t.id);
        t.cachedLocally = std::filesystem::exists(t.cacheDir) &&
                          !std::filesystem::is_empty(t.cacheDir);
    }
}

// ─── downloadFile ─────────────────────────────────────────────
bool TemplateManager::downloadFile(
    const std::string& url,
    const std::filesystem::path& dest,
    ProgressCallback onProgress,
    std::string& outErr)
{
    FILE* f = fopen(dest.string().c_str(), "wb");
    if (!f) { outErr = "Cannot open dest file: " + dest.string(); return false; }

    CURL* c = curl_easy_init();
    curl_easy_setopt(c, CURLOPT_URL,           url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curlWriteFile);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,     f);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT,       60L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER,1L);
    CURLcode res = curl_easy_perform(c);
    curl_easy_cleanup(c);
    fclose(f);

    if (res != CURLE_OK) {
        outErr = curl_easy_strerror(res);
        std::filesystem::remove(dest);
        return false;
    }
    return true;
}

// ─── callbacks ────────────────────────────────────────────────
int TemplateManager::onThumbnailReady(ThumbCallback cb) {
    int id = m_nextCbId++; m_thumbCbs[id] = std::move(cb); return id;
}
void TemplateManager::removeCallback(int id) { m_thumbCbs.erase(id); }

} // namespace GK
