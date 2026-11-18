// burrow-agent unlocks this Ubuntu desktop for Burrow's remote sessions and locks it again afterwards
// (ADR 0003). Install it as your own user, not root:
//
//	burrow-agent install     copy itself to ~/.local/bin, make a pairing key, start the user service
//	burrow-agent pair        make a new pairing key (the old code stops working)
//	burrow-agent status      show whether the service runs and a session is active
//	burrow-agent uninstall   stop and remove the service and the key
//	burrow-agent run         the service itself (started by systemd)
package main

import (
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"

	"github.com/metin-aydin/Burrow/agent"
)

const unitName = "burrow-agent.service"

func configDir() string {
	if d := os.Getenv("XDG_CONFIG_HOME"); d != "" {
		return filepath.Join(d, "burrow-agent")
	}
	home, _ := os.UserHomeDir()
	return filepath.Join(home, ".config", "burrow-agent")
}

func keyPath() string { return filepath.Join(configDir(), "key") }

func readKey() ([]byte, error) {
	b, err := os.ReadFile(keyPath())
	if err != nil {
		return nil, errors.New("no pairing key yet; run `burrow-agent install`")
	}
	return agent.ParsePairingCode(string(b))
}

// writeKey makes a new key; the file is created only readable by this user.
func writeKey() ([]byte, error) {
	key, err := agent.NewKey()
	if err != nil {
		return nil, err
	}
	if err := os.MkdirAll(configDir(), 0o700); err != nil {
		return nil, err
	}
	tmp := keyPath() + ".new"
	_ = os.Remove(tmp)
	f, err := os.OpenFile(tmp, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o600)
	if err != nil {
		return nil, err
	}
	if _, err := io.WriteString(f, agent.PairingCode(key)+"\n"); err != nil {
		f.Close()
		return nil, err
	}
	if err := f.Close(); err != nil {
		return nil, err
	}
	return key, os.Rename(tmp, keyPath())
}

func systemctl(args ...string) error {
	out, err := exec.Command("systemctl", append([]string{"--user"}, args...)...).CombinedOutput()
	if err != nil {
		return fmt.Errorf("systemctl --user %s: %s", strings.Join(args, " "), strings.TrimSpace(string(out)))
	}
	return nil
}

func install(port int) error {
	if os.Geteuid() == 0 {
		return errors.New("run this as yourself, not root: the agent works in your own desktop session")
	}
	home, _ := os.UserHomeDir()
	bin := filepath.Join(home, ".local", "bin", "burrow-agent")
	self, err := os.Executable()
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(bin), 0o755); err != nil {
		return err
	}
	if self, _ = filepath.EvalSymlinks(self); self != bin {
		data, err := os.ReadFile(self)
		if err != nil {
			return err
		}
		if err := os.WriteFile(bin+".new", data, 0o755); err != nil {
			return err
		}
		if err := os.Rename(bin+".new", bin); err != nil {
			return err
		}
	}
	unitDir := filepath.Join(home, ".config", "systemd", "user")
	if err := os.MkdirAll(unitDir, 0o755); err != nil {
		return err
	}
	unit := fmt.Sprintf(`[Unit]
Description=Burrow agent: unlocks this desktop for Burrow's remote sessions and locks it afterwards
After=graphical-session.target

[Service]
ExecStart=%s run --port %d
Restart=on-failure

[Install]
WantedBy=default.target
`, bin, port)
	if err := os.WriteFile(filepath.Join(unitDir, unitName), []byte(unit), 0o644); err != nil {
		return err
	}
	key, err := readKey()
	if err != nil {
		if key, err = writeKey(); err != nil {
			return err
		}
	}
	if err := systemctl("daemon-reload"); err != nil {
		return err
	}
	if err := systemctl("enable", "--now", unitName); err != nil {
		return err
	}
	_ = systemctl("restart", unitName)
	fmt.Printf("burrow-agent is running and starts with your session (port %d).\n\n", port)
	printCode(key, port)
	return nil
}

func printCode(key []byte, port int) {
	fmt.Printf("In Burrow, edit this Ubuntu device → Session → Office Screen Helper, and enter:\n\n")
	fmt.Printf("  Pairing code: %s\n", agent.PairingCode(key))
	fmt.Printf("  Port:         %d\n\n", port)
	fmt.Printf("Keep the code private: with it and access to this network, someone could unlock this screen.\n")
}

// serve runs the service. Stopping it (systemd sends SIGTERM) closes the listener, and Serve then locks
// an open session; a session left open by a crash is locked when the service starts again.
func serve(port int) error {
	key, err := readKey()
	if err != nil {
		return err
	}
	logger := log.New(os.Stderr, "", 0)
	srv := agent.NewServer(key, agent.GNOME{}, logger)
	srv.StateFile = filepath.Join(configDir(), "session")
	srv.Recover()
	l, err := net.Listen("tcp", fmt.Sprintf(":%d", port))
	if err != nil {
		return err
	}
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGTERM, syscall.SIGINT)
	go func() {
		<-sig
		logger.Printf("stopping")
		l.Close()
	}()
	logger.Printf("listening on port %d", port)
	if err := srv.Serve(l); err != nil && !errors.Is(err, net.ErrClosed) {
		return err
	}
	return nil
}

func main() {
	log.SetFlags(0)
	if len(os.Args) < 2 {
		fmt.Fprintln(os.Stderr, "usage: burrow-agent install | pair | status | uninstall | run")
		os.Exit(2)
	}
	fs := flag.NewFlagSet(os.Args[1], flag.ExitOnError)
	port := fs.Int("port", agent.DefaultPort, "TCP port Burrow connects to")
	_ = fs.Parse(os.Args[2:])

	var err error
	switch os.Args[1] {
	case "run":
		err = serve(*port)
	case "install":
		err = install(*port)
	case "pair":
		var key []byte
		if key, err = writeKey(); err == nil {
			if systemctl("restart", unitName) != nil {
				fmt.Println("The service isn't installed yet; run `burrow-agent install`.")
			}
			fmt.Println("New pairing key made; the old code no longer works.")
			printCode(key, *port)
		}
	case "status":
		if systemctl("is-active", "--quiet", unitName) == nil {
			fmt.Println("burrow-agent is running. Recent events:")
			out, _ := exec.Command("journalctl", "--user", "-u", unitName, "-n", "10", "--no-pager", "-o", "cat").CombinedOutput()
			os.Stdout.Write(out)
		} else {
			fmt.Println("burrow-agent isn't running; run `burrow-agent install`.")
		}
	case "uninstall":
		_ = systemctl("disable", "--now", unitName)
		home, _ := os.UserHomeDir()
		_ = os.Remove(filepath.Join(home, ".config", "systemd", "user", unitName))
		_ = systemctl("daemon-reload")
		_ = os.Remove(keyPath())
		_ = os.Remove(filepath.Join(home, ".local", "bin", "burrow-agent"))
		fmt.Println("burrow-agent is removed.")
	default:
		err = fmt.Errorf("unknown command %q", os.Args[1])
	}
	if err != nil {
		log.Fatal("burrow-agent: ", err)
	}
}
