package agent

import (
	"bufio"
	"encoding/json"
	"errors"
	"io"
	"log"
	"net"
	"os"
	"sync"
	"time"
)

// Server keeps the session state: while Burrow holds a lease the screen stays unlocked and the monitor
// off; when Burrow says end, or stops sending beats, the agent locks the screen and turns the monitor on.
// It fails closed: a lock that didn't work is retried (monitor kept off meanwhile), stopping the agent
// locks an open session, and an agent that died mid-session locks the screen when it starts again.
type Server struct {
	Key     []byte
	Desktop Desktop
	Lease   time.Duration // how long a start or beat keeps the screen unlocked (Burrow beats every 5 s)
	Now     func() time.Time
	Log     *log.Logger
	// StateFile marks a session in progress on disk, so a restart after a crash locks the screen.
	StateFile string

	limiter     *Limiter
	mu          sync.Mutex
	active      bool
	lockPending bool // the session ended but locking failed: retried on every tick
	until       time.Time
}

func NewServer(key []byte, d Desktop, logger *log.Logger) *Server {
	return &Server{Key: key, Desktop: d, Lease: 20 * time.Second, Now: time.Now, Log: logger, limiter: NewLimiter()}
}

// Recover locks the screen if the agent stopped during a session (the state file is still there).
func (s *Server) Recover() {
	if s.StateFile == "" {
		return
	}
	if _, err := os.Stat(s.StateFile); err != nil {
		return
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	s.endLocked("the agent restarted during a session")
}

func (s *Server) markActive(on bool) {
	if s.StateFile == "" {
		return
	}
	if on {
		_ = os.WriteFile(s.StateFile, []byte("session\n"), 0o600)
	} else {
		_ = os.Remove(s.StateFile)
	}
}

// Handle runs a verified command.
func (s *Server) Handle(cmd string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	now := s.Now()
	switch cmd {
	case CmdStart:
		if err := s.Desktop.Unlock(); err != nil {
			return err
		}
		if err := s.Desktop.SetMonitorOff(true); err != nil {
			s.Log.Printf("couldn't turn the monitor off: %v", err)
		}
		s.active, s.lockPending, s.until = true, false, now.Add(s.Lease)
		s.markActive(true)
		s.Log.Printf("session started: screen unlocked")
	case CmdBeat:
		// After an agent restart a beat picks the session up again (the screen is already shared, so it
		// is unlocked); it never unlocks by itself.
		if !s.active {
			s.Log.Printf("beat without a session: taking it over")
			s.markActive(true)
		}
		s.active, s.lockPending, s.until = true, false, now.Add(s.Lease)
	case CmdEnd:
		s.endLocked("Burrow disconnected")
	}
	return nil
}

// endLocked locks the screen; only once that worked is the monitor turned back on and the session over.
func (s *Server) endLocked(why string) {
	s.active = false
	if err := s.Desktop.Lock(); err != nil {
		if !s.lockPending {
			s.Log.Printf("couldn't lock the screen (%s): %v; retrying, monitor stays off", why, err)
		}
		s.lockPending = true
		_ = s.Desktop.SetMonitorOff(true)
		return
	}
	s.lockPending = false
	s.markActive(false)
	if err := s.Desktop.SetMonitorOff(false); err != nil {
		s.Log.Printf("couldn't turn the monitor on: %v", err)
	}
	s.Log.Printf("screen locked (%s)", why)
}

// Tick ends a lease that ran out, retries a failed lock and keeps the monitor off during a session.
func (s *Server) Tick() {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.lockPending {
		s.endLocked("retry")
		return
	}
	if !s.active {
		return
	}
	if s.Now().After(s.until) {
		s.endLocked("Burrow stopped answering")
		return
	}
	// Someone at the office moved the mouse: the monitor woke up, turn it off again.
	if off, err := s.Desktop.MonitorOff(); err == nil && !off {
		if err := s.Desktop.SetMonitorOff(true); err == nil {
			s.Log.Printf("monitor woke up during the session; turned it off again")
		}
	}
}

// Shutdown locks the screen if a session is open (the agent is being stopped).
func (s *Server) Shutdown() {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.active || s.lockPending {
		s.endLocked("the agent is stopping")
	}
}

func (s *Server) Active() bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.active
}

// Serve answers requests on l until it is closed, and ticks the lease meanwhile. On return an open
// session is locked.
func (s *Server) Serve(l net.Listener) error {
	stop := make(chan struct{})
	defer func() {
		close(stop)
		s.Shutdown()
	}()
	go func() {
		t := time.NewTicker(2 * time.Second)
		defer t.Stop()
		for {
			select {
			case <-stop:
				return
			case <-t.C:
				s.Tick()
			}
		}
	}()
	for {
		c, err := l.Accept()
		if err != nil {
			return err
		}
		go s.serveConn(c)
	}
}

func (s *Server) serveConn(c net.Conn) {
	defer c.Close()
	host, _, _ := net.SplitHostPort(c.RemoteAddr().String())
	now := s.Now()
	if s.limiter.Blocked(host, now) {
		return
	}
	_ = c.SetDeadline(now.Add(10 * time.Second))
	challenge := NewChallenge()
	b, _ := json.Marshal(Challenge{V: 2, Challenge: challenge})
	if _, err := c.Write(append(b, '\n')); err != nil {
		return
	}
	line, err := bufio.NewReader(io.LimitReader(c, 2048)).ReadBytes('\n')
	if err != nil {
		return
	}
	var req Request
	if json.Unmarshal(line, &req) != nil {
		s.limiter.Fail(host, now)
		return
	}
	if err := Check(s.Key, req, challenge); err != nil {
		if errors.Is(err, ErrBadMAC) {
			s.limiter.Fail(host, now)
			s.Log.Printf("rejected a request from %s: %v", host, err)
			return // no reply: an unauthenticated peer learns nothing
		}
		s.reply(c, req.Cmd, challenge, err)
		return
	}
	s.reply(c, req.Cmd, challenge, s.Handle(req.Cmd))
}

func (s *Server) reply(c net.Conn, cmd, challenge string, err error) {
	r := Reply{OK: err == nil}
	if err != nil {
		r.Error = err.Error()
	}
	r.MAC = ReplyMAC(s.Key, cmd, challenge, r.OK)
	b, _ := json.Marshal(r)
	_, _ = c.Write(append(b, '\n'))
}
