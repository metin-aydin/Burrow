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
