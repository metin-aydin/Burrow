package netstack

import (
	"context"
	"io"
	"net/netip"
	"syscall"
	"testing"
	"time"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/adapters/gonet"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
)

// Two stacks wired back to back through a datagram socketpair stand in for "Burrow" and the
// remote network: a TCP echo round trip proves packets flow both ways.
func TestDialThroughPacketSocket(t *testing.T)   { testDial(t, Options{}) }
func TestDialWithAFPrefixFraming(t *testing.T) { testDial(t, Options{AFPrefix: true}) }

func testDial(t *testing.T, opt Options) {
	fds, err := syscall.Socketpair(syscall.AF_UNIX, syscall.SOCK_DGRAM, 0)
	if err != nil {
		t.Fatal(err)
	}
	client, err := NewWithOptions(fds[0], 1400, netip.MustParseAddr("10.9.0.2"), nil, opt)
	if err != nil {
		t.Fatal(err)
	}
	defer client.Close()
	server, err := NewWithOptions(fds[1], 1400, netip.MustParseAddr("10.9.0.1"), nil, opt)
	if err != nil {
		t.Fatal(err)
	}
	defer server.Close()

	ln, err := gonet.ListenTCP(server.s, tcpip.FullAddress{NIC: nicID, Port: 3389}, ipv4.ProtocolNumber)
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	go func() {
		c, err := ln.Accept()
		if err != nil {
			return
		}
		defer c.Close()
		io.Copy(c, c)
	}()

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	conn, err := client.DialContext(ctx, "tcp", "10.9.0.1:3389")
	if err != nil {
		t.Fatal(err)
	}
	defer conn.Close()
	msg := make([]byte, 64*1024) // larger than one packet
	for i := range msg {
		msg[i] = byte(i)
	}
	go conn.Write(msg)
	got := make([]byte, len(msg))
	conn.SetReadDeadline(time.Now().Add(10 * time.Second))
	if _, err := io.ReadFull(conn, got); err != nil {
		t.Fatal(err)
	}
	for i := range got {
		if got[i] != msg[i] {
			t.Fatalf("byte %d differs", i)
		}
	}
	if _, err := client.DialContext(ctx, "tcp", "no-dns.example:1"); err == nil {
		t.Fatal("resolved a name without DNS servers")
	}
	// Out-of-range ports must fail instead of wrapping around to another port (70000 would become 4464).
	for _, addr := range []string{"10.9.0.1:70000", "10.9.0.1:-1", "10.9.0.1:rdp"} {
		if _, err := client.DialContext(ctx, "tcp", addr); err == nil {
			t.Fatalf("dialled %s", addr)
		}
	}
}
