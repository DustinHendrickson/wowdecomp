/*
    macOS port of fmodex/win32/src/fmod_os_net.cpp (wowdecomp): FMOD Ex net streams on BSD sockets. Sockets travel in
    the void* handles, with (void *)-1 for none, as on Windows.
*/

#include "fmod_settings.h"
#include "fmod.h"
#include "fmod_os_net.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

extern int FMOD_Net_Timeout;

namespace
{
    int Socket(const void *handle)
    {
        return (int)(intptr_t)handle;
    }

    void *Handle(int socket)
    {
        return (void *)(intptr_t)socket;
    }
}

FMOD_RESULT FMOD_OS_Net_Init()
{
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Net_Shutdown()
{
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Net_Connect(const char *host, const unsigned short port, void **handle)
{
    *handle = (void *)-1;

    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char service[16];
    snprintf(service, sizeof(service), "%u", port);

    addrinfo *addresses = 0;

    if (getaddrinfo(host, service, &hints, &addresses) || !addresses)
    {
        return FMOD_ERR_NET_URL;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0)
    {
        freeaddrinfo(addresses);
        return FMOD_ERR_MEMORY;
    }

    int value = 1;
    setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &value, sizeof(value));

    /* Connect with the FMOD net timeout, then hand back a blocking socket like the Windows version. */
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK);
    int rc = connect(sock, addresses->ai_addr, addresses->ai_addrlen);
    freeaddrinfo(addresses);

    if (rc && errno == EINPROGRESS)
    {
        pollfd fd;
        fd.fd      = sock;
        fd.events  = POLLOUT;
        fd.revents = 0;

        int error = 0;
        socklen_t length = sizeof(error);

        if (poll(&fd, 1, FMOD_Net_Timeout) == 1 && !getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &length) && !error)
        {
            rc = 0;
        }
    }

    if (rc)
    {
        close(sock);
        return FMOD_ERR_NET_CONNECT;
    }

    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) & ~O_NONBLOCK);
    *handle = Handle(sock);

    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Net_Listen(const unsigned short port, void **listenhandle)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0)
    {
        return FMOD_ERR_MEMORY;
    }

    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK);

    sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_len         = sizeof(address);
    address.sin_family      = AF_INET;
    address.sin_port        = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (sockaddr *)&address, sizeof(address)) || listen(sock, SOMAXCONN))
    {
        close(sock);
        return FMOD_ERR_NET_SOCKET_ERROR;
    }

    *listenhandle = Handle(sock);
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Net_Accept(const void *listenhandle, void **clienthandle)
{
    if (listenhandle == (void *)-1)
    {
        return FMOD_ERR_NET_SOCKET_ERROR;
    }

    int client = accept(Socket(listenhandle), 0, 0);

    if (client < 0)
    {
        return errno == EWOULDBLOCK ? FMOD_ERR_NET_WOULD_BLOCK : FMOD_ERR_NET_SOCKET_ERROR;
    }

    int value = 1;
    setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &value, sizeof(value));
    *clienthandle = Handle(client);

    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Net_Close(const void *handle)
{
    if (handle != (void *)-1)
    {
        close(Socket(handle));
    }

    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Net_Write(const void *handle, const char *buf, const unsigned int len, unsigned int *byteswritten)
{
    if (handle == (void *)-1)
    {
        return FMOD_ERR_NET_SOCKET_ERROR;
    }

    unsigned int written = 0;

    while (written < len)
    {
        ssize_t sent = send(Socket(handle), buf + written, len - written, 0);

        if (sent < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            if (byteswritten)
            {
                *byteswritten = written;
            }

            return errno == EWOULDBLOCK ? FMOD_ERR_NET_WOULD_BLOCK : FMOD_ERR_NET_SOCKET_ERROR;
        }

        written += (unsigned int)sent;
    }

    if (byteswritten)
    {
        *byteswritten = written;
    }

    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Net_Read(const void *handle, char *buf, const unsigned int len, unsigned int *bytesread)
{
    if (handle == (void *)-1)
    {
        return FMOD_ERR_NET_SOCKET_ERROR;
    }

    if (!buf || len <= 0)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    unsigned int total = 0;
    FMOD_RESULT  result = FMOD_OK;

    while (total < len)
    {
        ssize_t got = recv(Socket(handle), buf + total, len - total, 0);

        if (got < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            result = errno == EWOULDBLOCK ? FMOD_ERR_NET_WOULD_BLOCK : FMOD_ERR_NET_SOCKET_ERROR;
            break;
        }

        if (!got)
        {
            result = FMOD_ERR_FILE_EOF;
            break;
        }

        total += (unsigned int)got;
    }

    if (bytesread)
    {
        *bytesread = total;
    }

    return result;
}

FMOD_RESULT FMOD_OS_Net_ReadLine(const void *handle, char *buf, const unsigned int len)
{
    if (handle == (void *)-1)
    {
        return FMOD_ERR_NET_SOCKET_ERROR;
    }

    if (!buf || len <= 0)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    unsigned int pos = 0;
    buf[0] = 0;

    while (pos < len)
    {
        char         c = 0;
        unsigned int read = 0;
        FMOD_RESULT  result = FMOD_OS_Net_Read(handle, &c, 1, &read);

        if (result == FMOD_ERR_NET_WOULD_BLOCK)
        {
            return FMOD_ERR_NET_WOULD_BLOCK;
        }

        if (read != 1 || c == '\n')
        {
            break;
        }

        if (c != '\r')
        {
            buf[pos++] = c;
        }
    }

    if (pos >= len)
    {
        pos = len - 1;
    }

    buf[pos] = 0;
    return FMOD_OK;
}
