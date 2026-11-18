package agent

import (
	"bufio"
	"encoding/json"
	"errors"
	"log"
	"net"
	"sync"
	"time"
)

// Server keeps the session state: while Burrow holds a lease the screen stays unlocked and the monitor
// off; when Burrow says end, or stops sending beats, the agent locks the screen and turns the monitor on.
type Server struct {
	Key     []byte
	Desktop Desktop
	Lease   time.Duration // how long a start or beat keeps the screen unlocked (Burrow beats every 5 s)
	Now     func() time.Time
	Log     *log.Logger

	verifier *Verifier
	limiter  *Limiter
	mu       sync.Mutex
	active   bool
	until    time.Time
}

func NewServer(key []byte, d Desktop, logger *log.Logger) *Server {
	return &Server{Key: key, Desktop: d, Lease: 20 * time.Second, Now: time.Now, Log: logger,
		verifier: NewVerifier(key), limiter: NewLimiter()}
}

var errNotStarted = errors.New("not started")

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
		s.active, s.until = true, now.Add(s.Lease)
		s.Log.Printf("session started: screen unlocked")
	case CmdBeat:
		// After an agent restart a beat picks the session up again (the screen is already shared, so it
		// is unlocked); it never unlocks by itself.
		if !s.active {
			s.Log.Printf("beat without a session: taking it over")
		}
		s.active, s.until = true, now.Add(s.Lease)
	case CmdEnd:
		s.endLocked("Burrow disconnected")
	}
	return nil
}

func (s *Server) endLocked(why string) {
	s.active = false
	if err := s.Desktop.SetMonitorOff(false); err != nil {
		s.Log.Printf("couldn't turn the monitor on: %v", err)
	}
	if err := s.Desktop.Lock(); err != nil {
		s.Log.Printf("couldn't lock the screen: %v", err)
		return
	}
	s.Log.Printf("screen locked (%s)", why)
}

// Tick ends a lease that ran out and keeps the monitor off during a session. Called every second or two.
func (s *Server) Tick() {
	s.mu.Lock()
	defer s.mu.Unlock()
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

func (s *Server) Active() bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.active
}

// Serve answers requests on l until it is closed, and ticks the lease meanwhile.
func (s *Server) Serve(l net.Listener) error {
	stop := make(chan struct{})
	defer close(stop)
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
	line, err := bufio.NewReader(&limitedReader{c, 2048}).ReadBytes('\n')
	if err != nil {
		return
	}
	var req Request
	if json.Unmarshal(line, &req) != nil {
		s.limiter.Fail(host, now)
		return
	}
	if err := s.verifier.Check(req, now); err != nil {
		if errors.Is(err, ErrBadMAC) {
			s.limiter.Fail(host, now)
			s.Log.Printf("rejected a request from %s: %v", host, err)
			return // no reply: an unauthenticated peer learns nothing
		}
		s.reply(c, req, err)
		return
	}
	s.reply(c, req, s.Handle(req.Cmd))
}

func (s *Server) reply(c net.Conn, req Request, err error) {
	r := Reply{OK: err == nil}
	if err != nil {
		r.Error = err.Error()
	}
	r.MAC = ReplyMAC(s.Key, req.Cmd, req.Nonce, r.OK)
	b, _ := json.Marshal(r)
	_, _ = c.Write(append(b, '\n'))
}

type limitedReader struct {
	c net.Conn
	n int
}

func (l *limitedReader) Read(p []byte) (int, error) {
	if l.n <= 0 {
		return 0, errors.New("request too long")
	}
	if len(p) > l.n {
		p = p[:l.n]
	}
	n, err := l.c.Read(p)
	l.n -= n
	return n, err
}
