// Command netcore is built as a C archive (go build -buildmode=c-archive)
// and linked into the Qt application. It owns all tunnels (Tailscale via
// tsnet, GlobalProtect via libopenconnect + gVisor netstack) and exposes
// them to the RDP engine as loopback port forwards.
//
// The C API is intentionally small and handle-based. Strings returned to C
// must be released with NetcoreFree.
package main

/*
#include <stdlib.h>
*/
import "C"

import (
	"net"
	"sync"
	"time"
	"unsafe"

	"burrow/netcore/internal/forward"
	"burrow/netcore/internal/totp"
)

const version = "0.0.1-dev"

var (
	mu       sync.Mutex
	nextID   C.int = 1
	forwards       = map[C.int]*forward.Forwarder{}
)

//export NetcoreVersion
func NetcoreVersion() *C.char { return C.CString(version) }

//export NetcoreFree
func NetcoreFree(p *C.char) { C.free(unsafe.Pointer(p)) }

// NetcoreStartDirectForward starts a loopback forward to target without any
// tunnel (the "direct" connection profile). Tunnel-backed variants will be
// added alongside the tsnet / openconnect integrations.
// Returns a handle > 0 and writes the "127.0.0.1:port" address to *outAddr,
// or returns 0 on error.
//
//export NetcoreStartDirectForward
func NetcoreStartDirectForward(target *C.char, outAddr **C.char) C.int {
	f := &forward.Forwarder{
		Dialer:   &net.Dialer{Timeout: 15 * time.Second},
		Target:   C.GoString(target),
		MaxConns: 1,
		OneShot:  true,
	}
	addr, err := f.Start()
	if err != nil {
		return 0
	}
	mu.Lock()
	id := nextID
	nextID++
	forwards[id] = f
	mu.Unlock()
	*outAddr = C.CString(addr)
	return id
}

//export NetcoreStopForward
func NetcoreStopForward(handle C.int) {
	mu.Lock()
	f := forwards[handle]
	delete(forwards, handle)
	mu.Unlock()
	if f != nil {
		_ = f.Close()
	}
}

// NetcoreTOTPFromURI returns the current code for an otpauth:// URI, or NULL.
// Test/diagnostic helper; in the app, secrets live in the encrypted vault.
//
//export NetcoreTOTPFromURI
func NetcoreTOTPFromURI(uri *C.char) *C.char {
	k, err := totp.ParseURI(C.GoString(uri))
	if err != nil {
		return nil
	}
	code, err := k.Code(time.Now())
	if err != nil {
		return nil
	}
	return C.CString(code)
}

func main() {}
