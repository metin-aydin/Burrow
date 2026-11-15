//go:build unix

package netstack

import (
	"fmt"
	"net/netip"
	"os"
	"syscall"
)

// BSD/macOS tun framing values (only used when Options.AFPrefix is set, which is never on Windows).
const (
	afInet  = syscall.AF_INET
	afInet6 = syscall.AF_INET6
)

// New starts the stack on fd (one raw IP packet per datagram), with this side's VPN address and
// the VPN's DNS servers.
func New(fd int, mtu int, local netip.Addr, dns []netip.Addr) (*Stack, error) {
	return NewWithOptions(fd, mtu, local, dns, Options{})
}

// NewWithOptions is New on a datagram socket with framing options. The stack owns fd.
func NewWithOptions(fd int, mtu int, local netip.Addr, dns []netip.Addr, opt Options) (*Stack, error) {
	TuneSocket(fd)
	// Non-blocking so the Go poller owns the fd and Close unblocks pending reads.
	if err := syscall.SetNonblock(fd, true); err != nil {
		return nil, fmt.Errorf("netstack: %v", err)
	}
	return NewPacketConn(os.NewFile(uintptr(fd), "vpn-packets"), mtu, local, dns, opt)
}

// TuneSocket raises the buffers of a packet socket (macOS defaults to a few KB for datagram
// sockets, which drops bursts of full-size packets).
func TuneSocket(fd int) {
	for _, opt := range []int{syscall.SO_SNDBUF, syscall.SO_RCVBUF} {
		for size := 4 << 20; size >= 64<<10; size /= 2 {
			if syscall.SetsockoptInt(fd, syscall.SOL_SOCKET, opt, size) == nil {
				break
			}
		}
	}
}
