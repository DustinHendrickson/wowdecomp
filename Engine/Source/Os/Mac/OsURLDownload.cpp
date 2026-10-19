// macOS implementation of Os/Win64/OsURLDownload.cpp, on Blizzard::URL (libcurl) instead of a second WinINet client.
// The callback contract is the Windows one: data chunks with done = false, then one final call with no data,
// done = true and the error code, after which the download is gone.

#include <cstdint>
#include <new>
#include "Engine/Source/BlizzardCore/Source/System/URL/URL.h"
#include "Engine/Source/Os/OsURLDownload.h"

namespace {

class Download : public Blizzard::URL::DownloadCallback {
    public:
    OsURLDownloadCallback m_callback;
    void* m_param;
    int32_t m_timeout;
    int32_t m_statusCode = 0;
    bool m_stopped = false;

    int32_t GetTimeout() override {
        return this->m_timeout > 0 ? this->m_timeout : DownloadCallback::GetTimeout();
    }

    void GetHTTPStatusCode(int32_t code) override {
        this->m_statusCode = code;
    }

    bool ReceivedData(const void* data, int32_t size) override {
        if (this->m_callback && !this->m_callback(this->m_param, static_cast<const char*>(data), static_cast<uint32_t>(size), false, HTTPError::NoError)) {
            this->m_stopped = true;
        }

        return true;
    }

    bool Stop() override {
        return this->m_stopped;
    }

    void Completed(Status status) override {
        HTTPError::ErrorCode error;

        switch (status) {
            case DOWNLOAD_SUCCESS:
            case DOWNLOAD_STOPPED:
                error = HTTPError::NoError;
                break;
            case DOWNLOAD_TIMEOUT:
                error = HTTPError::Timeout;
                break;
            case DOWNLOAD_NOT_FOUND:
                error = HTTPError::NotFound;
                break;
            default:
                error = HTTPError::Failed;
                break;
        }

        OsURLDownloadCallback callback = this->m_callback;
        void* param = this->m_param;
        delete this;

        if (callback) {
            callback(param, nullptr, 0, true, error);
        }
    }
};

}

OsUrlDownload::OsUrlDownload() {
    this->m_internet = nullptr;
    this->m_connect = nullptr;
    this->m_request = nullptr;
    this->m_connectEvent = nullptr;
    this->m_requestEvent = nullptr;
    this->m_completeEvent = nullptr;
    this->m_dataEvent = nullptr;
    this->m_state = 0;
    this->m_done = false;
    this->m_statusCode = 0;
    this->m_callback = nullptr;
    this->m_error = 0;
    this->m_errorCode = HTTPError::NoError;
}

bool OsUrlDownload::DownloadString(const char* url, OsURLDownloadCallback callback, void* param, int32_t timeout) {
    Download* download = new Download();
    download->m_callback = callback;
    download->m_param = param;
    download->m_timeout = timeout;

    if (!Blizzard::URL::DownloadURL(url, download)) {
        delete download;
        return false;
    }

    return true;
}

bool OsURLDownload(const char* url, OsURLDownloadCallback callback, void* param) {
    return OsUrlDownload::DownloadString(url, callback, param, 5000);
}
