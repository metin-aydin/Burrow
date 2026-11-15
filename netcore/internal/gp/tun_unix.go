//go:build unix

package gp

import (
	"io"
	"os"
	"syscall"

	"burrow/netcore/internal/netstack"
)

// tunEnd is the side of the packet wire handed to openconnect (which takes ownership once attached).
type tunEnd struct{ handle uintptr }

func (e tunEnd) close() { syscall.Close(int(e.handle)) }

// newTunWire returns a datagram socketpair: one end for openconnect's tun, the other carrying the
// same raw IP packets into Burrow's userspace stack.
func newTunWire() (tunEnd, io.ReadWriteCloser, error) {
	fds, err := syscall.Socketpair(syscall.AF_UNIX, syscall.SOCK_DGRAM, 0)
	if err != nil {
		return tunEnd{}, nil, err
	}
	netstack.TuneSocket(fds[0])
	netstack.TuneSocket(fds[1])
	// Non-blocking so the Go poller owns the fd and Close unblocks pending reads.
	if err := syscall.SetNonblock(fds[1], true); err != nil {
		syscall.Close(fds[0])
		syscall.Close(fds[1])
		return tunEnd{}, nil, err
	}
	return tunEnd{uintptr(fds[0])}, os.NewFile(uintptr(fds[1]), "vpn-packets"), nil
}
