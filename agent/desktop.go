package agent

import (
	"context"
	"errors"
	"os/exec"
	"os/user"
	"strings"
	"time"
)

// Desktop is what the agent does to the graphical session.
type Desktop interface {
	Unlock() error
	Lock() error
	// SetMonitorOff turns the physical monitor off (true) or back on.
	SetMonitorOff(off bool) error
	MonitorOff() (bool, error)
}

// GNOME drives the user's graphical session with loginctl (lock state) and Mutter's DisplayConfig
// PowerSaveMode over the session bus, as the user who owns the session (no root, ADR 0003).
type GNOME struct{}

func run(name string, args ...string) (string, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	out, err := exec.CommandContext(ctx, name, args...).CombinedOutput()
	if err != nil {
		return string(out), errors.New(name + ": " + strings.TrimSpace(string(out)))
	}
	return string(out), nil
}

// session is the user's graphical session (the one Desktop Sharing shows).
func session() (string, error) {
	u, err := user.Current()
	if err != nil {
		return "", err
	}
	out, err := run("loginctl", "show-user", u.Username, "-p", "Display", "--value")
	if err != nil {
		return "", err
	}
	id := strings.TrimSpace(out)
	if id == "" {
		return "", errors.New("no graphical session is open")
	}
	return id, nil
}

func (GNOME) Unlock() error {
	id, err := session()
	if err != nil {
		return err
	}
	_, err = run("loginctl", "unlock-session", id)
	return err
}

func (GNOME) Lock() error {
	id, err := session()
	if err != nil {
		return err
	}
	_, err = run("loginctl", "lock-session", id)
	return err
}

const displayConfig = "org.gnome.Mutter.DisplayConfig"

func (GNOME) SetMonitorOff(off bool) error {
	mode := "<int32 0>"
	if off {
		mode = "<int32 3>"
	}
	_, err := run("gdbus", "call", "--session", "--dest", displayConfig, "--object-path", "/org/gnome/Mutter/DisplayConfig",
		"--method", "org.freedesktop.DBus.Properties.Set", displayConfig, "PowerSaveMode", mode)
	return err
}

func (GNOME) MonitorOff() (bool, error) {
	out, err := run("gdbus", "call", "--session", "--dest", displayConfig, "--object-path", "/org/gnome/Mutter/DisplayConfig",
		"--method", "org.freedesktop.DBus.Properties.Get", displayConfig, "PowerSaveMode")
	if err != nil {
		return false, err
	}
	// "(<3>,)" when off, "(<0>,)" when on.
	return !strings.Contains(out, "<0>"), nil
}
