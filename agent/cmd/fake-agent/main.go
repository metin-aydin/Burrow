// fake-agent runs burrow-agent's protocol with a pretend desktop that only logs, for testing Burrow on a
// machine without GNOME: go run ./cmd/fake-agent [port]. It prints a fresh pairing code.
package main

import (
	"fmt"
	"log"
	"net"
	"os"

	"github.com/metin-aydin/Burrow/agent"
)

type logDesktop struct{ off bool }

func (d *logDesktop) Unlock() error { log.Print("desktop: unlock"); return nil }
func (d *logDesktop) Lock() error   { log.Print("desktop: lock"); return nil }
func (d *logDesktop) SetMonitorOff(off bool) error {
	d.off = off
	log.Printf("desktop: monitor off=%v", off)
	return nil
}
func (d *logDesktop) MonitorOff() (bool, error) { return d.off, nil }

func main() {
	port := "47391"
	if len(os.Args) > 1 {
		port = os.Args[1]
	}
	key, err := agent.NewKey()
	if err != nil {
		log.Fatal(err)
	}
	l, err := net.Listen("tcp", "127.0.0.1:"+port)
	if err != nil {
		log.Fatal(err)
	}
	fmt.Printf("pairing code: %s\n", agent.PairingCode(key))
	log.Fatal(agent.NewServer(key, &logDesktop{}, log.Default()).Serve(l))
}
