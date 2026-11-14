// Package forward exposes a tunnel dialer as a loopback TCP listener so that
// the RDP engine (FreeRDP) can connect to 127.0.0.1:<port> while the bytes
// actually travel through an in-process tunnel (tsnet / openconnect netstack).
package forward

import (
	"context"
	"errors"
	"io"
	"net"
	"sync"
	"time"
)

// Dialer opens a TCP connection to addr through some tunnel.
type Dialer interface {
	DialContext(ctx context.Context, network, addr string) (net.Conn, error)
}

// Forwarder listens on a random loopback port and forwards each accepted
// connection to Target through Dialer.
type Forwarder struct {
	Dialer Dialer
	Target string // host:port on the remote side, e.g. "10.0.0.5:3389"

	// MaxConns limits concurrent client connections (0 = unlimited).
	// A single RDP session normally needs exactly one.
	MaxConns int

	// OneShot closes the listener after the first accepted connection, so no
	// other local process can use the port for the rest of the session.
	// Security note: a loopback listener is reachable by any local process
	// until it is closed. The long-term design hands FreeRDP one end of a
	// socketpair via its rdpTransportIo TCPConnect callback (no listener at
	// all); see ADR 0001.
	OneShot bool

	// MaxAccepts closes the listener after this many accepted connections (0 = no limit). RDP
	// clients may reconnect during security negotiation, so tunnel forwards allow a few.
	MaxAccepts int

	ln       net.Listener
	accepted int
	wg     sync.WaitGroup
	mu     sync.Mutex
	active int
	ctx    context.Context
	cancel context.CancelFunc
}

// Start binds 127.0.0.1:0 and begins accepting. It returns the bound address.
func (f *Forwarder) Start() (string, error) {
	if f.Dialer == nil || f.Target == "" {
		return "", errors.New("forward: Dialer and Target are required")
	}
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return "", err
	}
	f.ln = ln
	f.ctx, f.cancel = context.WithCancel(context.Background())
	f.wg.Add(1)
	go f.acceptLoop()
	return ln.Addr().String(), nil
}

// Close stops listening and tears down all active connections.
func (f *Forwarder) Close() error {
	if f.ln == nil {
		return nil
	}
	f.cancel()
	err := f.ln.Close()
	f.wg.Wait()
	return err
}

func (f *Forwarder) acceptLoop() {
	defer f.wg.Done()
	for {
		c, err := f.ln.Accept()
		if err != nil {
			return
		}
		if !f.acquire() {
			_ = c.Close()
			continue
		}
		f.accepted++
		if f.OneShot || (f.MaxAccepts > 0 && f.accepted >= f.MaxAccepts) {
			_ = f.ln.Close()
		}
		f.wg.Add(1)
		go func() {
			defer f.wg.Done()
			defer f.release()
			f.handle(c)
		}()
	}
}

// acquire takes a connection slot, waiting briefly for one to free up: an RDP client that
// reconnects closes its previous connection only a moment before opening the next one.
func (f *Forwarder) acquire() bool {
	deadline := time.Now().Add(3 * time.Second)
	for {
		f.mu.Lock()
		if f.MaxConns <= 0 || f.active < f.MaxConns {
			f.active++
			f.mu.Unlock()
			return true
		}
		f.mu.Unlock()
		if time.Now().After(deadline) || f.ctx.Err() != nil {
			return false
		}
		time.Sleep(50 * time.Millisecond)
	}
}

func (f *Forwarder) release() {
	f.mu.Lock()
	f.active--
	f.mu.Unlock()
}

func (f *Forwarder) handle(client net.Conn) {
	defer client.Close()
	remote, err := f.Dialer.DialContext(f.ctx, "tcp", f.Target)
	if err != nil {
		return
	}
	defer remote.Close()

	// Close both sides when the forwarder is shut down.
	stop := context.AfterFunc(f.ctx, func() {
		_ = client.Close()
		_ = remote.Close()
	})
	defer stop()

	done := make(chan struct{}, 2)
	pipe := func(dst, src net.Conn) {
		_, _ = io.Copy(dst, src)
		if cw, ok := dst.(interface{ CloseWrite() error }); ok {
			_ = cw.CloseWrite()
		}
		done <- struct{}{}
	}
	go pipe(remote, client)
	go pipe(client, remote)
	<-done
	<-done
}
