#!/usr/bin/env python3
"""
GlobalProtect userspace tunnel test (no sudo, no system VPN).

Runs openconnect with --script-tun + ocproxy so only a local port forward goes
through the VPN, mirroring Burrow's architecture (ADR 0001). Credentials are
read from the macOS Keychain and OTPs are generated with TOTP, so no secrets
appear on the command line or in this file.

Keychain entries expected (service name configurable):
  security add-generic-password -s <service> -a password    -w
  security add-generic-password -s <service> -a otp-secret  -w

Usage:
  tools/gp-userspace-test.py --server vpn.example.com --user alice \
      --authgroup EXT_GW --keychain-service my-vpn --target 10.0.0.5:3389
  then connect an RDP client to localhost:13389. Stop with Ctrl+C.

Requires: openconnect, ocproxy (brew install openconnect ocproxy), pyotp.
"""
import argparse
import hashlib
import re
import shutil
import signal
import subprocess
import sys
import threading
import time

import pyotp


def keychain_get(service, account):
    try:
        return subprocess.run(
            ["security", "find-generic-password", "-s", service, "-a", account, "-w"],
            capture_output=True, text=True, check=True).stdout.strip()
    except subprocess.CalledProcessError:
        sys.exit(f"Keychain entry not found: service={service} account={account}")


def public_resolve(host):
    """Resolve via public DNS; split-DNS (/etc/resolver) may hide the VPN host."""
    for ns in ("1.1.1.1", "8.8.8.8"):
        try:
            out = subprocess.run(["dig", "+short", "+time=3", "+tries=1", host, "A", "@" + ns],
                                 capture_output=True, text=True, timeout=10).stdout.split()
        except (subprocess.SubprocessError, FileNotFoundError):
            continue
        ips = [x for x in out if re.fullmatch(r"\d+\.\d+\.\d+\.\d+", x)]
        if ips:
            return ips[0]
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", required=True)
    ap.add_argument("--user", required=True)
    ap.add_argument("--authgroup", help="GP gateway/auth group, e.g. EXT_GW")
    ap.add_argument("--gateway-only", action="store_true",
                    help="Skip the portal (--usergroup=gateway) to test single-OTP login")
    ap.add_argument("--keychain-service", required=True)
    ap.add_argument("--target", required=True, help="Remote RDP host:port behind the VPN")
    ap.add_argument("--local-port", type=int, default=13389)
    ap.add_argument("--also", action="append", default=[], metavar="LPORT:HOST:PORT",
                    help="Extra port forward through the tunnel, e.g. 2222:10.0.0.5:22 (repeatable)")
    ap.add_argument("--otp-digest", choices=["sha1", "sha256", "sha512"], required=True,
                    help="TOTP hash algorithm; no default on purpose (a wrong guess locks the account)")
    ap.add_argument("--otp-digits", type=int, default=6)
    ap.add_argument("--otp-period", type=int, default=30)
    ap.add_argument("--show-otp", action="store_true",
                    help="Print the current OTP and exit (no network); compare it with the authenticator app")
    ap.add_argument("--auth-only", action="store_true",
                    help="Only authenticate (no tunnel); prints result without the cookie")
    args = ap.parse_args()

    if args.show_otp:
        totp = pyotp.TOTP(keychain_get(args.keychain_service, "otp-secret"),
                          digits=args.otp_digits, interval=args.otp_period,
                          digest=getattr(hashlib, args.otp_digest))
        left = args.otp_period - int(time.time()) % args.otp_period
        print(f"[test] current OTP: {totp.now()} ({left}s left) - must match the authenticator app")
        return

    for tool in ("openconnect", "ocproxy"):
        if not shutil.which(tool):
            sys.exit(f"{tool} not found in PATH")

    password = keychain_get(args.keychain_service, "password")
    totp = pyotp.TOTP(keychain_get(args.keychain_service, "otp-secret"),
                      digits=args.otp_digits, interval=args.otp_period,
                      digest=getattr(hashlib, args.otp_digest))

    cmd = ["openconnect", "--protocol=gp", "--user", args.user, "--passwd-on-stdin", "--verbose"]
    if args.authgroup:
        cmd.append(f"--authgroup={args.authgroup}")
    if args.gateway_only:
        cmd.append("--usergroup=gateway")
    ip = public_resolve(args.server)
    if ip:
        print(f"[test] {args.server} -> {ip} (public DNS)")
        cmd.append(f"--resolve={args.server}:{ip}")
    if args.auth_only:
        cmd.append("--authenticate")
    else:
        cmd += ["--script-tun", "--script", " ".join(["ocproxy", "-L", f"{args.local_port}:{args.target}"]
                                                 + [x for a in args.also for x in ("-L", a)])]
    cmd.append(args.server)

    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True, bufsize=1,
                         start_new_session=True)
    last = {"otp": None, "prompts": 0, "portal_ok": False, "gw_failures": 0}

    def send_credentials():
        code = totp.now()
        if code == last["otp"]:
            print("[test] waiting for the next OTP window ...")
            while code == last["otp"]:
                time.sleep(1)
                code = totp.now()
        last["otp"] = code
        last["prompts"] += 1
        p.stdin.write(password + code + "\n")
        p.stdin.flush()

    def stop(msg):
        print(f"[test] {msg}; stopping to avoid account lockout")
        p.send_signal(signal.SIGINT)

    def reader():
        # Expected flow (double OTP): portal accepts password+OTP, openconnect then
        # reuses the same OTP for the gateway, which rejects it once (auth-failed),
        # and prompts again; the queued password + NEXT OTP answers it.
        # Any other auth-failed (portal, or a second gateway failure) stops at once.
        for line in p.stdout:
            s = line.rstrip()
            if "cookie" in s.lower():
                s = "[cookie line hidden]"
            print("VPN:", s)
            if "gateway servers available" in s or "Portal reports GlobalProtect version" in s:
                last["portal_ok"] = True
            if "auth-failed" in s:
                if not last["portal_ok"]:
                    return stop("portal rejected the login (password or OTP wrong)")
                last["gw_failures"] += 1
                if last["gw_failures"] > 1:
                    return stop("gateway rejected the login twice")
                print("[test] gateway rejected the reused OTP (expected); waiting for the re-prompt")
            if "Enter login credentials" in s:
                send_credentials()
            if "ESP session established" in s or "Connected as" in s or "Configured as" in s:
                if not args.auth_only:
                    print(f"[test] tunnel up -> connect RDP client to localhost:{args.local_port}")

    # openconnect's stdout is block-buffered on a pipe, so its first prompt is not
    # visible before it blocks on stdin: send the first credentials up front (same as
    # the working desktop script). Each visible prompt then queues password + the NEXT
    # OTP, which openconnect consumes at the gateway re-prompt.
    send_credentials()
    t = threading.Thread(target=reader, daemon=True)
    t.start()
    try:
        p.wait()
    except KeyboardInterrupt:
        p.send_signal(signal.SIGINT)
        try:
            p.wait(timeout=10)
        except subprocess.TimeoutExpired:
            p.kill()
    t.join(timeout=2)
    print(f"[test] openconnect exited with code {p.returncode}; credentials sent {last['prompts']} time(s)")


if __name__ == "__main__":
    main()
