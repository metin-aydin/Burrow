// Package gp connects to a GlobalProtect gateway with libopenconnect inside Burrow's process and
// carries the VPN packets on a userspace IP stack, so no tun device, root rights or system routes
// are needed (ADR 0001).
//
// Login safety (feedback-auth-retry-safety): credentials are filled from the profile, the second
// password form (GlobalProtect's portal→gateway step) gets a fresh OTP from the next TOTP window,
// and any further prompt cancels the login instead of retrying, so a wrong secret can't lock the
// account.
package gp

/*
#cgo pkg-config: openconnect
#include <stdlib.h>
#include <string.h>
#include "shim.h"
*/
import "C"

import (
	"context"
	"errors"
	"fmt"
	"net"
	"net/netip"
	"runtime"
	"runtime/cgo"
	"strings"
	"sync"
	"syscall"
	"time"
	"unsafe"

	"burrow/netcore/internal/netstack"
	"burrow/netcore/internal/totp"
	"burrow/netcore/internal/tunnel"
)

// Config is a GlobalProtect profile with its secrets (from the vault).
type Config struct {
	Host      string
	Username  string
	Password  string
	AuthGroup string
	OTP       bool
	OTPMode   string // "append" (password+OTP) | "separate"
	OTPSource string // "totp" | "manual"
	TOTP      *totp.Key
}

type Tunnel struct {
	cfg    Config
	handle cgo.Handle
	status tunnel.StatusBox

	vpn   *C.struct_openconnect_info
	cmdFd C.int
	stack *netstack.Stack

	mu            sync.Mutex
	passwordForms int
	lastOTP       string
	lastError     string
	otpCh         chan string
	done          chan struct{}
	closing       bool
}

var initSSL sync.Once

// Start begins connecting in the background; watch Status.
func Start(cfg Config) (*Tunnel, error) {
	if cfg.Host == "" || cfg.Username == "" {
		return nil, errors.New("gp: server and username are required")
	}
	if cfg.OTP && cfg.OTPSource == "totp" && cfg.TOTP == nil {
		return nil, errors.New("gp: OTP key missing")
	}
	t := &Tunnel{cfg: cfg, otpCh: make(chan string, 1), done: make(chan struct{})}
	t.handle = cgo.NewHandle(t)
	t.status.Set(tunnel.Status{State: tunnel.Starting})
	go t.run()
	return t, nil
}

func (t *Tunnel) fail(msg string) {
	t.status.Set(tunnel.Status{State: tunnel.Failed, Error: msg})
}

func (t *Tunnel) run() {
	defer close(t.done)
	defer t.handle.Delete()
	// openconnect isn't thread-safe; keep the whole session on one OS thread.
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()

	initSSL.Do(func() { C.openconnect_init_ssl() })
	t.vpn = C.burrow_oc_new(C.uintptr_t(t.handle))
	if t.vpn == nil {
		t.fail("couldn't start openconnect")
		return
	}
	defer C.openconnect_vpninfo_free(t.vpn)
	C.openconnect_set_loglevel(t.vpn, C.PRG_INFO)

	proto := C.CString("gp")
	defer C.free(unsafe.Pointer(proto))
	host := C.CString(t.cfg.Host)
	defer C.free(unsafe.Pointer(host))
	if C.openconnect_set_protocol(t.vpn, proto) != 0 || C.openconnect_parse_url(t.vpn, host) != 0 {
		t.fail("invalid GlobalProtect server")
		return
	}
	t.mu.Lock()
	t.cmdFd = C.openconnect_setup_cmd_pipe(t.vpn)
	closing := t.closing
	t.mu.Unlock()
	if closing {
		return
	}

	if C.openconnect_obtain_cookie(t.vpn) != 0 {
		t.fail(t.loginError())
		return
	}
	if C.openconnect_make_cstp_connection(t.vpn) != 0 {
		t.fail(t.errorOr("couldn't open the tunnel"))
		return
	}
	_ = C.openconnect_setup_dtls(t.vpn, 60) // ESP/UDP when possible; HTTPS otherwise

	var addr, addr6, dns0, dns1, dns2 *C.char
	var mtu C.int
	if C.burrow_oc_ip_info(t.vpn, &addr, &addr6, &dns0, &dns1, &dns2, &mtu) != 0 || addr == nil {
		t.fail("the gateway didn't assign an address")
		return
	}
	local, err := netip.ParseAddr(C.GoString(addr))
	if err != nil {
		t.fail("the gateway assigned an invalid address")
		return
	}
	var dns []netip.Addr
	for _, d := range []*C.char{dns0, dns1, dns2} {
		if d != nil {
			if a, err := netip.ParseAddr(C.GoString(d)); err == nil {
				dns = append(dns, a)
			}
		}
	}

	fds, err := syscall.Socketpair(syscall.AF_UNIX, syscall.SOCK_DGRAM, 0)
	if err != nil {
		t.fail(err.Error())
		return
	}
	netstack.TuneSocket(fds[0])
	if C.openconnect_setup_tun_fd(t.vpn, C.int(fds[0])) != 0 {
		syscall.Close(fds[0])
		syscall.Close(fds[1])
		t.fail("couldn't attach the tunnel")
		return
	}
	st, err := netstack.New(fds[1], int(mtu), local, dns)
	if err != nil {
		syscall.Close(fds[1])
		t.fail(err.Error())
		return
	}
	t.mu.Lock()
	t.stack = st
	t.mu.Unlock()
	defer st.Close()

	t.status.Set(tunnel.Status{State: tunnel.Connected, Address: local.String()})
	rc := C.openconnect_mainloop(t.vpn, 300, 10)
	t.mu.Lock()
	closing = t.closing
	t.mu.Unlock()
	if closing || rc == 0 {
		t.status.Set(tunnel.Status{State: tunnel.Stopped})
	} else {
		t.fail(t.errorOr("the VPN connection dropped"))
	}
}

func (t *Tunnel) errorOr(fallback string) string {
	t.mu.Lock()
	defer t.mu.Unlock()
	if t.lastError != "" {
		return t.lastError
	}
	return fallback
}

func (t *Tunnel) loginError() string {
	t.mu.Lock()
	defer t.mu.Unlock()
	if t.passwordForms > 2 {
		return "The VPN rejected the login. Burrow stopped instead of retrying, so the account isn't locked. Check the password and OTP settings."
	}
	if t.lastError != "" {
		return t.lastError
	}
	return "The VPN login failed."
}

// nextOTP returns a code that hasn't been sent yet in this login, waiting for the next TOTP window
// when needed (GlobalProtect rejects a reused code at the gateway step).
func (t *Tunnel) nextOTP(prompt string) (string, error) {
	if t.cfg.OTPSource == "manual" {
		t.status.Set(tunnel.Status{State: tunnel.NeedsOTP, Prompt: prompt})
		select {
		case code := <-t.otpCh:
			t.status.Set(tunnel.Status{State: tunnel.Starting})
			return code, nil
		case <-time.After(3 * time.Minute):
			return "", errors.New("no OTP entered")
		}
	}
	deadline := time.Now().Add(time.Duration(t.cfg.TOTP.Period+5) * time.Second)
	for {
		code, err := t.cfg.TOTP.Code(time.Now())
		if err != nil {
			return "", err
		}
		t.mu.Lock()
		fresh := code != t.lastOTP
		if fresh {
			t.lastOTP = code
		}
		t.mu.Unlock()
		if fresh {
			return code, nil
		}
		if time.Now().After(deadline) {
			return "", errors.New("no new OTP")
		}
		time.Sleep(500 * time.Millisecond)
	}
}

func (t *Tunnel) authForm(form *C.struct_oc_auth_form) C.int {
	if form.authgroup_opt != nil && t.cfg.AuthGroup != "" {
		opt := &form.authgroup_opt.form
		if !t.selectChoice(opt, t.cfg.AuthGroup) {
			t.setError(fmt.Sprintf("gateway group %q isn't offered by the server", t.cfg.AuthGroup))
			return C.OC_FORM_RESULT_CANCELLED
		}
	}
	hasPassword := false
	for opt := form.opts; opt != nil; opt = C.burrow_oc_opt_next(opt) {
		if opt.flags&C.OC_FORM_OPT_IGNORE != 0 {
			continue
		}
		if opt._type == C.OC_FORM_OPT_PASSWORD || opt._type == C.OC_FORM_OPT_TOKEN {
			hasPassword = true
		}
	}
	if hasPassword {
		t.mu.Lock()
		t.passwordForms++
		n := t.passwordForms
		t.mu.Unlock()
		if n > 2 {
			// Portal + gateway are expected; a third prompt means the login was rejected.
			return C.OC_FORM_RESULT_CANCELLED
		}
	}
	firstPassword := true
	for opt := form.opts; opt != nil; opt = C.burrow_oc_opt_next(opt) {
		if opt.flags&C.OC_FORM_OPT_IGNORE != 0 {
			continue
		}
		name := strings.ToLower(C.GoString(opt.name))
		label := C.GoString(opt.label)
		var value string
		switch opt._type {
		case C.OC_FORM_OPT_TEXT:
			if strings.Contains(name, "user") || strings.Contains(strings.ToLower(label), "user") {
				value = t.cfg.Username
			} else {
				continue
			}
		case C.OC_FORM_OPT_PASSWORD:
			separateOTP := t.cfg.OTP && t.cfg.OTPMode == "separate" && !firstPassword
			if separateOTP {
				code, err := t.nextOTP(label)
				if err != nil {
					t.setError(err.Error())
					return C.OC_FORM_RESULT_CANCELLED
				}
				value = code
			} else {
				value = t.cfg.Password
				if t.cfg.OTP && t.cfg.OTPMode != "separate" {
					code, err := t.nextOTP(label)
					if err != nil {
						t.setError(err.Error())
						return C.OC_FORM_RESULT_CANCELLED
					}
					value += code
				}
			}
			firstPassword = false
		case C.OC_FORM_OPT_TOKEN:
			if !t.cfg.OTP {
				continue
			}
			code, err := t.nextOTP(label)
			if err != nil {
				t.setError(err.Error())
				return C.OC_FORM_RESULT_CANCELLED
			}
			value = code
		case C.OC_FORM_OPT_SELECT:
			if t.cfg.AuthGroup != "" {
				t.selectChoice(opt, t.cfg.AuthGroup)
			}
			continue
		default:
			continue
		}
		cv := C.CString(value)
		C.openconnect_set_option_value(opt, cv)
		// Wipe our copy; openconnect keeps its own.
		C.memset(unsafe.Pointer(cv), 0, C.size_t(len(value)))
		C.free(unsafe.Pointer(cv))
	}
	return C.OC_FORM_RESULT_OK
}

func (t *Tunnel) selectChoice(opt *C.struct_oc_form_opt, want string) bool {
	n := int(C.burrow_oc_select_count(opt))
	for i := 0; i < n; i++ {
		name := C.GoString(C.burrow_oc_select_choice(opt, C.int(i)))
		label := C.GoString(C.burrow_oc_select_label(opt, C.int(i)))
		if strings.EqualFold(name, want) || strings.EqualFold(label, want) {
			cv := C.CString(name)
			C.openconnect_set_option_value(opt, cv)
			C.free(unsafe.Pointer(cv))
			return true
		}
	}
	return false
}

func (t *Tunnel) setError(msg string) {
	t.mu.Lock()
	t.lastError = msg
	t.mu.Unlock()
}

func (t *Tunnel) DialContext(ctx context.Context, network, address string) (net.Conn, error) {
	t.mu.Lock()
	st := t.stack
	t.mu.Unlock()
	if st == nil || t.status.Get().State != tunnel.Connected {
		return nil, errors.New("globalprotect: not connected")
	}
	return st.DialContext(ctx, network, address)
}

func (t *Tunnel) Status() tunnel.Status { return t.status.Get() }

func (t *Tunnel) SubmitOTP(code string) {
	select {
	case t.otpCh <- strings.TrimSpace(code):
	default:
	}
}

func (t *Tunnel) Close() error {
	t.mu.Lock()
	t.closing = true
	fd := t.cmdFd
	t.mu.Unlock()
	if fd > 0 {
		C.burrow_oc_cancel(fd)
	}
	select {
	case <-t.done:
	case <-time.After(10 * time.Second):
	}
	return nil
}

func fromHandle(h C.uintptr_t) *Tunnel {
	v, ok := cgo.Handle(h).Value().(*Tunnel)
	if !ok {
		return nil
	}
	return v
}

//export goGPAuthForm
func goGPAuthForm(h C.uintptr_t, form *C.struct_oc_auth_form) C.int {
	t := fromHandle(h)
	if t == nil {
		return C.OC_FORM_RESULT_CANCELLED
	}
	return t.authForm(form)
}

//export goGPProgress
func goGPProgress(h C.uintptr_t, level C.int, msg *C.char) {
	t := fromHandle(h)
	if t == nil || level != C.PRG_ERR {
		return
	}
	s := strings.TrimSpace(C.GoString(msg))
	if s == "" || strings.Contains(strings.ToLower(s), "cookie") {
		return // never keep session cookies
	}
	t.setError(s)
}

//export goGPCertRejected
func goGPCertRejected(h C.uintptr_t, reason *C.char) {
	if t := fromHandle(h); t != nil {
		t.setError("The VPN server's certificate isn't trusted: " + C.GoString(reason))
	}
}

//export goGPResolve
func goGPResolve(_ C.uintptr_t, node *C.char) *C.char {
	host := C.GoString(node)
	for _, server := range []string{"1.1.1.1:53", "8.8.8.8:53"} {
		r := &net.Resolver{PreferGo: true, Dial: func(ctx context.Context, network, _ string) (net.Conn, error) {
			return (&net.Dialer{Timeout: 3 * time.Second}).DialContext(ctx, network, server)
		}}
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		addrs, err := r.LookupNetIP(ctx, "ip4", host)
		cancel()
		if err == nil && len(addrs) > 0 {
			return C.CString(addrs[0].String())
		}
	}
	return nil
}
