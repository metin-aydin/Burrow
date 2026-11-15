package vault

import (
	"bytes"
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"runtime"
	"testing"
)

func init() {
	// Keep tests fast; production parameters are exercised by TestDefaultKDFCost.
	DefaultKDF = KDFParams{Alg: "argon2id", Time: 1, Memory: 8 * 1024, Threads: 1}
}

func newPath(t *testing.T) string { return filepath.Join(t.TempDir(), "sub", "vault.json") }

func TestCreateOpenRoundTrip(t *testing.T) {
	p := newPath(t)
	v, err := Create(p, "correct horse", []byte(`{"devices":[]}`))
	if err != nil {
		t.Fatal(err)
	}
	if err := v.Save([]byte(`{"devices":[1]}`)); err != nil {
		t.Fatal(err)
	}
	v.Close()
	if _, err := v.Document(); !errors.Is(err, ErrClosed) {
		t.Fatalf("closed vault returned %v", err)
	}

	v2, err := Open(p, "correct horse")
	if err != nil {
		t.Fatal(err)
	}
	doc, _ := v2.Document()
	if string(doc) != `{"devices":[1]}` {
		t.Fatalf("doc = %s", doc)
	}
	st, _ := os.Stat(p)
	// Windows keeps only the read-only bit; the file's ACL (user profile) is what protects it there.
	if runtime.GOOS != "windows" && st.Mode().Perm() != 0o600 {
		t.Fatalf("perm = %v", st.Mode().Perm())
	}
	if _, err := os.Stat(p + ".bak"); err != nil {
		t.Fatalf("backup missing: %v", err)
	}
}

func TestWrongPassword(t *testing.T) {
	p := newPath(t)
	if _, err := Create(p, "a", []byte("x")); err != nil {
		t.Fatal(err)
	}
	if _, err := Open(p, "b"); !errors.Is(err, ErrWrongPassword) {
		t.Fatalf("got %v", err)
	}
}

func TestCreateRefusesExisting(t *testing.T) {
	p := newPath(t)
	if _, err := Create(p, "a", nil); err != nil {
		t.Fatal(err)
	}
	if _, err := Create(p, "a", nil); !errors.Is(err, ErrExists) {
		t.Fatalf("got %v", err)
	}
}

func TestChangePasswordAndExportKey(t *testing.T) {
	p := newPath(t)
	v, _ := Create(p, "old", []byte("secret doc"))
	key, _ := v.ExportKey()
	if err := v.ChangePassword("new"); err != nil {
		t.Fatal(err)
	}
	if _, err := Open(p, "old"); !errors.Is(err, ErrWrongPassword) {
		t.Fatalf("old password still works: %v", err)
	}
	if _, err := Open(p, "new"); err != nil {
		t.Fatal(err)
	}
	// The data key is rotated: an old exported key (Touch ID copy) no longer works.
	if _, err := OpenWithKey(p, key); !errors.Is(err, ErrWrongPassword) {
		t.Fatalf("old data key still opens the vault: %v", err)
	}
	newKey, _ := v.ExportKey()
	v3, err := OpenWithKey(p, newKey)
	if err != nil {
		t.Fatal(err)
	}
	doc, _ := v3.Document()
	if string(doc) != "secret doc" {
		t.Fatalf("doc = %s", doc)
	}
	bad := make([]byte, len(key))
	if _, err := OpenWithKey(p, bad); !errors.Is(err, ErrWrongPassword) {
		t.Fatalf("got %v", err)
	}
}

func TestTamperDetected(t *testing.T) {
	p := newPath(t)
	if _, err := Create(p, "pw", []byte("hello")); err != nil {
		t.Fatal(err)
	}
	var env envelope
	buf, _ := os.ReadFile(p)
	_ = json.Unmarshal(buf, &env)

	// Flip a ciphertext bit.
	env.Data.CT[0] ^= 1
	writeEnv(t, p, env)
	if _, err := Open(p, "pw"); !errors.Is(err, ErrWrongPassword) {
		t.Fatalf("tampered data accepted: %v", err)
	}
	env.Data.CT[0] ^= 1

	// Lowering KDF cost must break the key unwrap (params are in the AAD).
	env.KDF.Time++
	writeEnv(t, p, env)
	if _, err := Open(p, "pw"); !errors.Is(err, ErrWrongPassword) {
		t.Fatalf("modified KDF params accepted: %v", err)
	}

	os.WriteFile(p, []byte("not json"), 0o600)
	if _, err := Open(p, "pw"); !errors.Is(err, ErrCorrupt) {
		t.Fatalf("got %v", err)
	}
}

func TestPlaintextNotInFile(t *testing.T) {
	p := newPath(t)
	if _, err := Create(p, "pw", []byte("VERY-SECRET-PASSWORD-123")); err != nil {
		t.Fatal(err)
	}
	buf, _ := os.ReadFile(p)
	if bytes.Contains(buf, []byte("VERY-SECRET")) {
		t.Fatal("plaintext found in vault file")
	}
}

func TestDefaultKDFCost(t *testing.T) {
	prod := KDFParams{Alg: "argon2id", Time: 3, Memory: 64 * 1024, Threads: 4, Salt: make([]byte, 16)}
	if testing.Short() {
		t.Skip("slow")
	}
	if _, err := deriveKEK("x", prod); err != nil {
		t.Fatal(err)
	}
}

func writeEnv(t *testing.T, p string, env envelope) {
	t.Helper()
	buf, _ := json.Marshal(env)
	if err := os.WriteFile(p, buf, 0o600); err != nil {
		t.Fatal(err)
	}
}

func TestBlobs(t *testing.T) {
	p := newPath(t)
	v, _ := Create(p, "pw", []byte("{}"))
	img := []byte("JPEG-PIXELS-SECRET")
	if err := v.PutBlob("dev-1", img); err != nil {
		t.Fatal(err)
	}
	raw, _ := os.ReadFile(filepath.Join(filepath.Dir(p), "blobs", "dev-1.bin"))
	if bytes.Contains(raw, []byte("SECRET")) {
		t.Fatal("blob stored in plaintext")
	}
	got, err := v.Blob("dev-1")
	if err != nil || !bytes.Equal(got, img) {
		t.Fatalf("got %q %v", got, err)
	}
	// Swapping files between names must fail (name is in the AAD).
	os.WriteFile(filepath.Join(filepath.Dir(p), "blobs", "dev-2.bin"), raw, 0o600)
	if _, err := v.Blob("dev-2"); !errors.Is(err, ErrCorrupt) {
		t.Fatalf("swapped blob accepted: %v", err)
	}
	for _, bad := range []string{"../x", "a/b", ".hidden", ""} {
		if err := v.PutBlob(bad, img); err == nil {
			t.Fatalf("bad name %q accepted", bad)
		}
	}
	if err := v.DeleteBlob("dev-1"); err != nil {
		t.Fatal(err)
	}
	if _, err := v.Blob("dev-1"); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("got %v", err)
	}
	v.Close()
	if _, err := v.Blob("dev-2"); !errors.Is(err, ErrClosed) {
		t.Fatalf("got %v", err)
	}
}

func TestChangePasswordRotatesKeyAndDropsBackup(t *testing.T) {
	p := newPath(t)
	v, _ := Create(p, "old", []byte("v1"))
	if err := v.Save([]byte("v2")); err != nil { // creates .bak
		t.Fatal(err)
	}
	if err := v.PutBlob("preview-a", []byte("pixels")); err != nil {
		t.Fatal(err)
	}
	if err := v.ChangePassword("new"); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(p + ".bak"); !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("backup wrapped under the old password still exists: %v", err)
	}
	// Blobs were re-encrypted under the new key.
	v.Close()
	v2, err := Open(p, "new")
	if err != nil {
		t.Fatal(err)
	}
	b, err := v2.Blob("preview-a")
	if err != nil || string(b) != "pixels" {
		t.Fatalf("blob after rotation: %q %v", b, err)
	}
	// Next save makes a fresh backup, wrapped under the new password.
	if err := v2.Save([]byte("v3")); err != nil {
		t.Fatal(err)
	}
	if _, err := Open(p+".bak", "old"); !errors.Is(err, ErrWrongPassword) {
		t.Fatalf("old password opens the new backup: %v", err)
	}
}
