"""MHD Maps and engine codes through the shared link: MOST fallback, lease waits, HSFZ unchanged."""
from pathlib import Path
import os
import signal
import socket
import struct
import subprocess
import threading
import time
import unittest

from test_transport import exact, frame

BUILD = Path(__file__).resolve().parents[1] / "build"
BUSY = "Diagnostics busy. Wait a moment and try again.\n"


def fake_env(fake=None, delay_ms=None):
    env = dict(os.environ)
    env.pop("ID6_FAKE_MOST", None)
    env.pop("ID6_FAKE_MOST_DELAY_MS", None)
    if fake:
        env["ID6_FAKE_MOST"] = fake
    if delay_ms:
        env["ID6_FAKE_MOST_DELAY_MS"] = str(delay_ms)
    return env


def run(worker, args, fake=None, timeout=15, delay_ms=None):
    return subprocess.run([BUILD/worker, *args], capture_output=True, text=True,
                          env=fake_env(fake, delay_ms), timeout=timeout)


def result_before_close(test, worker, args, want):
    """With a slow DME the close takes a second; the result must reach the pipe's end before it."""
    proc = subprocess.Popen([BUILD/worker, *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, env=fake_env("up", 1000))
    try:
        out = proc.stdout.read()
        at_eof = time.monotonic()
        test.assertEqual(proc.wait(10), 0)
        exited = time.monotonic()
    finally:
        if proc.poll() is None:
            proc.kill()
        err = proc.communicate()[1]
    test.assertEqual(out, want)
    test.assertGreater(exited - at_eof, 0.5)
    test.assertIn("FAKE-MOST exit unregister_target=1 unregister_client=1 free=1", err)


def hold_lease(seconds):
    """Hold 127.0.0.1:42861 for a while on a thread; returns once the lease is taken."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 42861))
    thread = threading.Thread(target=lambda: (time.sleep(seconds), sock.close()))
    thread.start()
    return thread


class ControlsMostTest(unittest.TestCase):
    def disconnected(self, worker, args, fake="up"):
        # Reserve a local TCP port without listening so connect fails predictably.
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = str(sock.getsockname()[1])
            return run(worker, ["--host", "127.0.0.1", *args, "--port", port], fake)

    def test_bootmod3_fallback_short_commands(self):
        for args, message in [
            (["--map", "2"], "Map 2 verified by DME readback."),
            (["--sound", "1"], "Active Sound request accepted by DME."),
            (["--flap", "1"], "Exhaust flap request accepted by DME."),
        ]:
            with self.subTest(args=args):
                r = self.disconnected("bootmod3", args)
                self.assertEqual(r.returncode, 0)
                self.assertIn("transport: MOST", r.stderr)
                self.assertEqual(r.stdout.strip(), message)

    def test_bootmod3_blocks_unknown_rom(self):
        r = self.disconnected("bootmod3", ["--map", "2"], "bm3-unknown")
        self.assertEqual(r.stdout.strip(), "Bootmod3 CustomROM could not be verified.")

    def test_bootmod3_burbles_requires_hsfz(self):
        r = self.disconnected("bootmod3", ["--burbles", "2"])
        self.assertEqual(r.stdout.strip(), "Burble presets require the HSFZ gateway.")
        self.assertNotIn("FAKE-MOST bind", r.stderr)

    def test_integrated_exhaust_fallback(self):
        for mode, label in enumerate(("auto", "open", "closed")):
            with self.subTest(mode=mode):
                r = self.disconnected("exhaust-flap", ["--flap", str(mode)])
                self.assertEqual(r.returncode, 0)
                self.assertIn("transport: MOST", r.stderr)
                self.assertEqual(r.stdout.strip(), f"Confirmed: Exhaust {label}")

    def test_integrated_exhaust_reports_refusal(self):
        r = self.disconnected("exhaust-flap", ["--flap", "1"], "mhd-refuse")
        self.assertEqual(r.stdout.strip(), "Exhaust open refused by DME (NRC 31)")


class MhdMostTest(unittest.TestCase):
    def test_mhd_most_confirmed(self):
        r = run("mhd-maps", ["--transport", "most", "--map", "2"], "up")
        self.assertEqual(r.stdout, "Confirmed: Map 2\n")
        self.assertEqual(r.returncode, 0)
        self.assertIn("transport: MOST", r.stderr)

    def test_mhd_most_refused(self):
        r = run("mhd-maps", ["--transport", "most", "--map", "2"], "mhd-refuse")
        self.assertEqual(r.stdout, "Map 2 refused by DME (NRC 31)\n")

    def test_mhd_most_unknown(self):
        r = run("mhd-maps", ["--transport", "most", "--map", "2"], "no-server")
        self.assertEqual(r.stdout, "Diagnostic gateway unavailable\n")

    def test_mhd_without_host_uses_most(self):
        r = run("mhd-maps", ["--map", "3"], "up")
        self.assertEqual(r.stdout, "Confirmed: Map 3\n")

    def test_mhd_waits_for_busy_lease(self):
        thread = hold_lease(0.8)
        try:
            r = run("mhd-maps", ["--transport", "most", "--map", "2"], "up")
        finally:
            thread.join()
        self.assertEqual(r.stdout, "Confirmed: Map 2\n")

    def test_mhd_result_before_close(self):
        result_before_close(self, "mhd-maps", ["--transport", "most", "--map", "2"], "Confirmed: Map 2\n")

    def test_mhd_presses_while_boostdeck_reads_most(self):
        """A slow tunnel: the gauge worker steps aside, so every map press gets its turn."""
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0))
            boost = subprocess.Popen([BUILD/"boostdeck-collector", "--session", "--transport", "most",
                                      "--udp-port", str(udp.getsockname()[1])],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
                                     env=fake_env("up", 250))
            results = []
            try:
                time.sleep(1.5)
                for press in range(8):
                    slot = press % 4 + 1
                    started = time.monotonic()
                    r = run("mhd-maps", ["--transport", "most", "--map", str(slot)], "up", delay_ms=250)
                    results.append(r.stdout)
                    self.assertIsNone(boost.poll())
                    time.sleep(max(0, 2.5 - (time.monotonic() - started)))
            finally:
                if boost.poll() is None:
                    boost.send_signal(signal.SIGTERM)
                code = boost.wait(10)
                err = boost.communicate()[1]
        self.assertEqual(results, [f"Confirmed: Map {p % 4 + 1}\n" for p in range(8)])
        self.assertEqual(code, 0, err)

    def test_mhd_busy_after_2s(self):
        thread = hold_lease(3)
        try:
            start = time.monotonic()
            r = run("mhd-maps", ["--transport", "most", "--map", "2"], "up")
            waited = time.monotonic() - start
        finally:
            thread.join()
        self.assertEqual(r.stdout, BUSY)
        self.assertGreaterEqual(waited, 1.8)
        self.assertLess(waited, 3)


class EngineCodesTest(unittest.TestCase):
    def test_engine_codes_most_read(self):
        r = run("engine-codes", ["--transport", "most", "--codes"], "up")
        self.assertEqual(r.stdout, "Engine: 2 codes\n123456  status 2F\n0ABCDE  status 08\n")

    def test_engine_codes_most_clear(self):
        r = run("engine-codes", ["--transport", "most", "--clear"], "up")
        self.assertEqual(r.stdout, "Engine: 2 codes after clear\n123456  status 2F\n0ABCDE  status 08\n")

    def test_engine_codes_full_reply(self):
        r = run("engine-codes", ["--transport", "most", "--codes"], "codes-512")
        lines = r.stdout.splitlines()
        self.assertEqual(lines[0], "Engine: 127 codes")
        self.assertEqual(lines[1], "010000  status 08")
        self.assertEqual(lines[-2], "01007E  status 08")
        self.assertEqual(lines[-1], "More codes may exist")

    def test_engine_codes_oversized(self):
        r = run("engine-codes", ["--transport", "most", "--codes"], "codes-513")
        self.assertEqual(r.stdout, "No valid code response\n")

    def test_engine_codes_result_before_close(self):
        result_before_close(self, "engine-codes", ["--transport", "most", "--codes"],
                            "Engine: 2 codes\n123456  status 2F\n0ABCDE  status 08\n")

    def test_engine_codes_busy(self):
        thread = hold_lease(3)
        try:
            r = run("engine-codes", ["--transport", "most", "--codes"], "up")
        finally:
            thread.join()
        self.assertEqual(r.stdout, BUSY)

    def run_hsfz(self, reply_hex):
        errors = []
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            listener.settimeout(3)

            def gateway():
                try:
                    conn, _ = listener.accept()
                    with conn:
                        conn.settimeout(3)
                        size, control = struct.unpack(">IH", exact(conn, 6))
                        self.assertEqual(exact(conn, size), bytes.fromhex("f4121902af"))
                        conn.sendall(frame(1, bytes.fromhex("12f4" + reply_hex)))
                        self.assertEqual(conn.recv(1), b"")
                except Exception as error:
                    errors.append(error)

            thread = threading.Thread(target=gateway)
            thread.start()
            r = run("engine-codes", ["--host", "127.0.0.1", "--port", str(listener.getsockname()[1]),
                                     "--codes"], "up")
            thread.join(4)
        if errors:
            raise errors[0]
        return r

    def test_engine_codes_hsfz_odd_length_is_invalid(self):
        r = self.run_hsfz("5902ff01")
        self.assertEqual(r.stdout, "No valid code response\n")

    def test_engine_codes_hsfz_200_codes_show_128(self):
        codes = "".join(f"01{i >> 8:02x}{i & 255:02x}08" for i in range(200))
        r = self.run_hsfz("5902ff" + codes)
        lines = r.stdout.splitlines()
        self.assertEqual(lines[0], "Engine: 200 codes")
        self.assertEqual(len(lines), 1 + 128 + 1)
        self.assertEqual(lines[-1], "More codes returned; first 128 shown")
        self.assertNotIn("More codes may exist", r.stdout)

    def test_engine_codes_hsfz_unchanged(self):
        errors = []
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            listener.settimeout(3)

            def gateway():
                try:
                    conn, _ = listener.accept()
                    with conn:
                        conn.settimeout(3)
                        size, control = struct.unpack(">IH", exact(conn, 6))
                        self.assertEqual(exact(conn, size), bytes.fromhex("f4121902af"))
                        conn.sendall(frame(1, bytes.fromhex("12f45902ff1234562f"
                                                            + "0abcde08")))
                        self.assertEqual(conn.recv(1), b"")
                except Exception as error:
                    errors.append(error)

            thread = threading.Thread(target=gateway)
            thread.start()
            r = run("engine-codes", ["--host", "127.0.0.1", "--port", str(listener.getsockname()[1]),
                                     "--codes"], "up")
            thread.join(4)
        if errors:
            raise errors[0]
        self.assertEqual(r.stdout, "Engine: 2 codes\n123456  status 2F\n0ABCDE  status 08\n")
        self.assertNotIn("FAKE-MOST bind", r.stderr)


if __name__ == "__main__":
    unittest.main()
