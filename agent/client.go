package agent

import (
	"bufio"
	"crypto/hmac"
	"encoding/json"
	"errors"
	"io"
	"net"
	"time"
)

var ErrBadReply = errors.New("the agent's reply isn't signed with this pairing key")

// Send sends one command over c and checks the signed reply (Burrow does the same in C++, AgentLink;
// this one serves the tests).
func Send(c net.Conn, key []byte, cmd string) error {
	_ = c.SetDeadline(time.Now().Add(10 * time.Second))
	in := bufio.NewReader(io.LimitReader(c, 4096))
	line, err := in.ReadBytes('\n')
	var ch Challenge
	if err != nil || json.Unmarshal(line, &ch) != nil || len(ch.Challenge) != 32 {
		return errors.New("no burrow-agent at this address")
	}
	b, _ := json.Marshal(NewRequest(key, cmd, ch.Challenge))
	if _, err := c.Write(append(b, '\n')); err != nil {
		return err
	}
	line, err = in.ReadBytes('\n')
	if err != nil {
		return errors.New("no reply from the agent (wrong pairing code?)")
	}
	var r Reply
	if json.Unmarshal(line, &r) != nil || !hmac.Equal([]byte(r.MAC), []byte(ReplyMAC(key, cmd, ch.Challenge, r.OK))) {
		return ErrBadReply
	}
	if !r.OK {
		return errors.New(r.Error)
	}
	return nil
}
