package gp

import "testing"

func TestRedactSecrets(t *testing.T) {
	cases := []struct {
		in, want string
		withheld bool
	}{
		{"POST https://vpn.example.com/ssl-vpn/login.esp?user=alice&passwd=x", "[line omitted: may contain credentials]", true},
		{"GET https://vpn.example.com/global-protect/getconfig.esp?a=1", "GET https://vpn.example.com/[redacted]", false},
		{"Connected to https://bob:pw@host.example/path", "Connected to https://host.example/[redacted]", false},
		{"Weird https://a@b@c/x", "Weird https://c/[redacted]", false}, // user info dropped
		{"Got HTTP response: HTTP/1.1 512", "Got HTTP response: HTTP/1.1 512", false},
		{"value AbCdEfGhIjKlMnOpQrStUvWxYz0123456789 here", "value [redacted] here", false},
		{"portal-userauthcookie=abc", "[line omitted: may contain credentials]", true},
	}
	for _, c := range cases {
		got, withheld := redactSecrets(c.in)
		if got != c.want || withheld != c.withheld {
			t.Errorf("redactSecrets(%q) = %q, %v; want %q, %v", c.in, got, withheld, c.want, c.withheld)
		}
	}
}
