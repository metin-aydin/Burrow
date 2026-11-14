// Package tunnel defines what every in-app tunnel (Tailscale, GlobalProtect) offers to the rest of
// netcore: a way to dial TCP through it and a status for the UI. Nothing here touches the system
// network configuration; tunnels live entirely inside Burrow's process (ADR 0001).
package tunnel

import (
	"context"
	"net"
	"sync"
)

type State string

const (
	Starting   State = "starting"
	NeedsLogin State = "needs_login" // Tailscale: open AuthURL in a browser
	NeedsOTP   State = "needs_otp"   // GlobalProtect with manual OTP: SubmitOTP
	Connected  State = "connected"
	Failed     State = "failed"
	Stopped    State = "stopped"
)

// Status is reported to the UI as JSON.
type Status struct {
	State   State  `json:"state"`
	AuthURL string `json:"authUrl,omitempty"`
	Error   string `json:"error,omitempty"`
	Address string `json:"address,omitempty"` // this side's address inside the tunnel
	Prompt  string `json:"prompt,omitempty"`  // label of a pending OTP prompt
}

// Tunnel is a userspace network path to a private network.
type Tunnel interface {
	DialContext(ctx context.Context, network, address string) (net.Conn, error)
	Status() Status
	Close() error
}

// OTPReceiver is implemented by tunnels that may ask for a one-time password.
type OTPReceiver interface {
	SubmitOTP(code string)
}

// BlobStore keeps small encrypted blobs (the vault, ADR 0004).
type BlobStore interface {
	Blob(name string) ([]byte, error)
	PutBlob(name string, data []byte) error
}

// StatusBox is a mutex-protected Status.
type StatusBox struct {
	mu sync.Mutex
	s  Status
}

func (b *StatusBox) Get() Status {
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.s
}

func (b *StatusBox) Set(s Status) {
	b.mu.Lock()
	b.s = s
	b.mu.Unlock()
}

func (b *StatusBox) Update(fn func(*Status)) {
	b.mu.Lock()
	fn(&b.s)
	b.mu.Unlock()
}
