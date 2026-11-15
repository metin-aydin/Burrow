package main

/*
#include <stdlib.h>
#include <string.h>
*/
import "C"

import (
	"encoding/base32"
	"encoding/json"
	"errors"
	"os"
	"strings"
	"time"
	"unsafe"

	"burrow/netcore/internal/totp"
	"burrow/netcore/internal/vault"
)

// Vault error codes returned through the C API.
const (
	vaultOK            = 0
	vaultWrongPassword = 1
	vaultCorrupt       = 2
	vaultExists        = 3
	vaultIOError       = 4
	vaultBadHandle     = 5
)

var vaults = map[C.int]*vault.Vault{}

func vaultCode(err error) C.int {
	switch {
	case err == nil:
		return vaultOK
	case errors.Is(err, vault.ErrWrongPassword):
		return vaultWrongPassword
	case errors.Is(err, vault.ErrCorrupt):
		return vaultCorrupt
	case errors.Is(err, vault.ErrExists):
		return vaultExists
	case errors.Is(err, vault.ErrClosed):
		return vaultBadHandle
	default:
		return vaultIOError
	}
}

func registerVault(v *vault.Vault) C.int {
	mu.Lock()
	defer mu.Unlock()
	id := nextID
	nextID++
	vaults[id] = v
	return id
}

func lookupVault(h C.int) *vault.Vault {
	mu.Lock()
	defer mu.Unlock()
	return vaults[h]
}

func setErr(out *C.int, code C.int) {
	if out != nil {
		*out = code
	}
}

//export NetcoreVaultExists
func NetcoreVaultExists(path *C.char) C.int {
	if _, err := os.Stat(C.GoString(path)); err == nil {
		return 1
	}
	return 0
}

// NetcoreVaultCreate creates a vault holding doc (JSON). Returns a handle > 0,
// or 0 with *err set.
//
//export NetcoreVaultCreate
func NetcoreVaultCreate(path, password, doc *C.char, errOut *C.int) C.int {
	v, err := vault.Create(C.GoString(path), C.GoString(password), []byte(C.GoString(doc)))
	setErr(errOut, vaultCode(err))
	if err != nil {
		return 0
	}
	return registerVault(v)
}

//export NetcoreVaultOpen
func NetcoreVaultOpen(path, password *C.char, errOut *C.int) C.int {
	v, err := vault.Open(C.GoString(path), C.GoString(password))
	setErr(errOut, vaultCode(err))
	if err != nil {
		return 0
	}
	return registerVault(v)
}

// NetcoreVaultDocument returns the decrypted JSON document (free with
// NetcoreFreeSecret), or NULL.
//
//export NetcoreVaultDocument
func NetcoreVaultDocument(h C.int) *C.char {
	v := lookupVault(h)
	if v == nil {
		return nil
	}
	doc, err := v.Document()
	if err != nil {
		return nil
	}
	return C.CString(string(doc))
}

//export NetcoreVaultSave
func NetcoreVaultSave(h C.int, doc *C.char) C.int {
	v := lookupVault(h)
	if v == nil {
		return vaultBadHandle
	}
	return vaultCode(v.Save([]byte(C.GoString(doc))))
}

//export NetcoreVaultChangePassword
func NetcoreVaultChangePassword(h C.int, newPassword *C.char) C.int {
	v := lookupVault(h)
	if v == nil {
		return vaultBadHandle
	}
	return vaultCode(v.ChangePassword(C.GoString(newPassword)))
}

//export NetcoreVaultClose
func NetcoreVaultClose(h C.int) {
	mu.Lock()
	v := vaults[h]
	delete(vaults, h)
	mu.Unlock()
	if v != nil {
		v.Close()
	}
}

// NetcoreFreeSecret zeroes and frees a string returned by the API.
//
//export NetcoreFreeSecret
func NetcoreFreeSecret(p *C.char) {
	if p == nil {
		return
	}
	C.memset(unsafe.Pointer(p), 0, C.strlen(p))
	C.free(unsafe.Pointer(p))
}

type totpAccount struct {
	Issuer    string `json:"issuer"`
	Account   string `json:"account"`
	Secret    string `json:"secret"` // base32, no padding
	Algorithm string `json:"algorithm"`
	Digits    int    `json:"digits"`
	Period    int    `json:"period"`
}

// NetcoreTOTPParse parses an otpauth:// or otpauth-migration:// URI and
// returns a JSON array of accounts (free with NetcoreFreeSecret), or NULL.
//
//export NetcoreTOTPParse
func NetcoreTOTPParse(uri *C.char) *C.char {
	raw := strings.TrimSpace(C.GoString(uri))
	var keys []totp.Key
	var err error
	if strings.HasPrefix(raw, "otpauth-migration://") {
		keys, err = totp.ParseMigration(raw)
	} else {
		var k totp.Key
		k, err = totp.ParseURI(raw)
		keys = []totp.Key{k}
	}
	if err != nil || len(keys) == 0 {
		return nil
	}
	enc := base32.StdEncoding.WithPadding(base32.NoPadding)
	out := make([]totpAccount, 0, len(keys))
	for _, k := range keys {
		out = append(out, totpAccount{k.Issuer, k.Account, enc.EncodeToString(k.Secret),
			string(k.Algorithm), k.Digits, k.Period})
	}
	buf, _ := json.Marshal(out)
	return C.CString(string(buf))
}

// NetcoreTOTPCode returns the current code for a base32 secret and writes the
// seconds left in the window to *remaining. Returns NULL on bad input.
//
//export NetcoreTOTPCode
func NetcoreTOTPCode(secretB32, algorithm *C.char, digits, period C.int, remaining *C.int) *C.char {
	secret, err := totp.DecodeSecret(C.GoString(secretB32))
	if err != nil {
		return nil
	}
	k := totp.Key{Secret: secret, Algorithm: totp.Algorithm(strings.ToUpper(C.GoString(algorithm))),
		Digits: int(digits), Period: int(period)}
	now := time.Now()
	code, err := k.Code(now)
	if err != nil {
		return nil
	}
	if remaining != nil {
		*remaining = C.int(k.Remaining(now) / time.Second)
	}
	return C.CString(code)
}

// NetcoreVaultPutBlob stores len bytes at data as an encrypted blob.
//
//export NetcoreVaultPutBlob
func NetcoreVaultPutBlob(h C.int, name *C.char, data unsafe.Pointer, length C.int) C.int {
	v := lookupVault(h)
	if v == nil {
		return vaultBadHandle
	}
	return vaultCode(v.PutBlob(C.GoString(name), C.GoBytes(data, length)))
}

// NetcoreVaultBlob returns a decrypted blob (free with NetcoreFreeBytes) and
// writes its length to *outLen, or returns NULL if missing or unreadable.
//
//export NetcoreVaultBlob
func NetcoreVaultBlob(h C.int, name *C.char, outLen *C.int) unsafe.Pointer {
	v := lookupVault(h)
	if v == nil {
		return nil
	}
	b, err := v.Blob(C.GoString(name))
	if err != nil || len(b) == 0 {
		return nil
	}
	*outLen = C.int(len(b))
	return C.CBytes(b)
}

//export NetcoreVaultDeleteBlob
func NetcoreVaultDeleteBlob(h C.int, name *C.char) C.int {
	v := lookupVault(h)
	if v == nil {
		return vaultBadHandle
	}
	return vaultCode(v.DeleteBlob(C.GoString(name)))
}

//export NetcoreFreeBytes
func NetcoreFreeBytes(p unsafe.Pointer) { C.free(p) }
