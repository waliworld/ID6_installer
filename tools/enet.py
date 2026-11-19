#!/usr/bin/env python3
"""
enet.py - ENET car discovery (Python port of the Go enet.go broadcast).

Finds the BMW diagnostic gateway and head unit on the direct ENET link:
UDP broadcast 00 00 00 00 00 11 -> 169.254.255.255:6811; replies carry a
DIAGADR tag (10 = gateway, 62/63 = head unit). Pure stdlib so it runs inside
the packaged app with no dependencies.
"""
from __future__ import annotations
import ipaddress
import re
import socket
import subprocess
import os

TAG = re.compile(rb"DIAGADR(10|62|63)")


def _usable(ip: str) -> bool:
    try:
        a = ipaddress.IPv4Address(ip)
    except ValueError:
        return False
    o = a.packed
    return o[0] == 169 and o[1] == 254 and o[2] not in (0, 255)


def local_link_addrs() -> list[str]:
    """Every local 169.254.x.x address (the ENET adapters)."""
    found: set[str] = set()
    # Primary: ask the OS.
    try:
        if os.name == "nt":
            out = subprocess.run(["ipconfig"], capture_output=True, text=True, timeout=8).stdout
        else:
            cmd = ["ip", "-4", "addr"] if _which("ip") else ["ifconfig"]
            out = subprocess.run(cmd, capture_output=True, text=True, timeout=8).stdout
        for m in re.findall(r"169\.254\.\d{1,3}\.\d{1,3}", out or ""):
            if _usable(m):
                found.add(m)
    except (OSError, subprocess.SubprocessError):
        pass
    return sorted(found)


def _which(name: str) -> bool:
    from shutil import which
    return which(name) is not None


def broadcast(local: str, timeout: float = 1.8) -> tuple[list[str], list[str]]:
    gateways: list[str] = []
    heads: list[str] = []
    seen: set[str] = set()
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        try:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        except OSError:
            pass
        s.bind((local, 0))
        s.settimeout(timeout)
        s.sendto(b"\x00\x00\x00\x00\x00\x11", ("169.254.255.255", 6811))
        while True:
            try:
                data, addr = s.recvfrom(4096)
            except socket.timeout:
                break
            except OSError:
                break
            ip = addr[0]
            if len(data) < 6 or data[4] != 0 or data[5] != 0x11:
                continue
            if not _usable(ip) or ip == local or ip in seen:
                continue
            m = TAG.search(data[6:])
            if not m:
                continue
            seen.add(ip)
            (gateways if m.group(1) == b"10" else heads).append(ip)
    except OSError:
        pass
    finally:
        try:
            s.close()
        except Exception:
            pass
    return gateways, heads


def discover(_body: dict | None = None):
    """Return a list of {Gateway, Head, Local} endpoints, or {Result: ...}."""
    locals_ = local_link_addrs()
    if not locals_:
        return {"Result": "No 169.254.x.x network adapter found. Plug in the ENET cable "
                          "(a USB-Ethernet adapter is fine) and wait for a link-local address, "
                          "or type both IPs manually."}
    results = []
    for local in locals_:
        gws, heads = broadcast(local)
        for gw in gws:
            head = heads[0] if len(gws) == 1 and len(heads) == 1 else ""
            results.append({"Gateway": gw, "Head": head, "Local": local})
    # de-dup, stable order
    uniq = {(r["Gateway"], r["Local"]): r for r in results}
    results = [uniq[k] for k in sorted(uniq)]
    if not results:
        return {"Result": "No ENET gateway replied. Check the cable, that the ignition is on and "
                          "iDrive is awake, and your firewall — or type both IPs manually."}
    return results


if __name__ == "__main__":
    import json
    import sys
    print(json.dumps(discover(json.loads(sys.stdin.read() or "{}"))))
