// Package ts runs Burrow's own Tailscale node with tsnet. The node key and state are kept in the
// encrypted vault (never as plain files), and Tailscale log upload is disabled.
package ts

import (
	"context"
	"encoding/json"
	"errors"
	"net"
	"os"
	"sort"
	"strings"
	"sync"
	"time"

	"tailscale.com/ipn"
	"tailscale.com/tsnet"

	"burrow/netcore/internal/tunnel"
)

type Tunnel struct {
	srv    *tsnet.Server
	status tunnel.StatusBox
	cancel context.CancelFunc
}

// Start brings the node up in the background; poll Status for NeedsLogin/AuthURL and Connected.
// dir holds only non-secret runtime files (logs are not uploaded).
func Start(id, hostname, controlURL, dir string, store tunnel.BlobStore) (*Tunnel, error) {
	if hostname == "" {
		hostname = "burrow"
	}
	// Opt out of Tailscale's log upload; Burrow's tunnels shouldn't phone home.
	os.Setenv("TS_NO_LOGS_NO_SUPPORT", "true")
	if err := os.MkdirAll(dir, 0o700); err != nil {
		return nil, err
	}
	t := &Tunnel{
		srv: &tsnet.Server{
			Hostname:   hostname,
			ControlURL: controlURL,
			Dir:        dir,
			Store:      &vaultStore{store: store, name: "tailscale-" + id},
			Logf:       func(string, ...any) {},
			UserLogf:   func(string, ...any) {},
		},
	}
	t.status.Set(tunnel.Status{State: tunnel.Starting, Step: "start"})
	ctx, cancel := context.WithCancel(context.Background())
	t.cancel = cancel
	go t.run(ctx)
	return t, nil
}

func (t *Tunnel) run(ctx context.Context) {
	if err := t.srv.Start(); err != nil {
		t.status.Set(tunnel.Status{State: tunnel.Failed, Error: err.Error()})
		return
	}
	lc, err := t.srv.LocalClient()
	if err != nil {
		t.status.Set(tunnel.Status{State: tunnel.Failed, Error: err.Error()})
		return
	}
	tick := time.NewTicker(time.Second)
	defer tick.Stop()
	for {
		st, err := lc.StatusWithoutPeers(ctx)
		if err == nil {
			s := tunnel.Status{State: tunnel.Starting, Step: "start"}
			switch st.BackendState {
			case ipn.NeedsLogin.String(), ipn.NeedsMachineAuth.String():
				s.State = tunnel.NeedsLogin
				s.Step = "login"
				s.AuthURL = st.AuthURL
			case ipn.Running.String():
				s.State = tunnel.Connected
				if len(st.TailscaleIPs) > 0 {
					s.Address = st.TailscaleIPs[0].String()
				}
			case ipn.Stopped.String():
				s.State = tunnel.Stopped
			}
			t.status.Set(s)
		}
		select {
		case <-ctx.Done():
			return
		case <-tick.C:
		}
	}
}

// Peer is a machine in the tailnet, for "import devices".
type Peer struct {
	Name   string `json:"name"`   // MagicDNS short name or host name
	DNS    string `json:"dns"`    // full MagicDNS name (dialable), may be empty
	IP     string `json:"ip"`     // first Tailscale address
	OS     string `json:"os"`
	Online bool   `json:"online"`
}

// Peers lists the other machines in the tailnet (needs a connected node).
func (t *Tunnel) Peers(ctx context.Context) ([]Peer, error) {
	lc, err := t.srv.LocalClient()
	if err != nil {
		return nil, err
	}
	st, err := lc.Status(ctx)
	if err != nil {
		return nil, err
	}
	var out []Peer
	for _, p := range st.Peer {
		if p == nil || len(p.TailscaleIPs) == 0 {
			continue
		}
		dns := strings.TrimSuffix(p.DNSName, ".")
		name := p.HostName
		if dns != "" {
			name = strings.SplitN(dns, ".", 2)[0]
		}
		out = append(out, Peer{Name: name, DNS: dns, IP: p.TailscaleIPs[0].String(), OS: p.OS, Online: p.Online})
	}
	sort.Slice(out, func(i, j int) bool { return strings.ToLower(out[i].Name) < strings.ToLower(out[j].Name) })
	return out, nil
}

func (t *Tunnel) DialContext(ctx context.Context, network, address string) (net.Conn, error) {
	if t.status.Get().State != tunnel.Connected {
		return nil, errors.New("tailscale: not connected")
	}
	return t.srv.Dial(ctx, network, address)
}

func (t *Tunnel) Status() tunnel.Status { return t.status.Get() }

func (t *Tunnel) Close() error {
	t.cancel()
	t.status.Set(tunnel.Status{State: tunnel.Stopped})
	return t.srv.Close()
}

// vaultStore implements ipn.StateStore on top of one encrypted vault blob.
type vaultStore struct {
	mu    sync.Mutex
	store tunnel.BlobStore
	name  string
	cache map[ipn.StateKey][]byte
}

func (s *vaultStore) load() map[ipn.StateKey][]byte {
	if s.cache != nil {
		return s.cache
	}
	s.cache = map[ipn.StateKey][]byte{}
	if raw, err := s.store.Blob(s.name); err == nil {
		_ = json.Unmarshal(raw, &s.cache)
	}
	return s.cache
}

func (s *vaultStore) ReadState(id ipn.StateKey) ([]byte, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	v, ok := s.load()[id]
	if !ok {
		return nil, ipn.ErrStateNotExist
	}
	return append([]byte(nil), v...), nil
}

func (s *vaultStore) WriteState(id ipn.StateKey, bs []byte) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	m := s.load()
	m[id] = append([]byte(nil), bs...)
	raw, err := json.Marshal(m)
	if err != nil {
		return err
	}
	return s.store.PutBlob(s.name, raw)
}
