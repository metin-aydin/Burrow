package totp

import (
	"encoding/base64"
	"errors"
	"fmt"
	"net/url"
	"strings"
)

// ParseMigration decodes a Google Authenticator export QR payload
// (otpauth-migration://offline?data=...). Only TOTP entries are returned;
// HOTP entries are skipped. The protobuf schema is community-documented:
//
//	MigrationPayload { repeated OtpParameters otp_parameters = 1; ... }
//	OtpParameters { bytes secret = 1; string name = 2; string issuer = 3;
//	                Algorithm algorithm = 4; DigitCount digits = 5; OtpType type = 6; }
func ParseMigration(raw string) ([]Key, error) {
	u, err := url.Parse(strings.TrimSpace(raw))
	if err != nil {
		return nil, err
	}
	if u.Scheme != "otpauth-migration" {
		return nil, errors.New("totp: not an otpauth-migration URI")
	}
	data := u.Query().Get("data")
	if data == "" {
		return nil, errors.New("totp: migration URI has no data")
	}
	buf, err := decodeB64(data)
	if err != nil {
		return nil, fmt.Errorf("totp: bad migration data: %w", err)
	}

	var keys []Key
	err = walk(buf, func(field int, wire int, v uint64, b []byte) error {
		if field != 1 || wire != 2 {
			return nil
		}
		k, isTOTP, err := parseOtpParameters(b)
		if err != nil {
			return err
		}
		if isTOTP {
			keys = append(keys, k)
		}
		return nil
	})
	if err != nil {
		return nil, err
	}
	return keys, nil
}

func decodeB64(s string) ([]byte, error) {
	s = strings.TrimRight(s, "=")
	if b, err := base64.RawStdEncoding.DecodeString(s); err == nil {
		return b, nil
	}
	return base64.RawURLEncoding.DecodeString(s)
}

func parseOtpParameters(b []byte) (Key, bool, error) {
	k := Key{Algorithm: SHA1, Digits: 6, Period: 30}
	otpType := uint64(2) // default to TOTP if absent
	err := walk(b, func(field int, wire int, v uint64, bytes []byte) error {
		switch field {
		case 1:
			k.Secret = append([]byte(nil), bytes...)
		case 2:
			name := string(bytes)
			if i := strings.Index(name, ":"); i >= 0 {
				name = strings.TrimSpace(name[i+1:])
			}
			k.Account = name
		case 3:
			k.Issuer = string(bytes)
		case 4:
			switch v {
			case 2:
				k.Algorithm = SHA256
			case 3:
				k.Algorithm = SHA512
			case 4:
				k.Algorithm = "MD5" // rejected by Code()
			}
		case 5:
			if v == 2 {
				k.Digits = 8
			}
		case 6:
			otpType = v
		}
		return nil
	})
	return k, otpType == 2, err
}

// walk iterates top-level protobuf fields. For varints v is set; for
// length-delimited fields b is set. Other wire types are skipped.
func walk(buf []byte, fn func(field, wire int, v uint64, b []byte) error) error {
	for len(buf) > 0 {
		tag, n := uvarint(buf)
		if n <= 0 {
			return errors.New("totp: truncated protobuf tag")
		}
		buf = buf[n:]
		field, wire := int(tag>>3), int(tag&7)
		switch wire {
		case 0:
			v, n := uvarint(buf)
			if n <= 0 {
				return errors.New("totp: truncated varint")
			}
			buf = buf[n:]
			if err := fn(field, wire, v, nil); err != nil {
				return err
			}
		case 2:
			l, n := uvarint(buf)
			if n <= 0 || uint64(len(buf)-n) < l {
				return errors.New("totp: truncated bytes field")
			}
			b := buf[n : n+int(l)]
			buf = buf[n+int(l):]
			if err := fn(field, wire, 0, b); err != nil {
				return err
			}
		case 1:
			if len(buf) < 8 {
				return errors.New("totp: truncated fixed64")
			}
			buf = buf[8:]
		case 5:
			if len(buf) < 4 {
				return errors.New("totp: truncated fixed32")
			}
			buf = buf[4:]
		default:
			return fmt.Errorf("totp: unsupported wire type %d", wire)
		}
	}
	return nil
}

func uvarint(b []byte) (uint64, int) {
	var x uint64
	var s uint
	for i, c := range b {
		if i == 10 {
			return 0, -1
		}
		if c < 0x80 {
			return x | uint64(c)<<s, i + 1
		}
		x |= uint64(c&0x7f) << s
		s += 7
	}
	return 0, 0
}
