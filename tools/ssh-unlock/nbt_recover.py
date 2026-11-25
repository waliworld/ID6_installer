#!/usr/bin/env python3
"""
nbt_recover.py -- diagnose (and optionally un-stick) an NBT Evo head unit that is
DARK after an ABORTED OPT_DEBUG flash.

WHAT THIS TOOL DOES NOT DO
  * It NEVER writes to the head unit's filesystem.
  * It loads NO flash containers. No requestDownload, no transferData, no transferExit.
  * It takes NO SecurityAccess / L3 handshake.
  * Default mode is READ-ONLY -- it only asks the unit questions.

WHY IT EXISTS
  The OPT_DEBUG flasher must clear EnergyMode before it resets the HU. Its own
  teardown note: "leaving the HU in EnergyMode=Flash is a guaranteed black screen."
  When the teardown logs

      *** NOT clean (energy=!=00 ext=00 state=0x03) -- forcing reset to exit flash mode. ***

  the unit may still be latched in a flash/programming energy state, in which case it
  answers diagnostics but never starts its HMI -> black screen that survives sleep.

  This tool reads that state back. With --clear it re-runs the SAME tested teardown
  from the flasher (ActivateApplicationMode -> clear EnergyMode -> clear ExtendedMode
  -> CommControl normal -> DTC on -> ECUReset). Nothing else.

USAGE
  python nbt_recover.py                      # READ-ONLY report  <-- do this first
  python nbt_recover.py --clear              # read, then run the teardown + reset
  python nbt_recover.py --host 169.254.1.2   # skip 6811 discovery
"""

import argparse
import importlib.util
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
FLASHER = os.path.join(HERE, "nbt_optdebug_flash.py")

# ------------------------------------------------------------------ load the flasher
# Imported for its TESTED primitives only (HSFZ transport, discovery, the UDS
# constants and the teardown sequence). Importing runs no flash code: the module
# guards its CLI behind __name__ == "__main__".


def load_flasher():
    if not os.path.isfile(FLASHER):
        sys.exit("ERROR: cannot find %s -- run this from the tools/ssh-unlock folder." % FLASHER)
    spec = importlib.util.spec_from_file_location("nbt_optdebug_flash", FLASHER)
    mod = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(mod)
    except Exception as e:
        sys.exit("ERROR: could not load the flasher module: %s" % e)
    return mod


# ------------------------------------------------------------------ raw helpers
# _guard() in the flasher refuses anything outside its flash allowlist, so the extra
# DIAGNOSTIC reads below go through h.request() directly -- the same way the flasher's
# own read_vin() does. Reads are free; nothing here changes unit state.

NRC_NAMES = {
    0x11: "serviceNotSupported",
    0x12: "subFunctionNotSupported",
    0x13: "incorrectMessageLength",
    0x22: "conditionsNotCorrect",
    0x31: "requestOutOfRange",
    0x33: "securityAccessDenied",
    0x7E: "subFuncNotSupportedInActiveSession",
    0x7F: "serviceNotSupportedInActiveSession",
}


def raw(h, mod, uds_hex, label, log):
    """Send one raw UDS request, bypassing the flash allowlist. Reads only."""
    try:
        r = h.request(mod.HU, bytes.fromhex(uds_hex))
    except Exception as e:
        log("  %-22s -> ERR %s" % (label, e))
        return None
    if r is None:
        log("  %-22s -> (no response)" % label)
        return None
    if mod.is_neg(r):
        nrc = r[2] if len(r) > 2 else 0
        log("  %-22s -> NRC 0x%02X %s" % (label, nrc, NRC_NAMES.get(nrc, "?")))
        return None
    log("  %-22s -> %s" % (label, r[:16].hex()))
    return r


def ascii_runs(b, minlen=4):
    """Pull printable runs out of a blob -- version strings live in the SVK."""
    out, cur = [], []
    for ch in b:
        if 32 <= ch < 127:
            cur.append(chr(ch))
        else:
            if len(cur) >= minlen:
                out.append("".join(cur))
            cur = []
    if len(cur) >= minlen:
        out.append("".join(cur))
    return out


def parse_dtcs(r):
    """59 02 <availabilityMask> then N x (3-byte DTC + 1-byte status)."""
    if not r or len(r) < 4 or r[0] != 0x59:
        return []
    body = r[3:]
    return [
        ("%02X%02X%02X" % (body[i], body[i + 1], body[i + 2]), body[i + 3])
        for i in range(0, len(body) - 3, 4)
    ]


# ------------------------------------------------------------------ main
def main():
    ap = argparse.ArgumentParser(
        description="Diagnose / un-stick a dark NBT Evo HU after an aborted OPT_DEBUG flash.",
    )
    ap.add_argument("--host", default=None, help="gateway HSFZ host (default: 6811 auto-discover)")
    ap.add_argument("--port", type=int, default=None, help="HSFZ port (default 6801)")
    ap.add_argument("--timeout", type=float, default=6.0)
    ap.add_argument(
        "--clear",
        action="store_true",
        help="after the report, run the flasher's tested teardown + reset "
             "(clears a latched EnergyMode/ExtendedMode). Still writes NO files.",
    )
    a = ap.parse_args()

    log = print
    mod = load_flasher()
    port = a.port if a.port is not None else mod.HSFZ_PORT

    print("=" * 72)
    print("NBT Evo recovery / diagnostic  --  READ-ONLY" + ("  + TEARDOWN (--clear)" if a.clear else ""))
    print("This tool writes NOTHING to the head unit's filesystem.")
    print("=" * 72)

    # ---- locate the gateway
    host, disc_vin = a.host, None
    if not host:
        print("\n[1] ENET discovery (UDP 6811 broadcast) ...")
        try:
            host, disc_vin = mod.discover(timeout=4.0)
        except Exception as e:
            print("  discovery error: %s" % e)
        if host:
            print("  gateway %s   VIN %s" % (host, disc_vin or "?"))
        else:
            host = mod.DIAG_EDGE
            print("  no 6811 reply -- falling back to %s" % host)
            print("  (if this fails: check the ENET cable, and that the laptop NIC has a")
            print("   169.254.x.x link-local address on the cable, not on Wi-Fi)")
    else:
        print("\n[1] using --host %s" % host)

    # ---- connect
    print("\n[2] HSFZ connect %s:%d ..." % (host, port))
    try:
        h = mod.HSFZ(host, port=port, timeout=a.timeout).open()
    except Exception as e:
        print("  FAILED: %s" % e)
        print("\nVERDICT: cannot reach the car's gateway at all.")
        print("  This is a connection problem, not (yet) a head-unit verdict.")
        print("  Check: ENET cable seated, ignition ON, laptop NIC link-local, firewall off.")
        return 2
    print("  connected.")

    # ---- reads
    print("\n[3] identity")
    vin = None
    try:
        vin = mod.read_vin(h)
    except Exception:
        pass
    print("  VIN (22 F190)          -> %s" % (vin or "no answer"))

    svk = raw(h, mod, "22F101", "SVK (22 F101)", log)
    if svk:
        strings = ascii_runs(svk[3:])
        if strings:
            print("    ascii in SVK: %s" % ", ".join(strings[:12]))
        print("    full SVK hex: %s" % svk[3:].hex())
        print("    ^ this is the software-line evidence. The bundled OPT_DEBUG")
        print("      starter.cfg fits the 007.xxx family ONLY.")

    print("\n[4] flash-state registers  (the black-screen suspects)")
    print("  entering extended session so the state DIDs answer:")
    mod._gtx(h, mod.EXT_SESSION, "10 03 ExtendedSession", log)

    en = mod._val(mod._gtx(h, mod.READ_ENERGY, "22 100A EnergyMode", log))
    ext = mod._val(mod._gtx(h, mod.READ_EXTMODE, "22 100E ExtendedMode", log))
    st = mod._read_state(h, "22 F186 (state)", log)

    def fmt(v):
        return "no answer" if v is None else "0x%02X" % v

    print("\n  EnergyMode    = %s   (want 0x00; 0x03 == FLASH MODE LATCHED)" % fmt(en))
    print("  ExtendedMode  = %s   (want 0x00)" % fmt(ext))
    print("  State (F186)  = %s   (0x01 app / 0x02 bootloader / 0x03 flash transition)" % fmt(st))

    print("\n[5] fault memory")
    dtc = raw(h, mod, "1902FF", "DTCs (19 02 FF)", log)
    if dtc is None:
        dtc = raw(h, mod, "190209", "DTCs (19 02 09)", log)
    codes = parse_dtcs(dtc)
    if codes:
        print("  %d stored code(s):" % len(codes))
        for c, status in codes[:40]:
            print("    %s  status 0x%02X" % (c, status))
        if len(codes) > 40:
            print("    ... (%d more)" % (len(codes) - 40))
    else:
        print("  no codes parsed (either none stored, or the HU declined the request)")

    # ---- verdict
    print("\n" + "=" * 72)
    print("VERDICT")
    print("=" * 72)

    latched = (en is not None and en != 0x00) or (ext is not None and ext != 0x00)
    alive = (en is not None) or (st is not None) or (vin is not None)

    if not alive:
        print("The HU is NOT answering diagnostics.")
        print("  The unit is not on the bus. This is beyond a state-clear:")
        print("  -> take it to ISTA, read the gateway's view of the HU, and plan a")
        print("     module recovery / reprogramming to factory software.")
        rc = 2
    elif latched:
        print("LATCHED FLASH STATE FOUND -- this is the documented black-screen cause.")
        print("  EnergyMode/ExtendedMode did not return to 0x00 after the aborted flash,")
        print("  so the HU answers diagnostics but will not start its HMI.")
        if a.clear:
            print("\n  --clear given: running the flasher's tested teardown now.")
        else:
            print("\n  -> RE-RUN THIS TOOL WITH  --clear  to clear it and reset the unit.")
            print("     (clears the registers and resets; writes no files)")
        rc = 1
    else:
        print("Flash-state registers read CLEAN (EnergyMode 0x00 / ExtendedMode 0x00).")
        print("  So a latched flash state is NOT why the screen is dark.")
        print("  -> the HU needs a software recovery: put it on ISTA, read the fault")
        print("     memory above into the session, and reprogram the HU to factory")
        print("     software (battery charger on, solid ENET).")
        print("  -> do NOT re-run the OPT_DEBUG flash on this unit until it boots again")
        print("     AND the SVK above is confirmed to be in the 007.xxx family.")
        rc = 0

    # ---- optional teardown
    if a.clear and alive:
        print("\n" + "=" * 72)
        print("TEARDOWN (clear latched state + reset)")
        print("=" * 72)
        try:
            ok = mod.teardown(h, log)
            print("\n  teardown reported: %s" % ("CLEAN" if ok else "forced reset"))
        except Exception as e:
            print("\n  teardown error: %s" % e)
            ok = False

        print("\n  waiting 60s for the HU to boot its HMI ...")
        time.sleep(60)
        try:
            mod._reopen(h)
            mod._gtx(h, mod.EXT_SESSION, "10 03 ExtendedSession", log)
            en2 = mod._val(mod._gtx(h, mod.READ_ENERGY, "22 100A EnergyMode", log))
            st2 = mod._read_state(h, "22 F186 (state)", log)
            print("\n  after reset: EnergyMode = %s   State = %s" % (fmt(en2), fmt(st2)))
            if en2 == 0x00 and st2 == 0x01:
                print("  Registers are clean and the HU is in its application.")
                print("  -> CHECK THE SCREEN NOW. If it is up, the unit is recovered.")
                print("  -> If it is still dark, the state was not the cause: go to ISTA.")
            else:
                print("  Still not clean -> go to ISTA for a module recovery.")
        except Exception as e:
            print("  post-reset read failed: %s" % e)
            print("  (the HU may still be rebooting -- re-run this tool read-only in a minute)")

    try:
        h.close()
    except Exception:
        pass

    print("\nDone. Nothing was written to the head unit.")
    return rc


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\ninterrupted")
        sys.exit(130)
