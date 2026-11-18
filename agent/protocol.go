// Package agent is burrow-agent (ADR 0003): a small user service on an Ubuntu desktop that unlocks the
// screen before Burrow shares it over GNOME's Desktop Sharing, keeps the office monitor off meanwhile and
// locks the screen again when Burrow leaves or goes quiet.
//
// Wire format, one exchange per TCP connection, each message one JSON line: the agent sends a random
// challenge, Burrow sends a command signed together with that challenge (HMAC-SHA256 with the key made at
// pairing), the agent replies, signed the same way. A recorded command is useless on any later connection,
// even after the agent restarts, and Burrow knows it talks to its own agent.
package agent

import (
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base32"
	"encoding/hex"
	"errors"
	"strconv"
	"strings"
	"sync"
	"time"
)

const (
	KeySize     = 32
	DefaultPort = 47391
)

// Commands Burrow sends.
const (
	CmdStart = "start" // unlock, turn the monitor off, start the lease
	CmdBeat  = "beat"  // extend the lease
	CmdEnd   = "end"   // lock, turn the monitor on
)

// Challenge is the agent's first line on every connection.
type Challenge struct {
	V         int    `json:"v"`
	Challenge string `json:"challenge"`
}

type Request struct {
	V   int    `json:"v"`
	Cmd string `json:"cmd"`
	MAC string `json:"mac"`
}

type Reply struct {
	OK    bool   `json:"ok"`
	Error string `json:"error,omitempty"`
	MAC   string `json:"mac"`
}

var b32 = base32.StdEncoding.WithPadding(base32.NoPadding)

// NewKey makes a pairing key.
func NewKey() ([]byte, error) {
	k := make([]byte, KeySize)
	_, err := rand.Read(k)
	return k, err
}

// PairingCode shows a key the way people type it into Burrow: base32 in groups of four.
func PairingCode(key []byte) string {
	s := b32.EncodeToString(key)
	var groups []string
	for len(s) > 4 {
		groups = append(groups, s[:4])
		s = s[4:]
	}
	return strings.Join(append(groups, s), "-")
}

// ParsePairingCode reads a code back, ignoring spaces, dashes and case.
func ParsePairingCode(code string) ([]byte, error) {
	clean := strings.Map(func(r rune) rune {
		if r == ' ' || r == '-' || r == '\t' || r == '\n' || r == '\r' {
			return -1
		}
		return r
	}, strings.ToUpper(code))
	k, err := b32.DecodeString(clean)
	if err != nil || len(k) != KeySize {
		return nil, errors.New("not a burrow-agent pairing code")
	}
	return k, nil
}

func mac(key []byte, parts ...string) string {
	m := hmac.New(sha256.New, key)
	m.Write([]byte(strings.Join(parts, "\n")))
	return hex.EncodeToString(m.Sum(nil))
}

// RequestMAC and ReplyMAC bind a message to the connection's challenge, and are domain-separated so a
// reply can never pass as a request.
func RequestMAC(key []byte, cmd, challenge string) string {
	return mac(key, "burrow-agent/2 request", cmd, challenge)
}

func ReplyMAC(key []byte, cmd, challenge string, ok bool) string {
	return mac(key, "burrow-agent/2 reply", cmd, challenge, strconv.FormatBool(ok))
}

// NewChallenge makes a connection's challenge: 16 random bytes in hex.
func NewChallenge() string {
	n := make([]byte, 16)
	_, _ = rand.Read(n)
	return hex.EncodeToString(n)
}

// NewRequest builds a request signed for a challenge (Burrow's side; also used by tests).
func NewRequest(key []byte, cmd, challenge string) Request {
	return Request{V: 2, Cmd: cmd, MAC: RequestMAC(key, cmd, challenge)}
}

var (
	ErrBadMAC = errors.New("bad signature")
	ErrBadCmd = errors.New("unknown command")
)

// Check verifies a request against the challenge this connection was given.
func Check(key []byte, r Request, challenge string) error {
	if r.V != 2 || !hmac.Equal([]byte(r.MAC), []byte(RequestMAC(key, r.Cmd, challenge))) {
		return ErrBadMAC
	}
	switch r.Cmd {
	case CmdStart, CmdBeat, CmdEnd:
		return nil
	}
	return ErrBadCmd
}

// Limiter ignores an address after too many bad signatures in a short time.
type Limiter struct {
	mu      sync.Mutex
	fails   map[string][]time.Time
	blocked map[string]time.Time
	Max     int
	Window  time.Duration
	Block   time.Duration
}

func NewLimiter() *Limiter {
	return &Limiter{fails: map[string][]time.Time{}, blocked: map[string]time.Time{}, Max: 5, Window: time.Minute, Block: 5 * time.Minute}
}

func (l *Limiter) Blocked(addr string, now time.Time) bool {
	l.mu.Lock()
	defer l.mu.Unlock()
	until, ok := l.blocked[addr]
	if ok && now.After(until) {
		delete(l.blocked, addr)
		return false
	}
	return ok
}

func (l *Limiter) Fail(addr string, now time.Time) {
	l.mu.Lock()
	defer l.mu.Unlock()
	recent := l.fails[addr][:0]
	for _, t := range l.fails[addr] {
		if now.Sub(t) < l.Window {
			recent = append(recent, t)
		}
	}
	recent = append(recent, now)
	l.fails[addr] = recent
	if len(recent) >= l.Max {
		l.blocked[addr] = now.Add(l.Block)
		delete(l.fails, addr)
	}
}
