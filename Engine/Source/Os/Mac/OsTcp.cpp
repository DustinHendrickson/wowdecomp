// macOS implementation of the Os/Win64/OsTcp.cpp functions the client uses. NETADDR holds a sockaddr_in; macOS's has a
// length byte in front of the family, which only this file looks at. The asynchronous TCPNET is never created by the
// client (its s_pnet stays null), so OsNetPump only waits, as it does on Windows.

#include <arpa/inet.h>
#include <sys/socket.h>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <unistd.h>
#include "Engine/Source/Os/OsTcp.h"
#include "Engine/Source/Storm/H/storm.h"

static_assert(sizeof(NETADDR) >= sizeof(sockaddr_in), "NETADDR must hold a sockaddr_in");

void OsNetAddrMake(uint32_t address, uint16_t port, NETADDR* addr) {
    sockaddr_in* in = reinterpret_cast<sockaddr_in*>(addr);
    memset(in, 0, sizeof(*in));
    in->sin_len = sizeof(sockaddr_in);
    in->sin_family = AF_INET;
    in->sin_port = htons(port);
    in->sin_addr.s_addr = address;
}

uint32_t OsNetAddrGetAddress(const NETADDR* addr, uint16_t* port) {
    const sockaddr_in* in = reinterpret_cast<const sockaddr_in*>(addr);

    if (port) {
        *port = ntohs(in->sin_port);
    }

    return in->sin_addr.s_addr;
}

const char* OsNetAddrToStr(const NETADDR* addr, char* buffer, uint32_t size) {
    const sockaddr_in* in = reinterpret_cast<const sockaddr_in*>(addr);
    char text[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &in->sin_addr, text, sizeof(text));
    SStrPrintf(buffer, size, "%s:%u", text, ntohs(in->sin_port));

    return buffer;
}

void OsNetPump(uint32_t timeout) {
    usleep(static_cast<useconds_t>(timeout) * 1000);
}
