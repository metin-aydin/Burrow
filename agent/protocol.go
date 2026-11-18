// Package agent is burrow-agent (ADR 0003): a small user service on an Ubuntu desktop that unlocks the
// screen before Burrow shares it over GNOME's Desktop Sharing, keeps the office monitor off meanwhile and
// locks the screen again when Burrow leaves or goes quiet.
//
// Wire format: one JSON line per TCP connection, request then reply. Both carry an HMAC-SHA256 with the
// key made at pairing, so the agent only obeys Burrow and Burrow knows it talks to its agent.
package agent

import (
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base32"
	"encoding/hex"
	"errors"
	"fmt"
	"strconv"
	"strings"
	"sync"
	"time"
)

const (
	KeySize     = 32
	DefaultPort = 47391
	// MaxSkew is how far a request's clock may be from ours; NonceMemory outlives it, so a request
	// can't be replayed while its time stamp is still accepted.
	MaxSkew     = 60 * time.Second
	NonceMemory = 2 * MaxSkew
)

// Commands Burrow sends.
const (
	CmdStart = "start" // unlock, turn the monitor off, start the lease
	CmdBeat  = "beat"  // extend the lease
	CmdEnd   = "end"   // lock, turn the monitor on
)

type Request struct {
	V     int    `json:"v"`
	Cmd   string `json:"cmd"`
	TS    int64  `json:"ts"`
	Nonce string `json:"nonce"`
	MAC   string `json:"mac"`
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

// RequestMAC and ReplyMAC are domain-separated so a reply can never pass as a request.
func RequestMAC(key []byte, cmd string, ts int64, nonce string) string {
	return mac(key, "burrow-agent/1 request", cmd, strconv.FormatInt(ts, 10), nonce)
}

func ReplyMAC(key []byte, cmd, nonce string, ok bool) string {
	return mac(key, "burrow-agent/1 reply", cmd, nonce, strconv.FormatBool(ok))
}

// NewRequest builds a signed request (Burrow's side; also used by tests and `burrow-agent test`).
func NewRequest(key []byte, cmd string, now time.Time) Request {
	n := make([]byte, 16)
	_, _ = rand.Read(n)
	nonce := hex.EncodeToString(n)
	ts := now.Unix()
	return Request{V: 1, Cmd: cmd, TS: ts, Nonce: nonce, MAC: RequestMAC(key, cmd, ts, nonce)}
}

// Verifier checks requests: MAC, clock skew and nonce reuse.
type Verifier struct {
	key  []byte
	mu   sync.Mutex
	seen map[string]time.Time
}

func NewVerifier(key []byte) *Verifier { return &Verifier{key: key, seen: map[string]time.Time{}} }

var (
	ErrBadMAC   = errors.New("bad signature")
	ErrStale    = errors.New("request time is off by more than a minute")
	ErrReplayed = errors.New("request was already used")
	ErrBadCmd   = errors.New("unknown command")
)

func (v *Verifier) Check(r Request, now time.Time) error {
	if r.V != 1 || len(r.Nonce) != 32 {
		return ErrBadMAC
	}
	if !hmac.Equal([]byte(r.MAC), []byte(RequestMAC(v.key, r.Cmd, r.TS, r.Nonce))) {
		return ErrBadMAC
	}
	switch r.Cmd {
	case CmdStart, CmdBeat, CmdEnd:
	default:
		return ErrBadCmd
	}
	if d := now.Sub(time.Unix(r.TS, 0)); d > MaxSkew || d < -MaxSkew {
		return ErrStale
	}
	v.mu.Lock()
	defer v.mu.Unlock()
	for n, t := range v.seen {
		if now.Sub(t) > NonceMemory {
			delete(v.seen, n)
		}
	}
	if _, used := v.seen[r.Nonce]; used {
		return ErrReplayed
	}
	v.seen[r.Nonce] = now
	return nil
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

func (r Request) String() string { return fmt.Sprintf("%s (ts %d)", r.Cmd, r.TS) }
