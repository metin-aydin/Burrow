package main

/*
#include <stdlib.h>
*/
import "C"

import (
	"context"
	"encoding/json"
	"io"
	"net"
	"os"
	"strings"
	"time"
	"unsafe"

	"burrow/netcore/internal/cf"
	"burrow/netcore/internal/forward"
	"burrow/netcore/internal/gp"
	"burrow/netcore/internal/qr"
	"burrow/netcore/internal/totp"
	"burrow/netcore/internal/ts"
	"burrow/netcore/internal/tunnel"
)

var tunnels = map[C.int]tunnel.Tunnel{}

func registerTunnel(t tunnel.Tunnel) C.int {
	mu.Lock()
	defer mu.Unlock()
	id := nextID
	nextID++
	tunnels[id] = t
	return id
}

func lookupTunnel(h C.int) tunnel.Tunnel {
	mu.Lock()
	defer mu.Unlock()
	return tunnels[h]
}

func errorJSON(msg string) *C.char {
	b, _ := json.Marshal(tunnel.Status{State: tunnel.Failed, Error: msg})
	return C.CString(string(b))
}

// NetcoreTunnelStartTailscale starts the Tailscale node of a tunnel profile. Its state is kept in the
// open vault `vaultHandle`; `dir` only holds non-secret runtime files. Returns a handle > 0 or 0.
//
//export NetcoreTunnelStartTailscale
func NetcoreTunnelStartTailscale(vaultHandle C.int, id, hostname, controlURL, dir *C.char) C.int {
	v := lookupVault(vaultHandle)
	if v == nil {
		return 0
	}
	t, err := ts.Start(C.GoString(id), C.GoString(hostname), C.GoString(controlURL), C.GoString(dir), v)
	if err != nil {
		return 0
	}
	return registerTunnel(t)
}

// NetcoreTunnelStartCloudflare starts a Cloudflare Access tunnel profile (ADR 0007); tokens are kept in
// the open vault. `bastion` is optional. Returns a handle > 0 or 0.
//
//export NetcoreTunnelStartCloudflare
func NetcoreTunnelStartCloudflare(vaultHandle C.int, id, bastion *C.char) C.int {
	v := lookupVault(vaultHandle)
	if v == nil {
		return 0
	}
	t, err := cf.Start(cf.Config{ID: C.GoString(id), Bastion: C.GoString(bastion)}, v)
	if err != nil {
		return 0
	}
	return registerTunnel(t)
}

// NetcoreTunnelPrepare readies a tunnel for a target before connecting (Cloudflare: sign-in/token).
//
//export NetcoreTunnelPrepare
func NetcoreTunnelPrepare(h C.int, target *C.char) {
	if p, ok := lookupTunnel(h).(interface{ Prepare(string) }); ok {
		p.Prepare(C.GoString(target))
	}
}

type gpParams struct {
	Host      string `json:"host"`
	Username  string `json:"username"`
	Password  string `json:"password"`
	AuthGroup string `json:"authGroup"`
	OTP       bool   `json:"otpEnabled"`
	OTPMode   string `json:"otpMode"`
	OTPSource string `json:"otpSource"`
	TOTP      struct {
		Secret    string `json:"secret"`
		Algorithm string `json:"algorithm"`
		Digits    int    `json:"digits"`
		Period    int    `json:"period"`
	} `json:"totp"`
}

// NetcoreTunnelStartGlobalProtect starts a GlobalProtect tunnel from a JSON profile (with secrets).
// Returns a handle > 0, or 0 with a JSON status in *errOut (free with NetcoreFreeSecret).
//
//export NetcoreTunnelStartGlobalProtect
func NetcoreTunnelStartGlobalProtect(profile *C.char, errOut **C.char) C.int {
	var p gpParams
	raw := C.GoString(profile)
	if err := json.Unmarshal([]byte(raw), &p); err != nil {
		*errOut = errorJSON("invalid profile")
		return 0
	}
	cfg := gp.Config{Host: p.Host, Username: p.Username, Password: p.Password, AuthGroup: p.AuthGroup,
		OTP: p.OTP, OTPMode: p.OTPMode, OTPSource: p.OTPSource}
	if p.OTP && p.OTPSource == "totp" {
		secret, err := totp.DecodeSecret(p.TOTP.Secret)
		if err != nil {
			*errOut = errorJSON("the OTP key is invalid")
			return 0
		}
		cfg.TOTP = &totp.Key{Secret: secret, Algorithm: totp.Algorithm(strings.ToUpper(p.TOTP.Algorithm)),
			Digits: p.TOTP.Digits, Period: p.TOTP.Period}
	}
	p.Password = ""
	t, err := gp.Start(cfg)
	if err != nil {
		*errOut = errorJSON(err.Error())
		return 0
	}
	return registerTunnel(t)
}

// NetcoreTunnelStatus returns the tunnel status as JSON (free with NetcoreFree).
//
//export NetcoreTunnelStatus
func NetcoreTunnelStatus(h C.int) *C.char {
	t := lookupTunnel(h)
	if t == nil {
		return errorJSON("no such tunnel")
	}
	b, _ := json.Marshal(t.Status())
	return C.CString(string(b))
}

//export NetcoreTunnelSubmitOTP
func NetcoreTunnelSubmitOTP(h C.int, code *C.char) {
	if r, ok := lookupTunnel(h).(tunnel.OTPReceiver); ok {
		r.SubmitOTP(C.GoString(code))
	}
}

//export NetcoreTunnelStop
func NetcoreTunnelStop(h C.int) {
	mu.Lock()
	t := tunnels[h]
	delete(tunnels, h)
	mu.Unlock()
	if t != nil {
		go t.Close()
	}
}

// NetcoreStartTunnelForward is NetcoreStartDirectForward through a tunnel: a one-shot loopback
// listener whose single connection is dialed inside the tunnel.
//
//export NetcoreStartTunnelForward
func NetcoreStartTunnelForward(tunnelHandle C.int, target *C.char, outAddr **C.char) C.int {
	t := lookupTunnel(tunnelHandle)
	if t == nil {
		return 0
	}
	f := &forward.Forwarder{
		Dialer:   dialerFunc(t.DialContext),
		Target:   C.GoString(target),
		MaxConns: 1,
		// FreeRDP may reconnect during security negotiation, so a few accepts are allowed — but only
		// until the session is up (the app calls NetcoreForwardSeal) and never longer than a minute.
		MaxAccepts:   4,
		AcceptWindow: time.Minute,
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

type dialerFunc func(ctx context.Context, network, address string) (net.Conn, error)

func (f dialerFunc) DialContext(ctx context.Context, network, address string) (net.Conn, error) {
	ctx2, cancel := context.WithTimeout(ctx, 20*time.Second)
	defer cancel()
	return f(ctx2, network, address)
}

// NetcoreForwardSeal stops a forward from accepting new connections (call once the RDP session is
// connected); the established connection keeps running.
//
//export NetcoreForwardSeal
func NetcoreForwardSeal(handle C.int) {
	mu.Lock()
	f := forwards[handle]
	mu.Unlock()
	if f != nil {
		f.Seal()
	}
}

// NetcoreConnectFD carries the connected stream socket `fd` (one end of a socketpair the RDP engine
// owns the other end of) to `target`, through tunnel `tunnelHandle` or directly when it is 0. No
// listening port is opened at all (ADR 0001). Takes ownership of fd. Returns 0, or -1 on error.
//
//export NetcoreConnectFD
func NetcoreConnectFD(tunnelHandle C.int, target *C.char, fd C.int) C.int {
	var dial func(ctx context.Context, network, address string) (net.Conn, error)
	if tunnelHandle > 0 {
		t := lookupTunnel(tunnelHandle)
		if t == nil {
			_ = os.NewFile(uintptr(fd), "rdp").Close()
			return -1
		}
		dial = t.DialContext
	} else {
		dial = (&net.Dialer{}).DialContext
	}
	file := os.NewFile(uintptr(fd), "rdp")
	local, err := net.FileConn(file) // dups the descriptor
	file.Close()
	if err != nil {
		return -1
	}
	addr := C.GoString(target)
	go func() {
		defer local.Close()
		ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
		remote, err := dial(ctx, "tcp", addr)
		cancel()
		if err != nil {
			return
		}
		defer remote.Close()
		done := make(chan struct{}, 2)
		go func() { io.Copy(remote, local); closeWrite(remote); done <- struct{}{} }()
		go func() { io.Copy(local, remote); closeWrite(local); done <- struct{}{} }()
		<-done
		<-done
	}()
	return 0
}

func closeWrite(c net.Conn) {
	if cw, ok := c.(interface{ CloseWrite() error }); ok {
		_ = cw.CloseWrite()
	}
}

// NetcoreDecodeQR decodes a QR code from a w×h 8-bit grayscale buffer and returns its text (free with
// NetcoreFreeSecret: authenticator QR codes contain secrets), or NULL.
//
//export NetcoreDecodeQR
func NetcoreDecodeQR(gray *C.uchar, w, h C.int) *C.char {
	if gray == nil || w <= 0 || h <= 0 || w > 8192 || h > 8192 {
		return nil
	}
	pix := C.GoBytes(unsafe.Pointer(gray), w*h)
	text, err := qr.DecodeGray(pix, int(w), int(h))
	for i := range pix {
		pix[i] = 0
	}
	if err != nil {
		return nil
	}
	return C.CString(text)
}

// NetcoreTunnelPeers lists the machines reachable through a tunnel as JSON (Tailscale only for now),
// or NULL. Free with NetcoreFree.
//
//export NetcoreTunnelPeers
func NetcoreTunnelPeers(h C.int) *C.char {
	p, ok := lookupTunnel(h).(interface {
		Peers(context.Context) ([]ts.Peer, error)
	})
	if !ok {
		return nil
	}
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	peers, err := p.Peers(ctx)
	if err != nil {
		return nil
	}
	b, _ := json.Marshal(peers)
	return C.CString(string(b))
}

// NetcoreCloudflareListApps lists Access applications with an API token (not stored). Returns JSON
// {"apps":[...]} or {"error":"..."}; free with NetcoreFree.
//
//export NetcoreCloudflareListApps
func NetcoreCloudflareListApps(token *C.char) *C.char {
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()
	apps, err := cf.ListApps(ctx, C.GoString(token))
	var b []byte
	if err != nil {
		b, _ = json.Marshal(map[string]string{"error": err.Error()})
	} else {
		b, _ = json.Marshal(map[string]any{"apps": apps})
	}
	return C.CString(string(b))
}
