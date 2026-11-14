package cf

import (
	"context"
	"crypto/tls"
	"encoding/base64"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"net/http/httptest"
	"net/url"
	"testing"
	"time"

	"github.com/coder/websocket"
)

type memStore map[string][]byte

func (m memStore) Blob(name string) ([]byte, error) {
	if v, ok := m[name]; ok {
		return v, nil
	}
	return nil, errors.New("missing")
}
func (m memStore) PutBlob(name string, data []byte) error { m[name] = data; return nil }

func jwt(exp time.Time) string {
	p, _ := json.Marshal(map[string]int64{"exp": exp.Unix()})
	return "x." + base64.RawURLEncoding.EncodeToString(p) + ".y"
}

func TestExpired(t *testing.T) {
	if expired(jwt(time.Now().Add(time.Hour))) {
		t.Fatal("valid token reported expired")
	}
	if !expired(jwt(time.Now().Add(30 * time.Second))) {
		t.Fatal("token expiring within a minute should count as expired")
	}
	if !expired("garbage") {
		t.Fatal("garbage token accepted")
	}
}

// A local Access stand-in: checks the token and jump header, then echoes bytes over the WebSocket.
func TestDialCarriesTCPOverWebSocket(t *testing.T) {
	var gotToken, gotJump string
	srv := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotToken, gotJump = r.Header.Get("cf-access-token"), r.Header.Get("Cf-Access-Jump-Destination")
		if gotToken == "" {
			w.WriteHeader(http.StatusForbidden)
			return
		}
		c, err := websocket.Accept(w, r, nil)
		if err != nil {
			return
		}
		conn := websocket.NetConn(context.Background(), c, websocket.MessageBinary)
		io.Copy(conn, conn)
	}))
	defer srv.Close()
	host := mustHost(t, srv.URL)

	store := memStore{}
	tun, _ := Start(Config{ID: "p1", Bastion: host}, store)
	tun.httpClient = srv.Client()
	tun.tokens.AppTokens[host] = jwt(time.Now().Add(time.Hour))

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	conn, err := tun.DialContext(ctx, "tcp", "10.0.0.5:3389")
	if err != nil {
		t.Fatal(err)
	}
	defer conn.Close()
	conn.Write([]byte("hello rdp"))
	buf := make([]byte, 9)
	if _, err := io.ReadFull(conn, buf); err != nil || string(buf) != "hello rdp" {
		t.Fatalf("echo = %q, %v", buf, err)
	}
	if gotJump != "10.0.0.5:3389" {
		t.Fatalf("jump destination = %q", gotJump)
	}

	// A rejected token is forgotten so the next connection signs in again.
	tun.tokens.AppTokens[host] = ""
	if _, err := tun.DialContext(ctx, "tcp", "10.0.0.5:3389"); err == nil {
		t.Fatal("dial without token succeeded")
	}
}

func mustHost(t *testing.T, raw string) string {
	u, err := url.Parse(raw)
	if err != nil {
		t.Fatal(err)
	}
	return u.Host
}

func TestValidAuthDomain(t *testing.T) {
	for d, want := range map[string]bool{
		"team.cloudflareaccess.com":     true,
		"team.fed.cloudflareaccess.com": true,
		"evil.example.com":              false,
		"cloudflareaccess.com.evil.io":  false,
		"a@team.cloudflareaccess.com":   false,
	} {
		if validAuthDomain(d) != want {
			t.Errorf("validAuthDomain(%q) != %v", d, want)
		}
	}
}

// A redirect away from the app and the Access domain must not receive the org token.
func TestExchangeRefusesForeignRedirect(t *testing.T) {
	var leaked bool
	evil := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if _, err := r.Cookie("CF_Authorization"); err == nil {
			leaked = true
		}
	}))
	defer evil.Close()
	app := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Redirect(w, r, evil.URL+"/cdn-cgi/access/login/x", http.StatusFound)
	}))
	defer app.Close()
	exchangeTransport = &http.Transport{TLSClientConfig: &tls.Config{InsecureSkipVerify: true}} // test servers only
	defer func() { exchangeTransport = nil }()
	u, _ := url.Parse(app.URL)
	if _, err := exchangeOrgToken(u, "team.cloudflareaccess.com", "secret-org-token"); err == nil {
		t.Fatal("exchange followed a foreign redirect")
	}
	if leaked {
		t.Fatal("org token was sent to a foreign host")
	}
}
