// macOS implementation of Blizzard::URL (Win64/URL.cpp) on libcurl, which ships with macOS.
//
// WinINet runs the request asynchronously and reads the response on a pool thread; here the whole transfer runs on a
// pool thread. The callback sequence matches the Windows one: ConnectedOK once the response starts, then the status
// code, Last-Modified and Content-Length for 2xx responses, WantData, the data in chunks with Stop checks after each,
// and finally Completed with the status, after which the request is deleted.

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>
#include <pthread.h>
#include <string>
#include "Engine/Source/BlizzardCore/Source/System/String/String.h"
#include "Engine/Source/BlizzardCore/Source/System/Thread/Thread.h"
#include "Engine/Source/BlizzardCore/Source/System/URL/URL.h"

namespace {

typedef Blizzard::URL::DownloadCallback DownloadCallback;

struct Transfer {
    Blizzard::URL::HttpRequest* m_request;
    std::string m_url;
    CURL* m_curl;
    // Headers of the response being received; redirects start a new one.
    std::string m_lastModified;
    std::string m_contentLength;
    bool m_responseStarted;
    bool m_headersHandled;
    bool m_wantData;
    bool m_success;
    bool m_aborted;
};

pthread_once_t s_curlOnce = PTHREAD_ONCE_INIT;

void InitCurl() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

std::string Trim(const char* data, size_t length) {
    while (length && (data[length - 1] == '\r' || data[length - 1] == '\n' || data[length - 1] == ' ')) {
        length--;
    }

    while (length && *data == ' ') {
        data++;
        length--;
    }

    return std::string(data, length);
}

bool HeaderIs(const char* line, size_t length, const char* name) {
    size_t nameLength = strlen(name);
    return length > nameLength && line[nameLength] == ':' && !strncasecmp(line, name, nameLength);
}

// Runs once the final response's headers are in (at the first body byte, or after the transfer for bodyless ones).
void HandleHeaders(Transfer* transfer) {
    if (transfer->m_headersHandled) {
        return;
    }

    transfer->m_headersHandled = true;

    Blizzard::URL::HttpRequest* request = transfer->m_request;
    long statusCode = 0;
    curl_easy_getinfo(transfer->m_curl, CURLINFO_RESPONSE_CODE, &statusCode);
    request->m_callback->GetHTTPStatusCode(static_cast<int32_t>(statusCode));

    if (statusCode >= 200 && statusCode < 300) {
        transfer->m_success = true;

        if (!transfer->m_lastModified.empty()) {
            request->m_callback->GetLastModified(transfer->m_lastModified.c_str());
        }

        if (!transfer->m_contentLength.empty()) {
            request->m_callback->GetContentLength(static_cast<uint32_t>(atol(transfer->m_contentLength.c_str())));
        }

        transfer->m_wantData = request->m_callback->WantData();
        return;
    }

    switch (statusCode) {
        case 304:
            request->m_status = DownloadCallback::DOWNLOAD_NOT_MODIFIED;
            break;
        case 400:
            request->m_status = DownloadCallback::DOWNLOAD_BAD_REQUEST;
            break;
        case 403:
            request->m_status = DownloadCallback::DOWNLOAD_FORBIDDEN;
            break;
        case 404:
            request->m_status = DownloadCallback::DOWNLOAD_NOT_FOUND;
            break;
        default:
            request->m_status = DownloadCallback::DOWNLOAD_FAILED;
            break;
    }
}

size_t HeaderCallback(char* data, size_t size, size_t count, void* param) {
    Transfer* transfer = static_cast<Transfer*>(param);
    size_t length = size * count;

    if (length >= 5 && !strncmp(data, "HTTP/", 5)) {
        if (!transfer->m_responseStarted) {
            transfer->m_responseStarted = true;
            transfer->m_request->m_callback->ConnectedOK();
            transfer->m_request->m_connected = true;
        }

        transfer->m_lastModified.clear();
        transfer->m_contentLength.clear();
    } else if (HeaderIs(data, length, "Last-Modified")) {
        transfer->m_lastModified = Trim(data + 14, length - 14);
    } else if (HeaderIs(data, length, "Content-Length")) {
        transfer->m_contentLength = Trim(data + 15, length - 15);
    }

    return length;
}

size_t WriteCallback(char* data, size_t size, size_t count, void* param) {
    Transfer* transfer = static_cast<Transfer*>(param);
    Blizzard::URL::HttpRequest* request = transfer->m_request;
    size_t length = size * count;

    HandleHeaders(transfer);

    // Error bodies are never read on Windows, and a caller that doesn't want data stops the transfer here.
    if (!transfer->m_success || !transfer->m_wantData) {
        transfer->m_aborted = true;
        return 0;
    }

    if (!request->m_callback->ReceivedData(data, static_cast<int32_t>(length))) {
        request->m_status = DownloadCallback::DOWNLOAD_ABORTED;
        transfer->m_aborted = true;
        return 0;
    }

    if (request->m_callback->Stop()) {
        request->m_status = DownloadCallback::DOWNLOAD_STOPPED;
        transfer->m_aborted = true;
        return 0;
    }

    return length;
}

uint32_t PerformRequest(void* param) {
    Transfer* transfer = static_cast<Transfer*>(param);
    Blizzard::URL::HttpRequest* request = transfer->m_request;
    DownloadCallback* callback = request->m_callback;

    CURL* curl = curl_easy_init();
    transfer->m_curl = curl;
    curl_slist* headers = nullptr;

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, transfer->m_url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Blizzard Web Client");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, transfer);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, transfer);

        // GetTimeout is in milliseconds and covers both connecting and receiving, as on Windows.
        long timeout = callback->GetTimeout();

        if (timeout > 0) {
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeout);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, (timeout + 999) / 1000);
        }

        const char* postData = callback->GetPostData();

        if (postData) {
            std::string contentType = std::string("Content-Type: ") + (callback->GetPostContentType() ? callback->GetPostContentType() : "text/html");
            headers = curl_slist_append(headers, contentType.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(strlen(postData)));
        } else if (!callback->WantData()) {
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        }

        const char* ifModifiedSince = callback->WantIfModifiedSince();

        if (ifModifiedSince) {
            headers = curl_slist_append(headers, (std::string("If-Modified-Since: ") + ifModifiedSince).c_str());
        }

        DownloadCallback::ByteRange range = callback->GetByteRange();

        if (range.m_start != -1 || range.m_end != -1) {
            char value[64];

            if (range.m_end == -1) {
                Blizzard::String::Format(value, sizeof(value), "%lld-", range.m_start);
            } else {
                Blizzard::String::Format(value, sizeof(value), "%lld-%lld", range.m_start < 0 ? 0 : range.m_start, range.m_end);
            }

            curl_easy_setopt(curl, CURLOPT_RANGE, value);
        }

        DownloadCallback::Cookie cookie = callback->GetCookie();
        std::string cookieValue;

        if (cookie.m_name && *cookie.m_name && cookie.m_value) {
            cookieValue = std::string(cookie.m_name) + "=" + cookie.m_value;
            curl_easy_setopt(curl, CURLOPT_COOKIE, cookieValue.c_str());
        }

        if (headers) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }

        CURLcode result = curl_easy_perform(curl);

        if (result == CURLE_OK || (result == CURLE_WRITE_ERROR && transfer->m_aborted)) {
            HandleHeaders(transfer);
        } else {
            switch (result) {
                case CURLE_OPERATION_TIMEDOUT:
                    request->m_status = DownloadCallback::DOWNLOAD_TIMEOUT;
                    break;
                case CURLE_COULDNT_RESOLVE_HOST:
                    request->m_status = DownloadCallback::DOWNLOAD_NAME_NOT_RESOLVED;
                    break;
                case CURLE_COULDNT_CONNECT:
                    request->m_status = DownloadCallback::DOWNLOAD_CANNOT_CONNECT;
                    break;
                default:
                    request->m_status = DownloadCallback::DOWNLOAD_FAILED;
                    break;
            }
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    } else {
        request->m_status = DownloadCallback::DOWNLOAD_FAILED;
    }

    request->m_done = true;
    callback->Completed(request->m_status);
    delete request;
    delete transfer;

    return 0;
}

}

Blizzard::URL::HttpRequest::HttpRequest() {
    // The events and handles belong to WinINet's asynchronous model and stay unused here.
    this->m_connectEvent = nullptr;
    this->m_requestEvent = nullptr;
    this->m_completeEvent = nullptr;
    this->m_dataEvent = nullptr;
    this->m_session = nullptr;
    this->m_connect = nullptr;
    this->m_request = nullptr;
    this->m_state = 0;
    this->m_done = false;
    this->m_connected = false;
    this->m_callback = nullptr;
    this->m_error = 0;
    this->m_status = Blizzard::URL::DownloadCallback::DOWNLOAD_SUCCESS;
}

Blizzard::URL::HttpRequest::~HttpRequest() {
}

bool Blizzard::URL::DownloadURL(const char* url, DownloadCallback* callback) {
    pthread_once(&s_curlOnce, InitCurl);

    // Like InternetCrackUrl: refuse what isn't an http(s) URL with a host before starting anything.
    CURLU* parsed = curl_url();
    char* scheme = nullptr;
    char* host = nullptr;
    bool valid = parsed && curl_url_set(parsed, CURLUPART_URL, url, 0) == CURLUE_OK && curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0) == CURLUE_OK && curl_url_get(parsed, CURLUPART_HOST, &host, 0) == CURLUE_OK && (!strcasecmp(scheme, "http") || !strcasecmp(scheme, "https"));
    curl_free(scheme);
    curl_free(host);
    curl_url_cleanup(parsed);

    if (!valid) {
        return false;
    }

    HttpRequest* request = new HttpRequest();
    request->m_callback = callback;
    request->m_state = 1;

    Transfer* transfer = new Transfer();
    transfer->m_request = request;
    transfer->m_url = url;

    if (!Blizzard::Thread::CreateDetachedFromPool(nullptr, PerformRequest, transfer, 0, 0)) {
        delete transfer;
        delete request;
        return false;
    }

    return true;
}
