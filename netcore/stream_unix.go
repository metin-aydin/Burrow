//go:build unix

package main

import (
	"net"
	"os"
)

// adoptStream takes ownership of a connected stream socket (one end of a socketpair) as a net.Conn.
func adoptStream(fd int) (net.Conn, error) {
	file := os.NewFile(uintptr(fd), "rdp")
	defer file.Close()
	return net.FileConn(file) // dups the descriptor
}

func closeStream(fd int) { _ = os.NewFile(uintptr(fd), "rdp").Close() }
