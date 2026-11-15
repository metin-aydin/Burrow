package gp

import (
	"bytes"
	"errors"
	"io"
	"os"
	"testing"
	"time"

	"golang.org/x/sys/windows"
)

// openconnect's end does overlapped ReadFile/WriteFile, one IP packet per call: every write must come
// out as exactly one read on the other side, in both directions, and Close must unblock a read.
func TestTunWirePreservesPackets(t *testing.T) {
	end, wire, err := newTunWire()
	if err != nil {
		t.Fatal(err)
	}
	oc := os.NewFile(end.handle, "openconnect-end")
	defer oc.Close()

	packets := [][]byte{bytes.Repeat([]byte{0x45}, 1400), {0x45, 1, 2}, bytes.Repeat([]byte{0x60}, 60)}
	go func() {
		for _, p := range packets {
			if _, err := oc.Write(p); err != nil {
				t.Error(err)
				return
			}
		}
	}()
	buf := make([]byte, 2000)
	for i, want := range packets {
		n, err := wire.Read(buf)
		if err != nil {
			t.Fatal(err)
		}
		if !bytes.Equal(buf[:n], want) {
			t.Fatalf("packet %d: got %d bytes, want %d", i, n, len(want))
		}
	}
	for i, p := range packets {
		if _, err := wire.Write(p); err != nil {
			t.Fatal(err)
		}
		n, err := oc.Read(buf)
		if err != nil || !bytes.Equal(buf[:n], p) {
			t.Fatalf("reverse packet %d: got %d bytes (%v), want %d", i, n, err, len(p))
		}
	}

	done := make(chan error, 1)
	go func() { _, err := wire.Read(buf); done <- err }()
	time.Sleep(50 * time.Millisecond)
	wire.Close()
	select {
	case err := <-done:
		if !errors.Is(err, os.ErrClosed) && !errors.Is(err, io.EOF) {
			t.Fatalf("read after close: %v", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("Close didn't unblock the read")
	}
}

// Nobody else can open the pipe: it has a single instance, already taken by openconnect's end.
func TestTunWireSingleClient(t *testing.T) {
	end, wire, err := newTunWire()
	if err != nil {
		t.Fatal(err)
	}
	defer wire.Close()
	defer end.close()
	// The pipe's name is random and never leaves newTunWire; check the limits on the server handle.
	var flags, outSize, inSize, maxInstances uint32
	h := windows.Handle(wire.(*os.File).Fd())
	if err := windows.GetNamedPipeInfo(h, &flags, &outSize, &inSize, &maxInstances); err != nil {
		t.Fatal(err)
	}
	if maxInstances != 1 || flags&windows.PIPE_TYPE_MESSAGE == 0 {
		t.Fatalf("pipe: maxInstances %d, flags %#x", maxInstances, flags)
	}
}
