// macOS implementation of ComSat's UDP socket (Win64/ComSatNetworkSocketPosix.cpp).
//
// Address keeps its fields in the Winsock sockaddr_in layout (16-bit family, then port and address). BSD sockaddr_in
// starts with a length byte instead, so the address is converted on the way in and out rather than cast.

#include <cstdint>
#include <cstring>
#include "Engine/Source/ComSat/ComSatNetwork.h"
#include "Engine/Source/Os/Mac/Win32Sockets.h"

namespace {

sockaddr_in ToNative(const ComSat::Network::Address& address) {
    sockaddr_in native;
    memset(&native, 0, sizeof(native));
    native.sin_len = sizeof(native);
    native.sin_family = AF_INET;
    native.sin_port = address.m_port;
    native.sin_addr.s_addr = address.m_ip;
    return native;
}

void FromNative(const sockaddr_in& native, ComSat::Network::Address& address) {
    address.m_family = AF_INET;
    address.m_port = native.sin_port;
    address.m_ip = native.sin_addr.s_addr;
}

int Descriptor(const ComSat::Network::SocketPosix* socket) {
    return static_cast<int>(socket->m_socket);
}

}

ComSat::Network::SocketPosix::SocketPosix() {
    // Windows also turns off SIO_UDP_CONNRESET, which BSD sockets don't have.
    this->m_socket = static_cast<uintptr_t>(socket(AF_INET, SOCK_DGRAM, 0));
    u_long value = 1;
    ioctlsocket(Descriptor(this), FIONBIO, &value);
    int size = 0x8000;
    setsockopt(Descriptor(this), SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
}

ComSat::Network::SocketPosix::~SocketPosix() {
    closesocket(Descriptor(this));
}

bool ComSat::Network::SocketPosix::Bind(int32_t port) {
    ComSat::Network::Address address;
    address.SetPort(port);

    int reuse = 1;
    setsockopt(Descriptor(this), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in native = ToNative(address);
    return bind(Descriptor(this), reinterpret_cast<const sockaddr*>(&native), sizeof(native)) != SOCKET_ERROR;
}

int32_t ComSat::Network::SocketPosix::SendTo(const ComSat::Network::Address& address, const uint8_t* data, uint32_t size) {
    sockaddr_in native = ToNative(address);
    return static_cast<int32_t>(sendto(Descriptor(this), data, size, 0, reinterpret_cast<const sockaddr*>(&native), sizeof(native)));
}

bool ComSat::Network::SocketPosix::RecvFrom(ComSat::Network::Address& address, uint8_t* data, uint32_t& size) {
    memset(data, 0, size);

    sockaddr_in native;
    socklen_t length = sizeof(native);
    ssize_t received = ::recvfrom(Descriptor(this), data, size, 0, reinterpret_cast<sockaddr*>(&native), &length);

    if (received <= 0) {
        return false;
    }

    FromNative(native, address);
    size = static_cast<uint32_t>(received);

    return true;
}
