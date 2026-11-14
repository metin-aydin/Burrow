package gp

import "testing"

func TestRedactSecrets(t *testing.T) {
	cases := map[string]string{
		"POST https://vpn.example.com/ssl-vpn/login.esp?user=alice&passwd=x": "[line omitted: may contain credentials]",
		"GET https://vpn.example.com/global-protect/getconfig.esp?a=1":       "GET https://vpn.example.com/global-protect/getconfig.esp?[redacted]",
		"Connected to https://bob:pw@host.example/path":                      "Connected to https://host.example/path",
		"Got HTTP response: HTTP/1.1 512":                                    "Got HTTP response: HTTP/1.1 512",
		"portal-userauthcookie=abc":                                          "[line omitted: may contain credentials]",
	}
	for in, want := range cases {
		if got := redactSecrets(in); got != want {
			t.Errorf("redactSecrets(%q) = %q, want %q", in, got, want)
		}
	}
}
