// Package cf reaches RDP published through Cloudflare Tunnel + Access (ADR 0007). TCP is carried over
// a WebSocket to the Access application with the user's Access token; sign-in happens once in the
// browser through Cloudflare's own token package, and tokens are kept in the encrypted vault.
package cf

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"net/http"
	"net/url"
	"strings"
	"sync"
	"time"

	"github.com/cloudflare/cloudflared/token"
	"github.com/coder/websocket"
	"github.com/rs/zerolog"

	"burrow/netcore/internal/tunnel"
)

// Config is a Cloudflare tunnel profile. With Bastion set, devices are addressed by their internal
// host:port and reached through that Access bastion; otherwise a device's host is its Access app.
type Config struct {
	ID      string
	Bastion string
}

type saved struct {
	AppTokens map[string]string `json:"app"` // app hostname -> Access app token (JWT)
	OrgTokens map[string]string `json:"org"` // auth domain -> org token
}

type Tunnel struct {
	cfg    Config
	store  tunnel.BlobStore
	status tunnel.StatusBox

	mu     sync.Mutex
	tokens saved
	busy   bool

	httpClient *http.Client // nil = default; tests inject one that trusts a local TLS server
}

func Start(cfg Config, store tunnel.BlobStore) (*Tunnel, error) {
	t := &Tunnel{cfg: cfg, store: store}
	t.tokens = saved{AppTokens: map[string]string{}, OrgTokens: map[string]string{}}
	if raw, err := store.Blob(t.blobName()); err == nil {
		_ = json.Unmarshal(raw, &t.tokens)
		if t.tokens.AppTokens == nil {
			t.tokens.AppTokens = map[string]string{}
		}
		if t.tokens.OrgTokens == nil {
			t.tokens.OrgTokens = map[string]string{}
		}
	}
	t.status.Set(tunnel.Status{State: tunnel.Connected})
	return t, nil
}

func (t *Tunnel) blobName() string { return "cloudflare-" + t.cfg.ID }

func (t *Tunnel) persist() {
	raw, err := json.Marshal(t.tokens)
	if err == nil {
		_ = t.store.PutBlob(t.blobName(), raw)
	}
}

// appHost is the Access application that carries traffic for a device address.
func (t *Tunnel) appHost(address string) string {
	if t.cfg.Bastion != "" {
		return strings.TrimSpace(t.cfg.Bastion)
	}
	host, _, err := net.SplitHostPort(address)
	if err != nil {
		return address
	}
	return host
}

// Prepare makes sure an Access token exists for the device address, opening the browser to sign in
// when needed. Status goes starting → needs_login (browser) → connected, or failed.
func (t *Tunnel) Prepare(address string) {
	host := t.appHost(address)
	t.mu.Lock()
	if t.busy {
		t.mu.Unlock()
		return
	}
	if tok := t.tokens.AppTokens[host]; tok != "" && !expired(tok) {
		t.mu.Unlock()
		t.status.Set(tunnel.Status{State: tunnel.Connected})
		return
	}
	t.busy = true
	t.mu.Unlock()
	t.status.Set(tunnel.Status{State: tunnel.Starting})
	go func() {
		defer func() { t.mu.Lock(); t.busy = false; t.mu.Unlock() }()
		if err := t.fetchToken(host); err != nil {
			t.status.Set(tunnel.Status{State: tunnel.Failed, Error: err.Error()})
			return
		}
		t.status.Set(tunnel.Status{State: tunnel.Connected})
	}()
}

func (t *Tunnel) fetchToken(host string) error {
	appURL := &url.URL{Scheme: "https", Host: host}
	info, err := token.GetAppInfo(appURL)
	if err != nil {
		return fmt.Errorf("no Cloudflare Access application found at %s (%v)", host, err)
	}
	// An org token from an earlier sign-in gets a new app token without the browser.
	if !validAuthDomain(info.AuthDomain) {
		return fmt.Errorf("unexpected Cloudflare Access domain %q", info.AuthDomain)
	}
	t.mu.Lock()
	org := t.tokens.OrgTokens[info.AuthDomain]
	t.mu.Unlock()
	if org != "" {
		if app, err := exchangeOrgToken(appURL, info.AuthDomain, org); err == nil {
			t.mu.Lock()
			t.tokens.AppTokens[host] = app
			t.persist()
			t.mu.Unlock()
			return nil
		}
	}
	t.status.Set(tunnel.Status{State: tunnel.NeedsLogin, Prompt: host})
	nop := zerolog.Nop()
	data, err := token.RunTransfer(appURL, info.AppAUD, "token", "token", "", true, false, true, false, &nop, "")
	if err != nil {
		return fmt.Errorf("Cloudflare sign-in didn't finish: %v", err)
	}
	var resp struct {
		AppToken string `json:"app_token"`
		OrgToken string `json:"org_token"`
	}
	if err := json.Unmarshal(data, &resp); err != nil || resp.AppToken == "" {
		return errors.New("Cloudflare returned no access token")
	}
	t.mu.Lock()
	t.tokens.AppTokens[host] = resp.AppToken
	if resp.OrgToken != "" {
		t.tokens.OrgTokens[info.AuthDomain] = resp.OrgToken
	}
	t.persist()
	t.mu.Unlock()
	return nil
}

func (t *Tunnel) DialContext(ctx context.Context, _, address string) (net.Conn, error) {
	host := t.appHost(address)
	t.mu.Lock()
	tok := t.tokens.AppTokens[host]
	t.mu.Unlock()
	if tok == "" {
		return nil, errors.New("cloudflare: not signed in")
	}
	h := http.Header{}
	h.Set("cf-access-token", tok)
	if t.cfg.Bastion != "" {
		h.Set("Cf-Access-Jump-Destination", address)
	}
	c, resp, err := websocket.Dial(ctx, "wss://"+host, &websocket.DialOptions{HTTPHeader: h, HTTPClient: t.httpClient})
	if err != nil {
		if resp != nil && (resp.StatusCode == http.StatusUnauthorized || resp.StatusCode == http.StatusForbidden ||
			resp.StatusCode == http.StatusFound) {
			// The token was rejected (expired or revoked): forget it so the next connect signs in again.
			t.mu.Lock()
			delete(t.tokens.AppTokens, host)
			t.persist()
			t.mu.Unlock()
		}
		return nil, fmt.Errorf("cloudflare: %v", err)
	}
	c.SetReadLimit(-1)
	return websocket.NetConn(context.Background(), c, websocket.MessageBinary), nil
}

func (t *Tunnel) Status() tunnel.Status { return t.status.Get() }

func (t *Tunnel) Close() error {
	t.status.Set(tunnel.Status{State: tunnel.Stopped})
	return nil
}

// expired reports whether a JWT expires within a minute (signature isn't checked; the edge does).
func expired(jwt string) bool {
	parts := strings.Split(jwt, ".")
	if len(parts) != 3 {
		return true
	}
	payload, err := base64.RawURLEncoding.DecodeString(parts[1])
	if err != nil {
		return true
	}
	var claims struct {
		Exp int64 `json:"exp"`
	}
	if json.Unmarshal(payload, &claims) != nil || claims.Exp == 0 {
		return true
	}
	return time.Now().Add(time.Minute).After(time.Unix(claims.Exp, 0))
}

// validAuthDomain accepts only Cloudflare Access team domains.
func validAuthDomain(d string) bool {
	d = strings.ToLower(strings.TrimSuffix(d, "."))
	return strings.HasSuffix(d, ".cloudflareaccess.com") && !strings.ContainsAny(d, "/@:")
}

func sameHost(u *url.URL, host string) bool {
	return strings.EqualFold(strings.TrimSuffix(u.Hostname(), "."), strings.TrimSuffix(host, "."))
}

// exchangeOrgToken follows Access' SSO redirects with the org token and returns a fresh app token.
// The org token is only ever sent over https to the Access team domain, and redirects to any host
// other than the app and that domain are refused.
// Adapted from cloudflared's token package (Apache-2.0, github.com/cloudflare/cloudflared).
// exchangeTransport is replaced in tests (local TLS servers); nil = default transport.
var exchangeTransport http.RoundTripper

func exchangeOrgToken(appURL *url.URL, authDomain, orgToken string) (string, error) {
	client := &http.Client{
		Timeout:   10 * time.Second,
		Transport: exchangeTransport,
		CheckRedirect: func(req *http.Request, via []*http.Request) error {
			if req.URL.Scheme != "https" || (!sameHost(req.URL, appURL.Hostname()) && !sameHost(req.URL, authDomain)) {
				return fmt.Errorf("refusing redirect to %s", req.URL.Host)
			}
			if sameHost(req.URL, authDomain) && strings.Contains(req.URL.Path, token.AccessLoginWorkerPath) {
				req.AddCookie(&http.Cookie{Name: "CF_Authorization", Value: orgToken})
			}
			if sameHost(req.URL, appURL.Hostname()) && strings.Contains(req.URL.Path, token.AccessAuthorizedWorkerPath) {
				for _, prev := range via {
					if prev != nil && prev.Response != nil {
						for _, c := range prev.Response.Cookies() {
							if c.Name == "CF_AppSession" {
								req.AddCookie(&http.Cookie{Name: "CF_AppSession", Value: c.Value})
							}
						}
					}
				}
			}
			if len(via) > 0 && strings.Contains(via[len(via)-1].URL.Path, token.AccessAuthorizedWorkerPath) {
				return http.ErrUseLastResponse
			}
			if len(via) > 10 {
				return errors.New("too many redirects")
			}
			return nil
		},
	}
	req, err := http.NewRequest(http.MethodHead, appURL.String(), nil)
	if err != nil {
		return "", err
	}
	resp, err := client.Do(req)
	if err != nil {
		return "", err
	}
	_ = resp.Body.Close()
	// Only trust an app token set by the application host itself.
	if resp.Request == nil || resp.Request.URL.Scheme != "https" || !sameHost(resp.Request.URL, appURL.Hostname()) {
		return "", errors.New("the app token didn't come from the application host")
	}
	for _, c := range resp.Cookies() {
		if c.Name == "CF_Authorization" && time.Now().Before(c.Expires) {
			return c.Value, nil
		}
	}
	return "", errors.New("no app token in the response")
}
