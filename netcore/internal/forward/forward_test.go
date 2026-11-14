package forward

import (
	"bufio"
	"net"
	"testing"
	"time"
)

// echoServer stands in for the remote RDP host.
func echoServer(t *testing.T) string {
	t.Helper()
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { ln.Close() })
	go func() {
		for {
			c, err := ln.Accept()
			if err != nil {
				return
			}
			go func() {
				defer c.Close()
				buf := make([]byte, 4096)
				for {
					n, err := c.Read(buf)
					if err != nil {
						return
					}
					c.Write(buf[:n])
				}
			}()
		}
	}()
	return ln.Addr().String()
}

func TestForwarderRoundTrip(t *testing.T) {
	target := echoServer(t)
	f := &Forwarder{Dialer: &net.Dialer{}, Target: target}
	addr, err := f.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()

	c, err := net.DialTimeout("tcp", addr, 2*time.Second)
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	c.SetDeadline(time.Now().Add(2 * time.Second))

	if _, err := c.Write([]byte("hello rdp\n")); err != nil {
		t.Fatal(err)
	}
	line, err := bufio.NewReader(c).ReadString('\n')
	if err != nil {
		t.Fatal(err)
	}
	if line != "hello rdp\n" {
		t.Fatalf("got %q", line)
	}
}

func TestForwarderOnlyLoopback(t *testing.T) {
	f := &Forwarder{Dialer: &net.Dialer{}, Target: "127.0.0.1:1"}
	addr, err := f.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	host, _, _ := net.SplitHostPort(addr)
	if host != "127.0.0.1" {
		t.Fatalf("listener must bind loopback only, got %s", host)
	}
}

func TestForwarderOneShot(t *testing.T) {
	target := echoServer(t)
	f := &Forwarder{Dialer: &net.Dialer{}, Target: target, OneShot: true}
	addr, err := f.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()

	c1, err := net.Dial("tcp", addr)
	if err != nil {
		t.Fatal(err)
	}
	defer c1.Close()
	c1.Write([]byte("x"))
	c1.SetReadDeadline(time.Now().Add(2 * time.Second))
	if _, err := c1.Read(make([]byte, 1)); err != nil {
		t.Fatalf("first conn should work: %v", err)
	}
	if c2, err := net.DialTimeout("tcp", addr, time.Second); err == nil {
		c2.Close()
		t.Fatal("listener should be closed after the first connection")
	}
}

func TestForwarderMaxConns(t *testing.T) {
	target := echoServer(t)
	f := &Forwarder{Dialer: &net.Dialer{}, Target: target, MaxConns: 1}
	addr, err := f.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()

	c1, err := net.Dial("tcp", addr)
	if err != nil {
		t.Fatal(err)
	}
	defer c1.Close()
	c1.Write([]byte("x"))
	c1.SetReadDeadline(time.Now().Add(2 * time.Second))
	if _, err := c1.Read(make([]byte, 1)); err != nil {
		t.Fatalf("first conn should work: %v", err)
	}

	c2, err := net.Dial("tcp", addr)
	if err != nil {
		t.Fatal(err)
	}
	defer c2.Close()
	c2.SetReadDeadline(time.Now().Add(2 * time.Second))
	if _, err := c2.Read(make([]byte, 1)); err == nil {
		t.Fatal("second conn should be rejected")
	}
}

func TestMaxAccepts(t *testing.T) {
	echo := echoServer(t)
	f := &Forwarder{Dialer: &net.Dialer{}, Target: echo, MaxConns: 1, MaxAccepts: 2}
	addr, err := f.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	for i := 0; i < 2; i++ {
		c, err := net.Dial("tcp", addr)
		if err != nil {
			t.Fatalf("connection %d refused: %v", i+1, err)
		}
		c.Write([]byte("x"))
		buf := make([]byte, 1)
		c.SetReadDeadline(time.Now().Add(2 * time.Second))
		if _, err := c.Read(buf); err != nil {
			t.Fatalf("connection %d: %v", i+1, err)
		}
		c.Close()
	}
	time.Sleep(50 * time.Millisecond)
	if c, err := net.DialTimeout("tcp", addr, time.Second); err == nil {
		c.Close()
		t.Fatal("third connection accepted")
	}
}
