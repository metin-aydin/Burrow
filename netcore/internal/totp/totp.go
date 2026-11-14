// Package totp implements RFC 6238 TOTP generation and parsing of
// authenticator QR payloads (otpauth:// and Google Authenticator
// otpauth-migration://offline export).
package totp

import (
	"crypto/hmac"
	"crypto/sha1"
	"crypto/sha256"
	"crypto/sha512"
	"encoding/base32"
	"encoding/binary"
	"errors"
	"fmt"
	"hash"
	"net/url"
	"strconv"
	"strings"
	"time"
)

type Algorithm string

const (
	SHA1   Algorithm = "SHA1"
	SHA256 Algorithm = "SHA256"
	SHA512 Algorithm = "SHA512"
)

// Key is one TOTP account. Secret is the raw (decoded) shared secret.
type Key struct {
	Issuer    string
	Account   string
	Secret    []byte
	Algorithm Algorithm
	Digits    int
	Period    int // seconds
}

func (k Key) withDefaults() Key {
	if k.Algorithm == "" {
		k.Algorithm = SHA1
	}
	if k.Digits == 0 {
		k.Digits = 6
	}
	if k.Period == 0 {
		k.Period = 30
	}
	return k
}

func (k Key) hasher() (func() hash.Hash, error) {
	switch k.Algorithm {
	case SHA1:
		return sha1.New, nil
	case SHA256:
		return sha256.New, nil
	case SHA512:
		return sha512.New, nil
	}
	return nil, fmt.Errorf("totp: unsupported algorithm %q", k.Algorithm)
}

// Code returns the TOTP code valid at time t.
func (k Key) Code(t time.Time) (string, error) {
	k = k.withDefaults()
	if len(k.Secret) == 0 {
		return "", errors.New("totp: empty secret")
	}
	h, err := k.hasher()
	if err != nil {
		return "", err
	}
	counter := uint64(t.Unix()) / uint64(k.Period)
	var msg [8]byte
	binary.BigEndian.PutUint64(msg[:], counter)
	mac := hmac.New(h, k.Secret)
	mac.Write(msg[:])
	sum := mac.Sum(nil)
	off := sum[len(sum)-1] & 0x0f
	bin := binary.BigEndian.Uint32(sum[off:off+4]) & 0x7fffffff
	mod := uint32(1)
	for i := 0; i < k.Digits; i++ {
		mod *= 10
	}
	return fmt.Sprintf("%0*d", k.Digits, bin%mod), nil
}

// Remaining returns how long the code valid at t stays valid. Used for the
// GlobalProtect double-OTP flow, which needs the *next* code for step two.
func (k Key) Remaining(t time.Time) time.Duration {
	k = k.withDefaults()
	p := int64(k.Period)
	return time.Duration(p-t.Unix()%p) * time.Second
}

// DecodeSecret decodes a base32 secret, tolerating spaces, lowercase and
// missing padding (as typed by users or found in otpauth URIs).
func DecodeSecret(s string) ([]byte, error) {
	s = strings.ToUpper(strings.ReplaceAll(strings.TrimSpace(s), " ", ""))
	s = strings.TrimRight(s, "=")
	b, err := base32.StdEncoding.WithPadding(base32.NoPadding).DecodeString(s)
	if err != nil {
		return nil, fmt.Errorf("totp: invalid base32 secret: %w", err)
	}
	return b, nil
}

// ParseURI parses an otpauth://totp/... key URI.
func ParseURI(raw string) (Key, error) {
	u, err := url.Parse(strings.TrimSpace(raw))
	if err != nil {
		return Key{}, err
	}
	if u.Scheme != "otpauth" {
		return Key{}, fmt.Errorf("totp: not an otpauth URI")
	}
	if u.Host != "totp" {
		return Key{}, fmt.Errorf("totp: only totp is supported, got %q", u.Host)
	}
	q := u.Query()
	secret, err := DecodeSecret(q.Get("secret"))
	if err != nil {
		return Key{}, err
	}
	k := Key{Secret: secret, Issuer: q.Get("issuer")}

	label := strings.TrimPrefix(u.Path, "/")
	if i := strings.Index(label, ":"); i >= 0 {
		if k.Issuer == "" {
			k.Issuer = label[:i]
		}
		k.Account = strings.TrimSpace(label[i+1:])
	} else {
		k.Account = label
	}
	if a := q.Get("algorithm"); a != "" {
		k.Algorithm = Algorithm(strings.ToUpper(a))
	}
	if d := q.Get("digits"); d != "" {
		if k.Digits, err = strconv.Atoi(d); err != nil {
			return Key{}, fmt.Errorf("totp: bad digits: %w", err)
		}
	}
	if p := q.Get("period"); p != "" {
		if k.Period, err = strconv.Atoi(p); err != nil {
			return Key{}, fmt.Errorf("totp: bad period: %w", err)
		}
	}
	k = k.withDefaults()
	if _, err := k.hasher(); err != nil {
		return Key{}, err
	}
	if k.Digits != 6 && k.Digits != 8 {
		return Key{}, fmt.Errorf("totp: unsupported digits %d", k.Digits)
	}
	return k, nil
}
