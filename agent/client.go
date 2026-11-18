package agent

import (
	"bufio"
	"encoding/json"
	"errors"
	"net"
	"time"
)

var ErrBadReply = errors.New("the agent's reply isn't signed with this pairing key")

// Send sends one command over c and checks the signed reply (Burrow does the same in C++; this one
// serves the tests and `burrow-agent test`).
func Send(c net.Conn, key []byte, cmd string) error {
	_ = c.SetDeadline(time.Now().Add(10 * time.Second))
	req := NewRequest(key, cmd, time.Now())
	b, _ := json.Marshal(req)
	if _, err := c.Write(append(b, '\n')); err != nil {
		return err
	}
	line, err := bufio.NewReader(c).ReadBytes('\n')
	if err != nil {
		return errors.New("no reply from the agent (wrong pairing code?)")
	}
	var r Reply
	if json.Unmarshal(line, &r) != nil || r.MAC != ReplyMAC(key, cmd, req.Nonce, r.OK) {
		return ErrBadReply
	}
	if !r.OK {
		return errors.New(r.Error)
	}
	return nil
}
