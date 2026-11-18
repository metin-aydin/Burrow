package agent

import (
	"io"
	"log"
	"net"
	"strings"
	"sync"
	"testing"
	"time"
)

type fakeDesktop struct {
	mu         sync.Mutex
	locked     bool
	monitorOff bool
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

func TestVerifier(t *testing.T) {
	key, _ := NewKey()
	other, _ := NewKey()
	now := time.Unix(1_800_000_000, 0)
	v := NewVerifier(key)

	r := NewRequest(key, CmdStart, now)
	if err := v.Check(r, now); err != nil {
		t.Fatalf("valid request rejected: %v", err)
	}
	if err := v.Check(r, now.Add(time.Second)); err != ErrReplayed {
		t.Fatalf("replay not caught: %v", err)
	}
	if err := v.Check(NewRequest(other, CmdStart, now), now); err != ErrBadMAC {
		t.Fatalf("wrong key accepted: %v", err)
	}
	if err := v.Check(NewRequest(key, CmdStart, now.Add(-2*time.Minute)), now); err != ErrStale {
		t.Fatalf("old request accepted: %v", err)
	}
	tampered := NewRequest(key, CmdBeat, now)
	tampered.Cmd = CmdStart // signed as beat, sent as start
	if err := v.Check(tampered, now); err != ErrBadMAC {
		t.Fatalf("tampered command accepted: %v", err)
	}
	bad := NewRequest(key, "shell", now)
	if err := v.Check(bad, now); err != ErrBadCmd {
		t.Fatalf("unknown command accepted: %v", err)
	}
	// A reply MAC never verifies as a request MAC (domain separation).
	if ReplyMAC(key, CmdStart, r.Nonce, true) == RequestMAC(key, CmdStart, r.TS, r.Nonce) {
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
