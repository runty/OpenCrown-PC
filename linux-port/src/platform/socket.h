#pragma once
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace dcp::platform {
#ifdef __APPLE__
inline int prepare_socket(int socket) {
    if (socket < 0) return socket;
    int one = 1;
    if (fcntl(socket, F_SETFD, FD_CLOEXEC) == 0 &&
        setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one) == 0) return socket;
    int error = errno;
    close(socket);
    errno = error;
    return -1;
}
#endif
inline int socket_cloexec(int domain, int type, int protocol) {
#ifdef __APPLE__
    return prepare_socket(::socket(domain, type, protocol));
#else
    return ::socket(domain, type | SOCK_CLOEXEC, protocol);
#endif
}
inline int accept_cloexec(int socket, sockaddr* address, socklen_t* length) {
#ifdef __APPLE__
    return prepare_socket(::accept(socket, address, length));
#else
    return ::accept4(socket, address, length, SOCK_CLOEXEC);
#endif
}
}
