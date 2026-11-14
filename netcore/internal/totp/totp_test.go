package totp

import (
	"encoding/base64"
	"net/url"
	"testing"
	"time"
)

// RFC 6238 Appendix B test vectors (8 digits).
func TestRFC6238Vectors(t *testing.T) {
	seeds := map[Algorithm][]byte{
		SHA1:   []byte("12345678901234567890"),
		SHA256: []byte("12345678901234567890123456789012"),
		SHA512: []byte("1234567890123456789012345678901234567890123456789012345678901234"),
	}
	cases := []struct {
		ts   int64
		alg  Algorithm
		want string
	}{
		{59, SHA1, "94287082"}, {59, SHA256, "46119246"}, {59, SHA512, "90693936"},
		{1111111109, SHA1, "07081804"}, {1111111109, SHA256, "68084774"}, {1111111109, SHA512, "25091201"},
		{1234567890, SHA1, "89005924"}, {1234567890, SHA256, "91819424"}, {1234567890, SHA512, "93441116"},
		{20000000000, SHA1, "65353130"}, {20000000000, SHA256, "77737706"}, {20000000000, SHA512, "47863826"},
	}
	for _, c := range cases {
		k := Key{Secret: seeds[c.alg], Algorithm: c.alg, Digits: 8, Period: 30}
		got, err := k.Code(time.Unix(c.ts, 0))
		if err != nil {
			t.Fatal(err)
		}
		if got != c.want {
			t.Errorf("%s @%d: got %s want %s", c.alg, c.ts, got, c.want)
		}
	}
}

func TestParseURI(t *testing.T) {
	// "12345678901234567890" in base32, lowercase and without padding.
	k, err := ParseURI("otpauth://totp/ACME:alice@example.com?secret=gezdgnbvgy3tqojqgezdgnbvgy3tqojq&issuer=ACME&digits=8")
	if err != nil {
		t.Fatal(err)
	}
	if k.Issuer != "ACME" || k.Account != "alice@example.com" || k.Digits != 8 || k.Period != 30 || k.Algorithm != SHA1 {
		t.Fatalf("unexpected key: %+v", k)
	}
	got, _ := k.Code(time.Unix(59, 0))
	if got != "94287082" {
		t.Fatalf("got %s", got)
	}
}

func TestParseURIRejects(t *testing.T) {
	for _, s := range []string{
		"https://example.com",
		"otpauth://hotp/x?secret=GEZDGNBV",
		"otpauth://totp/x?secret=not-base32!",
		"otpauth://totp/x?secret=GEZDGNBV&algorithm=MD5",
		"otpauth://totp/x?secret=GEZDGNBV&digits=7",
	} {
		if _, err := ParseURI(s); err == nil {
			t.Errorf("expected error for %s", s)
		}
	}
}

func TestRemaining(t *testing.T) {
	k := Key{Secret: []byte("x")}
	if r := k.Remaining(time.Unix(65, 0)); r != 25*time.Second {
		t.Fatalf("got %v", r)
	}
}

// Build a migration payload by hand: one TOTP entry and one HOTP entry.
func TestParseMigration(t *testing.T) {
	param := func(secret, name, issuer string, alg, digits, typ byte) []byte {
		var b []byte
		b = append(b, 0x0a, byte(len(secret)))
		b = append(b, secret...)
		b = append(b, 0x12, byte(len(name)))
		b = append(b, name...)
		b = append(b, 0x1a, byte(len(issuer)))
		b = append(b, issuer...)
		b = append(b, 0x20, alg, 0x28, digits, 0x30, typ)
		return b
	}
	p1 := param("12345678901234567890", "ACME:alice", "ACME", 1, 1, 2)
	p2 := param("abcdefghij", "Other:bob", "Other", 1, 1, 1) // HOTP, skipped
	var payload []byte
	for _, p := range [][]byte{p1, p2} {
		payload = append(payload, 0x0a, byte(len(p)))
		payload = append(payload, p...)
	}
	payload = append(payload, 0x10, 0x01) // version = 1

	uri := "otpauth-migration://offline?data=" + url.QueryEscape(base64.StdEncoding.EncodeToString(payload))
	keys, err := ParseMigration(uri)
	if err != nil {
		t.Fatal(err)
	}
	if len(keys) != 1 {
		t.Fatalf("want 1 TOTP key, got %d", len(keys))
	}
	k := keys[0]
	if k.Issuer != "ACME" || k.Account != "alice" || k.Digits != 6 {
		t.Fatalf("unexpected key: %+v", k)
	}
	got, _ := k.Code(time.Unix(59, 0))
	if got != "287082" {
		t.Fatalf("got %s", got)
	}
}
