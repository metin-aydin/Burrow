package agent

import (
	"errors"
	"io"
	"log"
	"net"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"
)

type fakeDesktop struct {
	mu         sync.Mutex
	locked     bool
	monitorOff bool
	failLock   bool
	calls      []string
}

func (f *fakeDesktop) record(c string) { f.calls = append(f.calls, c) }
func (f *fakeDesktop) Unlock() error {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.locked = false
	f.record("unlock")
	return nil
}
func (f *fakeDesktop) Lock() error {
	f.mu.Lock()
	defer f.mu.Unlock()
	if f.failLock {
		return errors.New("lock failed")
	}
	f.locked = true
	f.record("lock")
	return nil
}
func (f *fakeDesktop) SetMonitorOff(off bool) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.monitorOff = off
	f.record(map[bool]string{true: "off", false: "on"}[off])
	return nil
}
func (f *fakeDesktop) MonitorOff() (bool, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.monitorOff, nil
}
func (f *fakeDesktop) state() (bool, bool) {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.locked, f.monitorOff
}

func quiet() *log.Logger { return log.New(io.Discard, "", 0) }

func TestPairingCodeRoundTrip(t *testing.T) {
	key, _ := NewKey()
	code := PairingCode(key)
	back, err := ParsePairingCode(strings.ToLower(strings.ReplaceAll(code, "-", " ")))
	if err != nil || string(back) != string(key) {
		t.Fatalf("round trip failed: %v", err)
	}
	if _, err := ParsePairingCode("ABCD-EFGH"); err == nil {
		t.Fatal("accepted a short code")
	}
}

func TestCheck(t *testing.T) {
	key, _ := NewKey()
	other, _ := NewKey()
	ch := NewChallenge()

	if err := Check(key, NewRequest(key, CmdStart, ch), ch); err != nil {
		t.Fatalf("valid request rejected: %v", err)
	}
	// A request recorded on one connection is worthless on the next (another challenge).
	if err := Check(key, NewRequest(key, CmdStart, ch), NewChallenge()); err != ErrBadMAC {
		t.Fatalf("replay on a new connection accepted: %v", err)
	}
	if err := Check(key, NewRequest(other, CmdStart, ch), ch); err != ErrBadMAC {
		t.Fatalf("wrong key accepted: %v", err)
	}
	tampered := NewRequest(key, CmdBeat, ch)
	tampered.Cmd = CmdStart // signed as beat, sent as start
	if err := Check(key, tampered, ch); err != ErrBadMAC {
		t.Fatalf("tampered command accepted: %v", err)
	}
	if err := Check(key, NewRequest(key, "shell", ch), ch); err != ErrBadCmd {
		t.Fatalf("unknown command accepted: %v", err)
	}
	if ReplyMAC(key, CmdStart, ch, true) == RequestMAC(key, CmdStart, ch) {
		t.Fatal("reply and request MACs coincide")
	}
}

func TestLimiter(t *testing.T) {
	l := NewLimiter()
	now := time.Unix(0, 0)
	for i := 0; i < 4; i++ {
		l.Fail("10.0.0.9", now)
	}
	if l.Blocked("10.0.0.9", now) {
		t.Fatal("blocked too early")
	}
	l.Fail("10.0.0.9", now)
	if !l.Blocked("10.0.0.9", now) || l.Blocked("10.0.0.8", now) {
		t.Fatal("wrong address blocked")
	}
	if l.Blocked("10.0.0.9", now.Add(6*time.Minute)) {
		t.Fatal("block never ends")
	}
}

func TestLeaseLifecycle(t *testing.T) {
	key, _ := NewKey()
	d := &fakeDesktop{locked: true}
	now := time.Unix(1_800_000_000, 0)
	s := NewServer(key, d, quiet())
	s.Now = func() time.Time { return now }

	if err := s.Handle(CmdStart); err != nil {
		t.Fatal(err)
	}
	if locked, off := d.state(); locked || !off {
		t.Fatalf("after start: locked=%v monitorOff=%v", locked, off)
	}
	// The monitor wakes up (someone moved the mouse): the next tick turns it off again.
	_ = d.SetMonitorOff(false)
	s.Tick()
	if _, off := d.state(); !off {
		t.Fatal("monitor left on during the session")
	}
	// Beats keep the session going past the original lease.
	now = now.Add(15 * time.Second)
	_ = s.Handle(CmdBeat)
	now = now.Add(15 * time.Second)
	s.Tick()
	if locked, _ := d.state(); locked {
		t.Fatal("locked although beats arrived")
	}
	// No beats for longer than the lease: locked, monitor back on.
	now = now.Add(21 * time.Second)
	s.Tick()
	if locked, off := d.state(); !locked || off || s.Active() {
		t.Fatalf("after silence: locked=%v monitorOff=%v active=%v", locked, off, s.Active())
	}
	// End locks right away.
	_ = s.Handle(CmdStart)
	_ = s.Handle(CmdEnd)
	if locked, off := d.state(); !locked || off || s.Active() {
		t.Fatal("end didn't lock")
	}
}

func TestFailsClosed(t *testing.T) {
	key, _ := NewKey()
	d := &fakeDesktop{locked: true}
	s := NewServer(key, d, quiet())
	s.StateFile = filepath.Join(t.TempDir(), "session")

	// A lock that fails is retried, and the monitor stays off until it works.
	_ = s.Handle(CmdStart)
	d.mu.Lock()
	d.failLock = true
	d.mu.Unlock()
	_ = s.Handle(CmdEnd)
	if locked, off := d.state(); locked || !off {
		t.Fatalf("after a failed lock: locked=%v monitorOff=%v", locked, off)
	}
	d.mu.Lock()
	d.failLock = false
	d.mu.Unlock()
	s.Tick()
	if locked, off := d.state(); !locked || off {
		t.Fatalf("lock not retried: locked=%v monitorOff=%v", locked, off)
	}

	// Stopping the agent during a session locks the screen.
	_ = s.Handle(CmdStart)
	s.Shutdown()
	if locked, _ := d.state(); !locked {
		t.Fatal("shutdown left the screen unlocked")
	}

	// An agent that died mid-session locks the screen when it starts again.
	_ = s.Handle(CmdStart)
	if _, err := os.Stat(s.StateFile); err != nil {
		t.Fatal("no state file during a session")
	}
	d2 := &fakeDesktop{}
	again := NewServer(key, d2, quiet())
	again.StateFile = s.StateFile
	again.Recover()
	if locked, _ := d2.state(); !locked {
		t.Fatal("restart after a crash didn't lock")
	}
	if _, err := os.Stat(s.StateFile); !os.IsNotExist(err) {
		t.Fatal("state file left behind after locking")
	}
}

func TestOverTheNetwork(t *testing.T) {
	key, _ := NewKey()
	d := &fakeDesktop{locked: true}
	s := NewServer(key, d, quiet())
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	go s.Serve(l)

	dial := func() net.Conn {
		c, err := net.Dial("tcp", l.Addr().String())
		if err != nil {
			t.Fatal(err)
		}
		return c
	}
	c := dial()
	if err := Send(c, key, CmdStart); err != nil {
		t.Fatalf("start: %v", err)
	}
	c.Close()
	if locked, _ := d.state(); locked {
		t.Fatal("not unlocked")
	}
	// A wrong key gets no answer and changes nothing.
	wrong, _ := NewKey()
	c = dial()
	if err := Send(c, wrong, CmdEnd); err == nil {
		t.Fatal("wrong key obeyed")
	}
	c.Close()
	if locked, _ := d.state(); locked {
		t.Fatal("wrong key locked the screen")
	}
	c = dial()
	if err := Send(c, key, CmdEnd); err != nil {
		t.Fatalf("end: %v", err)
	}
	c.Close()
	if locked, _ := d.state(); !locked {
		t.Fatal("not locked after end")
	}
}

func TestKeyRotationAndRemoval(t *testing.T) {
	oldKey, _ := NewKey()
	newKey, _ := NewKey()
	var mu sync.Mutex
	current, missing := oldKey, false
	d := &fakeDesktop{locked: true}
	s := NewServer(nil, d, quiet())
	s.KeyFunc = func() ([]byte, error) {
		mu.Lock()
		defer mu.Unlock()
		if missing {
			return nil, errors.New("no key file")
		}
		return current, nil
	}
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	go s.Serve(l)
	send := func(key []byte) error {
		c, err := net.Dial("tcp", l.Addr().String())
		if err != nil {
			t.Fatal(err)
		}
		defer c.Close()
		return Send(c, key, CmdBeat)
	}
	if err := send(oldKey); err != nil {
		t.Fatalf("old key before rotation: %v", err)
	}
	mu.Lock()
	current = newKey // `burrow-agent pair`
	mu.Unlock()
	if send(oldKey) == nil {
		t.Fatal("old code still works after a new one was made")
	}
	if err := send(newKey); err != nil {
		t.Fatalf("new key: %v", err)
	}
	mu.Lock()
	missing = true // `burrow-agent uninstall` removed the key, the service still runs
	mu.Unlock()
	if send(newKey) == nil {
		t.Fatal("obeyed without a key file")
	}
}
