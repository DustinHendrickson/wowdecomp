// Android implementation of Blizzard::URL (Mac/URL.cpp uses libcurl, which the NDK doesn't have). The client
// downloads two things over HTTP: the login screen's server alert and the patch downloader's files. Neither is
// fetched yet; the login screen shows no alert, as when the server has none.

#include "Engine/Source/BlizzardCore/Source/System/URL/URL.h"

Blizzard::URL::HttpRequest::HttpRequest() {
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
    return false;
}
