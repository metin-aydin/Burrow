// Package vault implements Burrow's encrypted local store (ADR 0004).
//
// A random 32-byte data key (DEK) encrypts the content with
// XChaCha20-Poly1305. The DEK is wrapped with a key derived from the master
// password via Argon2id. The file is a small JSON envelope; the content is an
// opaque document whose schema belongs to the application.
package vault

import (
	"bytes"
	"crypto/rand"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync"

	"golang.org/x/crypto/argon2"
	"golang.org/x/crypto/chacha20poly1305"
)

const (
	formatVersion = 1
	keySize       = chacha20poly1305.KeySize
	saltSize      = 16
)

var (
	ErrWrongPassword = errors.New("vault: wrong password or key")
	ErrCorrupt       = errors.New("vault: file is corrupt or unsupported")
	ErrExists        = errors.New("vault: file already exists")
	ErrClosed        = errors.New("vault: closed")
)

// KDFParams are stored in the file so they can be raised later.
type KDFParams struct {
	Alg     string `json:"alg"`
	Time    uint32 `json:"t"`
	Memory  uint32 `json:"m"` // KiB
	Threads uint8  `json:"p"`
	Salt    []byte `json:"salt"`
}

// DefaultKDF is used for new vaults and password changes.
var DefaultKDF = KDFParams{Alg: "argon2id", Time: 3, Memory: 64 * 1024, Threads: 4}

type sealed struct {
	Nonce []byte `json:"nonce"`
	CT    []byte `json:"ct"`
}

type envelope struct {
	V    int       `json:"v"`
	KDF  KDFParams `json:"kdf"`
	Key  sealed    `json:"key"`
	Data sealed    `json:"data"`
}

// Vault is an unlocked vault. Methods are safe for concurrent use.
type Vault struct {
	mu   sync.Mutex
	path string
	dek  []byte
	env  envelope
	doc  []byte
}

// Create writes a new vault holding doc and returns it unlocked.
func Create(path, password string, doc []byte) (*Vault, error) {
	if _, err := os.Stat(path); err == nil {
		return nil, ErrExists
	}
	dek := make([]byte, keySize)
	if _, err := rand.Read(dek); err != nil {
		return nil, err
	}
	v := &Vault{path: path, dek: dek, env: envelope{V: formatVersion}}
	if err := v.wrapKey(password, DefaultKDF); err != nil {
		return nil, err
	}
	if err := v.write(doc); err != nil {
		return nil, err
	}
	return v, nil
}

// Open unlocks the vault at path with the master password.
func Open(path, password string) (*Vault, error) {
	env, err := readEnvelope(path)
	if err != nil {
		return nil, err
	}
	kek, err := deriveKEK(password, env.KDF)
	if err != nil {
		return nil, err
	}
	dek, err := open(kek, env.Key, keyAAD(env))
	if err != nil {
		return nil, ErrWrongPassword
	}
	return finishOpen(path, env, dek)
}

// OpenWithKey unlocks the vault with a previously exported data key (Touch ID).
func OpenWithKey(path string, dek []byte) (*Vault, error) {
	if len(dek) != keySize {
		return nil, ErrWrongPassword
	}
	env, err := readEnvelope(path)
	if err != nil {
		return nil, err
	}
	return finishOpen(path, env, append([]byte(nil), dek...))
}

func finishOpen(path string, env envelope, dek []byte) (*Vault, error) {
	doc, err := open(dek, env.Data, dataAAD(env))
	if err != nil {
		wipe(dek)
		return nil, ErrWrongPassword
	}
	return &Vault{path: path, dek: dek, env: env, doc: doc}, nil
}

// Document returns a copy of the decrypted content.
func (v *Vault) Document() ([]byte, error) {
	v.mu.Lock()
	defer v.mu.Unlock()
	if v.dek == nil {
		return nil, ErrClosed
	}
	return append([]byte(nil), v.doc...), nil
}

// Save encrypts doc and atomically replaces the file.
func (v *Vault) Save(doc []byte) error {
	v.mu.Lock()
	defer v.mu.Unlock()
	if v.dek == nil {
		return ErrClosed
	}
	return v.write(doc)
}

// ChangePassword re-wraps the data key under a new master password.
func (v *Vault) ChangePassword(newPassword string) error {
	v.mu.Lock()
	defer v.mu.Unlock()
	if v.dek == nil {
		return ErrClosed
	}
	old := v.env
	if err := v.wrapKey(newPassword, DefaultKDF); err != nil {
		v.env = old
		return err
	}
	if err := v.write(v.doc); err != nil {
		v.env = old
		return err
	}
	return nil
}

// ExportKey returns a copy of the data key, for storage behind Touch ID.
func (v *Vault) ExportKey() ([]byte, error) {
	v.mu.Lock()
	defer v.mu.Unlock()
	if v.dek == nil {
		return nil, ErrClosed
	}
	return append([]byte(nil), v.dek...), nil
}

// blobPath maps a blob name to a file next to the vault. Names are restricted
// to [A-Za-z0-9._-] so they cannot escape the blob directory.
func (v *Vault) blobPath(name string) (string, error) {
	if name == "" || len(name) > 128 || strings.HasPrefix(name, ".") {
		return "", errors.New("vault: bad blob name")
	}
	for _, r := range name {
		if !(r >= 'a' && r <= 'z' || r >= 'A' && r <= 'Z' || r >= '0' && r <= '9' || r == '.' || r == '_' || r == '-') {
			return "", errors.New("vault: bad blob name")
		}
	}
	return filepath.Join(filepath.Dir(v.path), "blobs", name+".bin"), nil
}

func blobAAD(name string) []byte { return []byte("burrow/vault/v1/blob|" + name) }

// PutBlob encrypts data with the data key and stores it as a separate file
// (used for device preview images, ADR 0005). The name is bound into the AAD
// so blobs cannot be swapped between devices.
func (v *Vault) PutBlob(name string, data []byte) error {
	v.mu.Lock()
	defer v.mu.Unlock()
	if v.dek == nil {
		return ErrClosed
	}
	p, err := v.blobPath(name)
	if err != nil {
		return err
	}
	s, err := seal(v.dek, data, blobAAD(name))
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(p), 0o700); err != nil {
		return err
	}
	buf := append(append([]byte{}, s.Nonce...), s.CT...)
	tmp := p + ".tmp"
	if err := os.WriteFile(tmp, buf, 0o600); err != nil {
		return err
	}
	return os.Rename(tmp, p)
}

// Blob returns a decrypted blob, or os.ErrNotExist.
func (v *Vault) Blob(name string) ([]byte, error) {
	v.mu.Lock()
	defer v.mu.Unlock()
	if v.dek == nil {
		return nil, ErrClosed
	}
	p, err := v.blobPath(name)
	if err != nil {
		return nil, err
	}
	buf, err := os.ReadFile(p)
	if err != nil {
		return nil, err
	}
	n := chacha20poly1305.NonceSizeX
	if len(buf) < n {
		return nil, ErrCorrupt
	}
	out, err := open(v.dek, sealed{Nonce: buf[:n], CT: buf[n:]}, blobAAD(name))
	if err != nil {
		return nil, ErrCorrupt
	}
	return out, nil
}

// DeleteBlob removes a blob; a missing blob is not an error.
func (v *Vault) DeleteBlob(name string) error {
	p, err := v.blobPath(name)
	if err != nil {
		return err
	}
	if err := os.Remove(p); err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	return nil
}

// Close locks the vault and wipes keys and content from memory (best effort).
func (v *Vault) Close() {
	v.mu.Lock()
	defer v.mu.Unlock()
	wipe(v.dek)
	wipe(v.doc)
	v.dek, v.doc = nil, nil
}

func (v *Vault) wrapKey(password string, p KDFParams) error {
	if password == "" {
		return errors.New("vault: empty password")
	}
	p.Salt = make([]byte, saltSize)
	if _, err := rand.Read(p.Salt); err != nil {
		return err
	}
	kek, err := deriveKEK(password, p)
	if err != nil {
		return err
	}
	defer wipe(kek)
	v.env.KDF = p
	s, err := seal(kek, v.dek, keyAAD(v.env))
	if err != nil {
		return err
	}
	v.env.Key = s
	return nil
}

// write seals doc into the envelope and atomically replaces the file,
// keeping the previous version as path+".bak". Caller holds v.mu (or owns v).
func (v *Vault) write(doc []byte) error {
	s, err := seal(v.dek, doc, dataAAD(v.env))
	if err != nil {
		return err
	}
	env := v.env
	env.Data = s
	buf, err := json.MarshalIndent(env, "", " ")
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(v.path), 0o700); err != nil {
		return err
	}
	tmp, err := os.CreateTemp(filepath.Dir(v.path), ".vault-*")
	if err != nil {
		return err
	}
	defer os.Remove(tmp.Name())
	if err := tmp.Chmod(0o600); err != nil {
		tmp.Close()
		return err
	}
	if _, err := tmp.Write(buf); err != nil {
		tmp.Close()
		return err
	}
	if err := tmp.Sync(); err != nil {
		tmp.Close()
		return err
	}
	if err := tmp.Close(); err != nil {
		return err
	}
	if _, err := os.Stat(v.path); err == nil {
		_ = os.Remove(v.path + ".bak")
		if err := os.Link(v.path, v.path+".bak"); err != nil {
			return fmt.Errorf("vault: backup: %w", err)
		}
	}
	if err := os.Rename(tmp.Name(), v.path); err != nil {
		return err
	}
	v.env = env
	if !bytes.Equal(v.doc, doc) {
		wipe(v.doc)
		v.doc = append([]byte(nil), doc...)
	}
	return nil
}

func readEnvelope(path string) (envelope, error) {
	var env envelope
	buf, err := os.ReadFile(path)
	if err != nil {
		return env, err
	}
	if err := json.Unmarshal(buf, &env); err != nil {
		return env, ErrCorrupt
	}
	if env.V != formatVersion || env.KDF.Alg != "argon2id" || len(env.KDF.Salt) < saltSize ||
		env.KDF.Time == 0 || env.KDF.Memory < 8*1024 || env.KDF.Threads == 0 ||
		env.KDF.Time > 64 || env.KDF.Memory > 4*1024*1024 {
		return env, ErrCorrupt
	}
	return env, nil
}

func deriveKEK(password string, p KDFParams) ([]byte, error) {
	if p.Alg != "argon2id" {
		return nil, ErrCorrupt
	}
	return argon2.IDKey([]byte(password), p.Salt, p.Time, p.Memory, p.Threads, keySize), nil
}

// AADs bind ciphertexts to the format version and KDF parameters.
func keyAAD(env envelope) []byte {
	return []byte(fmt.Sprintf("burrow/vault/v%d/key|%s|%d|%d|%d|%x",
		env.V, env.KDF.Alg, env.KDF.Time, env.KDF.Memory, env.KDF.Threads, env.KDF.Salt))
}

func dataAAD(env envelope) []byte {
	return []byte(fmt.Sprintf("burrow/vault/v%d/data", env.V))
}

func seal(key, plaintext, aad []byte) (sealed, error) {
	aead, err := chacha20poly1305.NewX(key)
	if err != nil {
		return sealed{}, err
	}
	nonce := make([]byte, aead.NonceSize())
	if _, err := rand.Read(nonce); err != nil {
		return sealed{}, err
	}
	return sealed{Nonce: nonce, CT: aead.Seal(nil, nonce, plaintext, aad)}, nil
}

func open(key []byte, s sealed, aad []byte) ([]byte, error) {
	aead, err := chacha20poly1305.NewX(key)
	if err != nil {
		return nil, err
	}
	if len(s.Nonce) != aead.NonceSize() {
		return nil, ErrCorrupt
	}
	return aead.Open(nil, s.Nonce, s.CT, aad)
}

func wipe(b []byte) {
	for i := range b {
		b[i] = 0
	}
}
