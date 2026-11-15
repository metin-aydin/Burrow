// Package netstack runs a userspace TCP/IP stack (gVisor) on top of a packet socket, so a VPN
// client that normally writes to a tun device can be used without touching the system network.
package netstack

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"net"
	"net/netip"
	"os"
	"strconv"
	"sync"

	"gvisor.dev/gvisor/pkg/buffer"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/adapters/gonet"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/link/channel"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
)

const nicID = 1

// Stack is a userspace IP stack whose "wire" carries one raw IP packet per read or write: a datagram
// socket on Unix, a message-mode named pipe on Windows.
type Stack struct {
	s        *stack.Stack
	ep       *channel.Endpoint
	file     io.ReadWriteCloser
	dns      []netip.Addr
	afPrefix bool
	cancel   context.CancelFunc
	wg       sync.WaitGroup
}

// Options tune how packets are framed on the socket.
type Options struct {
	// AFPrefix: each packet carries a 4-byte address-family header (BSD/macOS tun framing, which
	// openconnect uses on those systems).
	AFPrefix bool
}

// NewPacketConn starts the stack on wire (one raw IP packet per Read/Write; it must unblock pending
// reads with os.ErrClosed or io.EOF when closed), with this side's VPN address and the VPN's DNS
// servers. The stack owns wire and closes it.
func NewPacketConn(wire io.ReadWriteCloser, mtu int, local netip.Addr, dns []netip.Addr, opt Options) (*Stack, error) {
	if mtu <= 0 {
		mtu = 1300
	}
	s := stack.New(stack.Options{
		NetworkProtocols:   []stack.NetworkProtocolFactory{ipv4.NewProtocol, ipv6.NewProtocol},
		TransportProtocols: []stack.TransportProtocolFactory{tcp.NewProtocol, udp.NewProtocol},
	})
	ep := channel.New(512, uint32(mtu), "")
	if err := s.CreateNIC(nicID, ep); err != nil {
		return nil, fmt.Errorf("netstack: create NIC: %v", err)
	}
	proto := ipv4.ProtocolNumber
	if local.Is6() {
		proto = ipv6.ProtocolNumber
	}
	if err := s.AddProtocolAddress(nicID, tcpip.ProtocolAddress{
		Protocol:          proto,
		AddressWithPrefix: tcpip.AddrFromSlice(local.AsSlice()).WithPrefix(),
	}, stack.AddressProperties{}); err != nil {
		return nil, fmt.Errorf("netstack: add address: %v", err)
	}
	s.SetRouteTable([]tcpip.Route{
		{Destination: header.IPv4EmptySubnet, NIC: nicID},
		{Destination: header.IPv6EmptySubnet, NIC: nicID},
	})
	ns := &Stack{s: s, ep: ep, file: wire, dns: dns, afPrefix: opt.AFPrefix}
	ctx, cancel := context.WithCancel(context.Background())
	ns.cancel = cancel
	ns.wg.Add(2)
	go ns.inbound(mtu)
	go ns.outbound(ctx)
	return ns, nil
}

// inbound: packets from the VPN into the stack.
func (ns *Stack) inbound(mtu int) {
	defer ns.wg.Done()
	buf := make([]byte, mtu+64)
	for {
		n, err := ns.file.Read(buf)
		if errors.Is(err, os.ErrClosed) || errors.Is(err, io.EOF) {
			return
		}
		if err != nil {
			continue
		}
		pktData := buf[:n]
		if ns.afPrefix {
			if n <= 4 {
				continue
			}
			pktData = buf[4:n]
		}
		if len(pktData) == 0 {
			continue
		}
		var proto tcpip.NetworkProtocolNumber
		switch pktData[0] >> 4 {
		case 4:
			proto = ipv4.ProtocolNumber
		case 6:
			proto = ipv6.ProtocolNumber
		default:
			continue
		}
		pkt := stack.NewPacketBuffer(stack.PacketBufferOptions{
			Payload: buffer.MakeWithData(append([]byte(nil), pktData...)),
		})
		ns.ep.InjectInbound(proto, pkt)
		pkt.DecRef()
	}
}

// outbound: packets from the stack to the VPN.
func (ns *Stack) outbound(ctx context.Context) {
	defer ns.wg.Done()
	for {
		pkt := ns.ep.ReadContext(ctx)
		if pkt == nil {
			return
		}
		view := pkt.ToView()
		data := view.AsSlice()
		if ns.afPrefix && len(data) > 0 {
			af := uint32(afInet)
			if data[0]>>4 == 6 {
				af = uint32(afInet6)
			}
			framed := make([]byte, 4+len(data))
			binary.BigEndian.PutUint32(framed, af)
			copy(framed[4:], data)
			data = framed
		}
		_, err := ns.file.Write(data)
		view.Release()
		pkt.DecRef()
		if errors.Is(err, os.ErrClosed) {
			return
		}
		// Other errors (ENOBUFS under bursts) drop the packet; TCP retransmits it.
	}
}

// DialContext dials TCP through the VPN; host names are resolved with the VPN's DNS servers.
func (ns *Stack) DialContext(ctx context.Context, network, address string) (net.Conn, error) {
	if network != "tcp" && network != "tcp4" && network != "tcp6" {
		return nil, errors.New("netstack: only TCP is supported")
	}
	host, portStr, err := net.SplitHostPort(address)
	if err != nil {
		return nil, err
	}
	// Parse straight into 16 bits so an out-of-range port is an error instead of wrapping around.
	port, err := strconv.ParseUint(portStr, 10, 16)
	if err != nil {
		return nil, fmt.Errorf("netstack: invalid port %q", portStr)
	}
	ip, err := netip.ParseAddr(host)
	if err != nil {
		ip, err = ns.resolve(ctx, host)
		if err != nil {
			return nil, err
		}
	}
	proto := ipv4.ProtocolNumber
	if ip.Is6() {
		proto = ipv6.ProtocolNumber
	}
	return gonet.DialContextTCP(ctx, ns.s, tcpip.FullAddress{
		NIC: nicID, Addr: tcpip.AddrFromSlice(ip.AsSlice()), Port: uint16(port),
	}, proto)
}

func (ns *Stack) resolve(ctx context.Context, host string) (netip.Addr, error) {
	if len(ns.dns) == 0 {
		return netip.Addr{}, fmt.Errorf("netstack: no DNS server to resolve %q", host)
	}
	var lastErr error
	for _, server := range ns.dns {
		server := server
		r := &net.Resolver{PreferGo: true, Dial: func(ctx context.Context, _, _ string) (net.Conn, error) {
			proto := ipv4.ProtocolNumber
			if server.Is6() {
				proto = ipv6.ProtocolNumber
			}
			return gonet.DialUDP(ns.s, nil, &tcpip.FullAddress{
				NIC: nicID, Addr: tcpip.AddrFromSlice(server.AsSlice()), Port: 53,
			}, proto)
		}}
		addrs, err := r.LookupNetIP(ctx, "ip4", host)
		if err == nil && len(addrs) > 0 {
			return addrs[0], nil
		}
		lastErr = err
	}
	return netip.Addr{}, fmt.Errorf("netstack: resolve %q: %v", host, lastErr)
}

func (ns *Stack) Close() error {
	ns.cancel()
	err := ns.file.Close()
	ns.ep.Close()
	ns.s.Close()
	ns.wg.Wait()
	return err
}
