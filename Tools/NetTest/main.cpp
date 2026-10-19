// Network test: drives WowConnection (the client's TCP layer, on the Mac poll() loop in
// WowServices/Mac/WowConnectionNet.cpp) against a loopback server on plain BSD sockets. It covers connecting by address
// and by name, framed messages with 2- and 3-byte headers, raw stream reads, queued sends larger than the socket
// buffer, disconnects from either side, refused connections, and an idle connection not spinning the CPU.

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/resource.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/Os/Time.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/WowServices/WowConnection.h"

// The client console (Console module) isn't linked; WowServices logs through it.
void ConsolePrintf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    printf("     console: ");
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

static int s_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } else { printf("ok   %s\n", #cond); } } while (0)

class Recorder : public WowConnectionResponse {
    public:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    int m_connected = 0;
    int m_cantConnect = 0;
    int m_disconnected = 0;
    std::vector<std::string> m_messages;
    std::string m_stream;

    void WCMessageReady(WowConnection* connection, uint32_t timeStamp, CDataStore* msg) override {
        std::lock_guard<std::mutex> lock(this->m_mutex);
        this->m_messages.emplace_back(reinterpret_cast<const char*>(msg->m_data), msg->m_size);
        this->m_changed.notify_all();
    }

    void WCConnected(WowConnection* connection, WowConnection* inbound, uint32_t timeStamp, const NETCONNADDR* addr) override {
        std::lock_guard<std::mutex> lock(this->m_mutex);
        this->m_connected++;
        this->m_changed.notify_all();
    }

    void WCCantConnect(WowConnection* connection, uint32_t timeStamp, const NETCONNADDR* addr) override {
        std::lock_guard<std::mutex> lock(this->m_mutex);
        this->m_cantConnect++;
        this->m_changed.notify_all();
    }

    void WCDisconnected(WowConnection* connection, uint32_t timeStamp, const NETCONNADDR* addr) override {
        std::lock_guard<std::mutex> lock(this->m_mutex);
        this->m_disconnected++;
        this->m_changed.notify_all();
    }

    void WCDataReady(WowConnection* connection, uint32_t timeStamp, uint8_t* data, int32_t length) override {
        std::lock_guard<std::mutex> lock(this->m_mutex);
        this->m_stream.append(reinterpret_cast<const char*>(data), length);
        this->m_changed.notify_all();
    }

    bool Wait(const std::function<bool()>& done, int milliseconds = 5000) {
        std::unique_lock<std::mutex> lock(this->m_mutex);
        return this->m_changed.wait_for(lock, std::chrono::milliseconds(milliseconds), done);
    }
};

// A listening socket on 127.0.0.1 with an ephemeral port.
static int Listen(uint16_t* port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address = {};
    address.sin_len = sizeof(address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    listen(sock, 4);

    socklen_t length = sizeof(address);
    getsockname(sock, reinterpret_cast<sockaddr*>(&address), &length);
    *port = ntohs(address.sin_port);
    return sock;
}

static int Accept(int listener) {
    timeval timeout = { 5, 0 };
    int sock = accept(listener, nullptr, nullptr);

    if (sock >= 0) {
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        int value = 1;
        setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &value, sizeof(value));
    }

    return sock;
}

static bool SendAll(int sock, const std::string& data) {
    return send(sock, data.data(), data.size(), 0) == static_cast<ssize_t>(data.size());
}

static std::string Receive(int sock, size_t size) {
    std::string data;
    char buffer[65536];

    while (data.size() < size) {
        ssize_t bytes = recv(sock, buffer, std::min(sizeof(buffer), size - data.size()), 0);

        if (bytes <= 0) {
            break;
        }

        data.append(buffer, bytes);
    }

    return data;
}

// A world-server style frame: big-endian size, 3 bytes with the top bit set when it doesn't fit in 15 bits.
static std::string Frame(const std::string& payload) {
    std::string frame;
    size_t size = payload.size();

    if (size > 0x7FFF) {
        frame += static_cast<char>(0x80 | ((size >> 16) & 0x7F));
        frame += static_cast<char>((size >> 8) & 0xFF);
        frame += static_cast<char>(size & 0xFF);
    } else {
        frame += static_cast<char>((size >> 8) & 0x7F);
        frame += static_cast<char>(size & 0xFF);
    }

    return frame + payload;
}

static std::string Pattern(size_t size, uint32_t seed) {
    std::string data(size, '\0');

    for (size_t i = 0; i < size; i++) {
        seed = seed * 1103515245 + 12345;
        data[i] = static_cast<char>(seed >> 16);
    }

    return data;
}

static WowConnection* NewConnection(Recorder* recorder, WOWC_TYPE type) {
    WowConnection* connection = new (SMemAlloc(sizeof(WowConnection), __FILE__, __LINE__, 0)) WowConnection(recorder, nullptr);
    connection->SetType(type);
    return connection;
}

static double CpuSeconds() {
    rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6 + usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
}

static void TestMessages() {
    printf("-- messages\n");
    uint16_t port;
    int listener = Listen(&port);
    Recorder recorder;
    WowConnection* connection = NewConnection(&recorder, WOWC_TYPE_MESSAGES);

    CHECK(connection->Connect("127.0.0.1", port, 5000));
    int server = Accept(listener);
    CHECK(server >= 0);
    CHECK(recorder.Wait([&] { return recorder.m_connected == 1; }));
    CHECK(connection->m_connState == WOWC_CONNECTED);

    // Two frames in one write, then a 100000-byte frame (3-byte header, read buffer growth, partial reads).
    std::string large = Pattern(100000, 1);
    CHECK(SendAll(server, Frame("hello") + Frame("abc")));
    CHECK(SendAll(server, Frame(large)));
    CHECK(recorder.Wait([&] { return recorder.m_messages.size() == 3; }));

    if (recorder.m_messages.size() == 3) {
        CHECK(recorder.m_messages[0] == "hello");
        CHECK(recorder.m_messages[1] == "abc");
        CHECK(recorder.m_messages[2] == large);
    }

    // An idle connected socket must leave the network thread asleep in poll().
    double cpu = CpuSeconds();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    cpu = CpuSeconds() - cpu;
    printf("     cpu while idle: %.3f s over 1 s\n", cpu);
    CHECK(cpu < 0.05);

    // Client to server: a small send, then 4 MB, more than the socket buffer holds, so most of it is queued and
    // written as the socket drains (POLLOUT).
    uint8_t ping[] = { 'p', 'i', 'n', 'g' };
    CHECK(connection->Send(ping, sizeof(ping), true) == WC_SEND_SENT);
    CHECK(Receive(server, 4) == "ping");

    std::string bulk = Pattern(4 * 1024 * 1024, 2);
    WC_SEND_RESULT result = connection->Send(reinterpret_cast<uint8_t*>(&bulk[0]), static_cast<int32_t>(bulk.size()), true);
    CHECK(result == WC_SEND_QUEUED || result == WC_SEND_SENT);
    std::string received = Receive(server, bulk.size());
    printf("     bulk: received %zu of %zu bytes\n", received.size(), bulk.size());
    CHECK(received == bulk);

    // Server closes: the client reports the disconnect.
    close(server);
    CHECK(recorder.Wait([&] { return recorder.m_disconnected == 1; }));
    CHECK(connection->m_connState == WOWC_DISCONNECTED);

    close(listener);
}

static void TestStreamAndClientDisconnect() {
    printf("-- stream, connect by name, client disconnect\n");
    uint16_t port;
    int listener = Listen(&port);
    Recorder recorder;
    WowConnection* connection = NewConnection(&recorder, WOWC_TYPE_STREAM);

    CHECK(connection->Connect("localhost", port, 5000));
    int server = Accept(listener);
    CHECK(server >= 0);
    CHECK(recorder.Wait([&] { return recorder.m_connected == 1; }));

    std::string data = Pattern(300000, 3);
    CHECK(SendAll(server, data));
    CHECK(recorder.Wait([&] { return recorder.m_stream.size() >= data.size(); }));
    CHECK(recorder.m_stream == data);

    // Client disconnects: the network thread closes the socket and the server reads end of stream.
    connection->Disconnect();
    char byte;
    CHECK(recv(server, &byte, 1, 0) == 0);
    CHECK(recorder.Wait([&] { return connection->m_connState == WOWC_DISCONNECTED; }, 2000));

    close(server);
    close(listener);
}

static void TestRefused() {
    printf("-- refused\n");
    uint16_t port;
    int listener = Listen(&port);
    close(listener);

    Recorder recorder;
    WowConnection* connection = NewConnection(&recorder, WOWC_TYPE_MESSAGES);
    connection->Connect("127.0.0.1", port, 5000);
    CHECK(recorder.Wait([&] { return recorder.m_cantConnect == 1; }));
    printf("     state after refused connect: %d\n", connection->m_connState);
    CHECK(recorder.m_connected == 0);
}

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, nullptr, _IONBF, 0);
    StormInitialize();
    OsTimeStartup(OsTimeManager::BestAvailable);

    CHECK(WowConnection::InitOsNet(nullptr, nullptr, 1, false));

    TestMessages();
    TestStreamAndClientDisconnect();
    TestRefused();

    WowConnection::DestroyOsNet();
    printf("ok   network stopped\n");

    printf("%s (%d failures)\n", s_failures ? "FAILED" : "PASSED", s_failures);
    return s_failures != 0;
}
