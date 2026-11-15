package gp

import (
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"os"
	"unsafe"

	"golang.org/x/sys/windows"
)

// tunEnd is the side of the packet wire handed to openconnect (which takes ownership once attached).
type tunEnd struct{ handle uintptr }

func (e tunEnd) close() { windows.CloseHandle(windows.Handle(e.handle)) }

// pipeBuffer holds a burst of full-size packets in each direction.
const pipeBuffer = 1 << 20

// newTunWire returns a message-mode named pipe (ADR 0010): openconnect gets the client end as its
// "TAP" handle (it does overlapped ReadFile/WriteFile on it, one IP packet per message) and the
// server end carries the same packets into Burrow's userspace stack. The pipe has a random name,
// a single instance, rejects remote clients, is open to this user only, and must end up connected
// to this very process.
func newTunWire() (tunEnd, io.ReadWriteCloser, error) {
	var nonce [16]byte
	if _, err := rand.Read(nonce[:]); err != nil {
		return tunEnd{}, nil, err
	}
	name, err := windows.UTF16PtrFromString(`\\.\pipe\burrow-gp-` + hex.EncodeToString(nonce[:]))
	if err != nil {
		return tunEnd{}, nil, err
	}
	// Owner only, no inheritance from the default DACL (which lets Everyone read).
	sd, err := windows.SecurityDescriptorFromString("D:P(A;;GA;;;OW)")
	if err != nil {
		return tunEnd{}, nil, err
	}
	sa := &windows.SecurityAttributes{SecurityDescriptor: sd}
	sa.Length = uint32(unsafe.Sizeof(*sa))
	server, err := windows.CreateNamedPipe(name,
		windows.PIPE_ACCESS_DUPLEX|windows.FILE_FLAG_OVERLAPPED|windows.FILE_FLAG_FIRST_PIPE_INSTANCE,
		windows.PIPE_TYPE_MESSAGE|windows.PIPE_READMODE_MESSAGE|windows.PIPE_WAIT|windows.PIPE_REJECT_REMOTE_CLIENTS,
		1, pipeBuffer, pipeBuffer, 0, sa)
	if err != nil {
		return tunEnd{}, nil, fmt.Errorf("couldn't create the tunnel pipe: %v", err)
	}
	client, err := windows.CreateFile(name, windows.GENERIC_READ|windows.GENERIC_WRITE, 0, nil,
		windows.OPEN_EXISTING, windows.FILE_FLAG_OVERLAPPED, 0)
	if err != nil {
		windows.CloseHandle(server)
		return tunEnd{}, nil, fmt.Errorf("couldn't open the tunnel pipe: %v", err)
	}
	fail := func(err error) (tunEnd, io.ReadWriteCloser, error) {
		windows.CloseHandle(client)
		windows.CloseHandle(server)
		return tunEnd{}, nil, err
	}
	mode := uint32(windows.PIPE_READMODE_MESSAGE)
	if err := windows.SetNamedPipeHandleState(client, &mode, nil, nil); err != nil {
		return fail(fmt.Errorf("couldn't set up the tunnel pipe: %v", err))
	}
	var pid uint32
	if err := windows.GetNamedPipeClientProcessId(server, &pid); err != nil || pid != windows.GetCurrentProcessId() {
		return fail(errors.New("the tunnel pipe was opened by another process"))
	}
	// Overlapped handles join the Go runtime's I/O completion port (Go 1.25+), so Close unblocks reads.
	return tunEnd{uintptr(client)}, os.NewFile(uintptr(server), "vpn-packets"), nil
}
