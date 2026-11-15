#pragma once

#include <cstddef>

// A connected pair of stream sockets inside this process, used to hand a connection to netcore
// without any listening port (ADR 0001): a socketpair on Unix; on Windows, which has none, a loopback
// TCP pair whose listener is closed at once and whose peer must be our own connecting socket
// (ADR 0010). Sockets are ints, as FreeRDP's TCPConnect returns them (a SOCKET on Windows).
namespace localsocket {

constexpr int kInvalid = -1;

// fds[0] stays with the caller, fds[1] goes to netcore. Neither raises SIGPIPE when written after the
// other side closed.
bool pair(int fds[2]);
void close(int fd);
void shutdownBoth(int fd);
// > 0 readable, 0 timed out, < 0 error.
int waitReadable(int fd, int timeoutMs);
// As recv/send: bytes moved, 0 for an orderly close (recv), < 0 on error.
long receive(int fd, void *buf, std::size_t n);
long sendSome(int fd, const void *buf, std::size_t n);

} // namespace localsocket
