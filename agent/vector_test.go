package agent

import "testing"

// Fixed values that Burrow's C++ side (AgentLink, tst_vault agentProtocol) checks too, so the two
// implementations can't drift apart.
func TestSharedVector(t *testing.T) {
	key := make([]byte, KeySize)
	for i := range key {
		key[i] = byte(i)
	}
	ch := "00112233445566778899aabbccddeeff"
	if PairingCode(key) != vectorCode || RequestMAC(key, CmdStart, ch) != vectorRequest ||
		ReplyMAC(key, CmdStart, ch, true) != vectorReply {
		t.Fatal("shared test vector changed; update app/tests/tst_vault.cpp too")
	}
}

const (
	vectorCode    = "AAAQ-EAYE-AUDA-OCAJ-BIFQ-YDIO-B4IB-CEQT-CQKR-MFYY-DENB-WHA5-DYPQ"
	vectorRequest = "7c6812ec4944545f6ffb049854f474a54a3df34075e15adf094493e904a6a30a"
	vectorReply   = "84d0e3a8d3e7c631e7b73c6962b41faa979564b4520cfda7598c6a02aa80e9b2"
)
