#include "net/LocalSocket.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mutex>
#else
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace localsocket {

#ifdef _WIN32

namespace {
void startWinsock()
{
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    });
}

SOCKET tcpSocket()
{
    return WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
}
} // namespace

bool pair(int fds[2])
{
    startWinsock();
    SOCKET listener = tcpSocket();
    if (listener == INVALID_SOCKET)
        return false;
    // Exclusive: no other socket can bind the same port to listen alongside ours.
    BOOL on = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&on), sizeof on);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int len = sizeof addr;
    SOCKET client = INVALID_SOCKET, server = INVALID_SOCKET;
    auto fail = [&] {
        for (SOCKET s : {listener, client, server})
            if (s != INVALID_SOCKET)
                closesocket(s);
        return false;
    };
    if (bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0
        || getsockname(listener, reinterpret_cast<sockaddr *>(&addr), &len) != 0 || listen(listener, 1) != 0)
        return fail();
    client = tcpSocket();
    if (client == INVALID_SOCKET || connect(client, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0)
        return fail();
    sockaddr_in peer{}, mine{};
    int peerLen = sizeof peer, mineLen = sizeof mine;
    server = accept(listener, reinterpret_cast<sockaddr *>(&peer), &peerLen);
    closesocket(listener);   // the port is gone before anything else could use it
    listener = INVALID_SOCKET;
    if (server == INVALID_SOCKET || getsockname(client, reinterpret_cast<sockaddr *>(&mine), &mineLen) != 0
        || peer.sin_port != mine.sin_port || peer.sin_addr.s_addr != mine.sin_addr.s_addr)
        return fail();   // another local process got in first: refuse the pair
    for (SOCKET s : {client, server})
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&on), sizeof on);
    fds[0] = int(client);
    fds[1] = int(server);
    return true;
}

void close(int fd)
{
    if (fd != kInvalid)
        closesocket(SOCKET(fd));
}

void shutdownBoth(int fd)
{
    if (fd != kInvalid)
        shutdown(SOCKET(fd), SD_BOTH);
}

int waitReadable(int fd, int timeoutMs)
{
    WSAPOLLFD p{SOCKET(fd), POLLRDNORM, 0};
    return WSAPoll(&p, 1, timeoutMs);
}

long receive(int fd, void *buf, std::size_t n)
{
    return recv(SOCKET(fd), static_cast<char *>(buf), int(n > 0x40000000 ? 0x40000000 : n), 0);
}

long sendSome(int fd, const void *buf, std::size_t n)
{
    return send(SOCKET(fd), static_cast<const char *>(buf), int(n > 0x40000000 ? 0x40000000 : n), 0);
}

#else

bool pair(int fds[2])
{
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
        return false;
#ifdef SO_NOSIGPIPE
    // macOS: a write after the tunnel closed must not raise SIGPIPE (Linux uses MSG_NOSIGNAL).
    const int on = 1;
    setsockopt(fds[0], SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
    return true;
}

void close(int fd)
{
    if (fd != kInvalid)
        ::close(fd);
}

void shutdownBoth(int fd)
{
    if (fd != kInvalid)
        ::shutdown(fd, SHUT_RDWR);
}

int waitReadable(int fd, int timeoutMs)
{
    pollfd p{fd, POLLIN, 0};
    return ::poll(&p, 1, timeoutMs);
}

long receive(int fd, void *buf, std::size_t n)
{
    return long(::recv(fd, buf, n, 0));
}

long sendSome(int fd, const void *buf, std::size_t n)
{
#ifdef MSG_NOSIGNAL
    return long(::send(fd, buf, n, MSG_NOSIGNAL));
#else
    return long(::send(fd, buf, n, 0));
#endif
}

#endif

} // namespace localsocket
