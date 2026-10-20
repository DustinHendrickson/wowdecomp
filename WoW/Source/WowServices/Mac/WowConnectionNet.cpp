// macOS implementation of the WowConnectionNet platform layer (Win64/WowConnectionNet.cpp).
//
// Windows waits on one Winsock event per connection (WSAEventSelect) plus the net's own event. Here the network thread
// polls the sockets, with a pipe standing in for the net's event: anything that would SetEvent(m_event) on Windows
// writes a byte to it to wake the poll. The interest set per state and the flags handed to SignalWorker are the
// Windows ones: 2 for readable, accept or close, 1 for writable or connected, 8 while disconnecting. poll is
// level-triggered, but a connection being serviced (m_serviceCount) is left out of the set, as on Windows, so a
// pending read doesn't spin the loop.
//
// The Switch build shares the file. Its poll only takes sockets and it has no pipes, so the wake-up "pipe" there is a
// UDP socket on the loopback address, connected to itself: a datagram sent to it makes it readable.

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <new>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include "WoW/Source/WowServices/WowConnectionNet.h"

namespace {

const uint32_t kMaxConnections = 64;

struct WakePipe {
    int m_read;
    int m_write;
};

WakePipe* Pipe(void* event) {
    return static_cast<WakePipe*>(event);
}

void Wake(void* event) {
    if (event) {
        char byte = 0;
        // A full pipe already has a wake-up pending.
        (void)write(Pipe(event)->m_write, &byte, 1);
    }
}

int MakeWakePipe(int fds[2]) {
#if defined(__SWITCH__)
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0) {
        return -1;
    }

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t length = sizeof(address);

    if (bind(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        getsockname(sock, reinterpret_cast<sockaddr*>(&address), &length) != 0 ||
        connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        close(sock);
        return -1;
    }

    fds[0] = sock;
    fds[1] = sock;
    return 0;
#else
    return pipe(fds);
#endif
}

void Drain(void* event) {
    char buffer[64];

    while (read(Pipe(event)->m_read, buffer, sizeof(buffer)) > 0) {
    }
}

}

void WowConnectionNet::PlatformChangeState(WowConnection* connection, WOW_CONN_STATE state) {
    Wake(this->m_event);
}

void WowConnectionNet::PlatformRun() {
    this->m_networkStarted = true;

    int timeout = 500;

    if (this->m_stop) {
        return;
    }

    do {
        WowConnection* connections[kMaxConnections + 1];
        pollfd fds[kMaxConnections + 1];

        this->m_connectionsLock.Enter();

        connections[0] = nullptr;
        fds[0].fd = Pipe(this->m_event)->m_read;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        uint32_t count = 1;

        for (WowConnection* connection = this->m_connections.Head(); connection; connection = this->m_connections.Next(connection)) {
            if (connection->m_serviceCount) {
                continue;
            }

            short events;

            switch (connection->m_connState) {
                case WOWC_CONNECTING:
                    events = POLLOUT;
                    break;

                case WOWC_LISTENING:
                    events = POLLIN;
                    break;

                case WOWC_CONNECTED:
                    events = POLLIN | (connection->m_sendList.Head() || connection->m_notifyWriteReady ? POLLOUT : 0);
                    break;

                case WOWC_DISCONNECTING:
                    timeout = 0;
                    events = 0;
                    break;

                default:
                    continue;
            }

            if (count <= kMaxConnections) {
                connection->m_refCount.Increment();
                fds[count].fd = connection->m_sock;
                fds[count].events = events;
                fds[count].revents = 0;
                connections[count] = connection;
                count++;

                if (connection->m_lagTestRecvList.Head()) {
                    timeout = 0;
                }
            }
        }

        this->m_connectionsLock.Leave();

        int ready = poll(fds, count, timeout);

        if (ready >= 0) {
            if (fds[0].revents & POLLIN) {
                Drain(this->m_event);
            }

            for (uint32_t i = 1; i < count; i++) {
                WowConnection* connection = connections[i];
                short revents = fds[i].revents;
                uint32_t flags = 0;

                if (revents & (POLLIN | POLLHUP | POLLERR)) {
                    flags = 2;
                }

                if (revents & (POLLOUT | POLLHUP | POLLERR)) {
                    flags |= 1;
                }

                if (connection->m_connState == WOWC_DISCONNECTING) {
                    flags |= 8;
                }

                if (WowConnection::s_lagTestDelayMin || WowConnection::s_lagTestDelayMax) {
                    flags |= 3;
                    timeout = 10;
                } else {
                    timeout = 500;
                }

                if (flags) {
                    this->SignalWorker(connection, flags);
                }
            }
        }

        for (uint32_t i = 1; i < count; i++) {
            connections[i]->Release();
        }
    } while (!this->m_stop);
}

void WowConnectionNet::PlatformInit(bool useEngine) {
    int fds[2];

    if (MakeWakePipe(fds) != 0) {
        return;
    }

    for (int fd : fds) {
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        fcntl(fd, F_SETFD, FD_CLOEXEC);
    }

    WakePipe* wake = new WakePipe;
    wake->m_read = fds[0];
    wake->m_write = fds[1];
    this->m_event = wake;
}

void WowConnectionNet::PlatformDestruct(WowConnection* connection) {
    connection->m_event = nullptr;
}

void WowConnectionNet::PlatformWorkerReady() {
    Wake(this->m_event);
}

void WowConnectionNet::PlatformAdd(WowConnection* connection) {
    int value = 1;
    setsockopt(connection->m_sock, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value));

    // A write to a closed connection must fail, not raise SIGPIPE.
    setsockopt(connection->m_sock, SOL_SOCKET, SO_NOSIGPIPE, &value, sizeof(value));

    Wake(this->m_event);
}

void WowConnectionNet::PlatformRemove(WowConnection* connection) {
    Wake(this->m_event);
}

void WowConnectionNet::PlatformStop() {
    Wake(this->m_event);
}

void WowConnectionNet::PlatformDestroy() {
    WakePipe* wake = Pipe(this->m_event);

    if (wake) {
        close(wake->m_read);

        if (wake->m_write != wake->m_read) {
            close(wake->m_write);
        }
        delete wake;
        this->m_event = nullptr;
    }
}
