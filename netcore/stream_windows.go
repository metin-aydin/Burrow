package main

import (
	"errors"
	"io"
	"net"
	"sync"
	"time"

	"golang.org/x/sys/windows"
)

// adoptStream takes ownership of a connected Winsock stream socket (one end of the app's loopback
// pair, ADR 0010) as a net.Conn. net.FileConn doesn't exist on Windows, so it is driven with
// blocking recv/send from the copying goroutines.
func adoptStream(fd int) (net.Conn, error) {
	h := windows.Handle(fd)
	if h == windows.InvalidHandle || h == 0 {
		return nil, errors.New("invalid socket")
	}
	return &winsockConn{h: h}, nil
}

func closeStream(fd int) { _ = windows.Closesocket(windows.Handle(fd)) }

type winsockConn struct {
	h    windows.Handle
	once sync.Once
}

func (c *winsockConn) Read(p []byte) (int, error) {
	if len(p) == 0 {
		return 0, nil
	}
	buf := windows.WSABuf{Len: uint32(min(len(p), 1<<30)), Buf: &p[0]}
	var n, flags uint32
	if err := windows.WSARecv(c.h, &buf, 1, &n, &flags, nil, nil); err != nil {
		return 0, err
	}
	if n == 0 {
		return 0, io.EOF
	}
	return int(n), nil
}

func (c *winsockConn) Write(p []byte) (int, error) {
	total := 0
	for total < len(p) {
		chunk := p[total:]
		buf := windows.WSABuf{Len: uint32(min(len(chunk), 1<<30)), Buf: &chunk[0]}
		var n uint32
		if err := windows.WSASend(c.h, &buf, 1, &n, 0, nil, nil); err != nil {
			return total, err
		}
		total += int(n)
	}
	return total, nil
}

func (c *winsockConn) CloseWrite() error { return windows.Shutdown(c.h, windows.SHUT_WR) }

func (c *winsockConn) Close() error {
	var err error
	c.once.Do(func() { err = windows.Closesocket(c.h) })
	return err
}

func (c *winsockConn) LocalAddr() net.Addr              { return pipeAddr{} }
func (c *winsockConn) RemoteAddr() net.Addr             { return pipeAddr{} }
func (c *winsockConn) SetDeadline(time.Time) error      { return errors.ErrUnsupported }
func (c *winsockConn) SetReadDeadline(time.Time) error  { return errors.ErrUnsupported }
func (c *winsockConn) SetWriteDeadline(time.Time) error { return errors.ErrUnsupported }

type pipeAddr struct{}

func (pipeAddr) Network() string { return "app" }
func (pipeAddr) String() string  { return "burrow" }
