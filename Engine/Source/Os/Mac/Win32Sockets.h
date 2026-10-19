#ifndef ENGINE_SOURCE_OS_MAC_WIN32SOCKETS_H
#define ENGINE_SOURCE_OS_MAC_WIN32SOCKETS_H

// Winsock names on BSD sockets, for Win64/ sources whose socket code is the BSD API under Windows spellings
// (WowServices' WowConnection and BattlenetLogin, ComSat's address helpers). They include this instead of
// <winsock2.h> on macOS and build unchanged. Event-based Winsock (WSAEventSelect and friends) is not here: the
// network loop has a real Mac version (WowServices/Mac/WowConnectionNet.cpp).

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

typedef int SOCKET;

#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)

#define WSAEINTR EINTR
#define WSAEWOULDBLOCK EWOULDBLOCK

inline int closesocket(SOCKET socket) {
    return close(socket);
}

inline int ioctlsocket(SOCKET socket, unsigned long command, u_long* argument) {
    int value = static_cast<int>(*argument);
    return ioctl(socket, command, &value);
}

// A non-blocking connect reports EINPROGRESS where Winsock reports WSAEWOULDBLOCK; callers test for the latter.
inline int WSAGetLastError() {
    return errno == EINPROGRESS ? EWOULDBLOCK : errno;
}

// Winsock takes int lengths where POSIX takes socklen_t.
inline int getpeername(SOCKET socket, sockaddr* address, int* length) {
    socklen_t size = static_cast<socklen_t>(*length);
    int result = ::getpeername(socket, address, &size);
    *length = static_cast<int>(size);
    return result;
}

inline int getsockname(SOCKET socket, sockaddr* address, int* length) {
    socklen_t size = static_cast<socklen_t>(*length);
    int result = ::getsockname(socket, address, &size);
    *length = static_cast<int>(size);
    return result;
}

inline SOCKET accept(SOCKET socket, sockaddr* address, int* length) {
    socklen_t size = static_cast<socklen_t>(*length);
    SOCKET result = ::accept(socket, address, &size);
    *length = static_cast<int>(size);
    return result;
}

inline int getsockopt(SOCKET socket, int level, int name, char* value, int* length) {
    socklen_t size = static_cast<socklen_t>(*length);
    int result = ::getsockopt(socket, level, name, value, &size);
    *length = static_cast<int>(size);
    return result;
}

inline ssize_t recvfrom(SOCKET socket, char* buffer, size_t length, int flags, sockaddr* address, int* addressLength) {
    socklen_t size = static_cast<socklen_t>(*addressLength);
    ssize_t result = ::recvfrom(socket, buffer, length, flags, address, &size);
    *addressLength = static_cast<int>(size);
    return result;
}

#endif
