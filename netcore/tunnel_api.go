package main

/*
#include <stdlib.h>
*/
import "C"

import (
	"context"
	"encoding/json"
	"net"
	"strings"
	"time"

	"burrow/netcore/internal/cf"
	"burrow/netcore/internal/forward"
	"burrow/netcore/internal/gp"
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
		Dialer:     dialerFunc(t.DialContext),
		Target:     C.GoString(target),
		MaxConns:   1,
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
