#!/usr/bin/env python3
"""User-friendly GUI: enable/disable SSH (OPT_DEBUG) on a BMW NBT-Evo head unit.

Wraps the standalone flasher in nbt_optdebug_flash.py (same repo). Nothing is
transmitted until you tick the safety box and type the confirmation word.
Two actions: unlock SSH, and restore the factory starter.cfg.
"""
import contextlib
import io
import os
import queue
import re
import sys
import threading
import traceback
import tkinter as tk
from tkinter import ttk, messagebox

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nbt_optdebug_flash as core

SSH_IP = "169.254.199.119"
SSH_CMD = "ssh root@" + SSH_IP

ENABLE_TEXT = (
    "This writes a modified starter.cfg DIRECTLY to the unit's live partition "
    "(LIVE-DIRECT) and bypasses signature verification.\n\n"
    "• Works only on NBT-Evo 007.xxx (NOT on 001_05x).\n"
    "• After the write and restart, sshd (22), telnetd (23) and qconn (8000) run as root.\n"
    "• A bad or version-mismatched file = the unit will not boot (recover with revert or serial/eMMC).\n"
    "• Do not interrupt the write and do not disconnect the network during it.\n\n"
    "The HU will reboot after the write. You take this action at your own risk."
)
REVERT_TEXT = (
    "This restores the FACTORY starter.cfg via the same LIVE-DIRECT path.\n\n"
    "• Use it if the unit does not boot properly after unlocking.\n"
    "• After the reboot, SSH/telnet/qconn should be closed again.\n"
    "• Do not interrupt the write.\n\n"
    "You take this action at your own risk."
)


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("NBT-Evo — SSH Unlock")
        self.geometry("920x800")
        self.minsize(820, 700)
        self.q = queue.Queue()
        self.busy = False
        self.action_buttons = []
        self.free_buttons = []
        self._style()
        self._build()
        self._update_buttons()
        self.after(100, self._drain)

    def _style(self):
        style = ttk.Style(self)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure(".", font=("Segoe UI", 10))
        style.configure("Title.TLabel", font=("Segoe UI Semibold", 17))
        style.configure("Sub.TLabel", foreground="#555555")
        style.configure("Enable.TButton", font=("Segoe UI Semibold", 11), padding=(14, 10))
        style.configure("Revert.TButton", font=("Segoe UI", 10), padding=(14, 10))
        style.configure("Mono.TLabel", font=("Consolas", 11))
        style.configure("Status.TLabel", foreground="#333333")

    def _build(self):
        root = ttk.Frame(self, padding=16)
        root.pack(fill="both", expand=True)
        root.columnconfigure(0, weight=1)
        root.rowconfigure(6, weight=1)

        ttk.Label(root, text="NBT-Evo — SSH Unlock", style="Title.TLabel").grid(
            row=0, column=0, sticky="w")
        ttk.Label(
            root,
            text="Simple front-end for nbt_optdebug_flash.py: enables OPT_DEBUG (SSH) or restores the factory config.",
            style="Sub.TLabel",
        ).grid(row=1, column=0, sticky="w", pady=(2, 12))

        conn = ttk.LabelFrame(root, text="Connection", padding=12)
        conn.grid(row=2, column=0, sticky="ew")
        conn.columnconfigure(1, weight=1)
        ttk.Label(conn, text="Gateway address:").grid(row=0, column=0, sticky="w")
        self.var_host = tk.StringVar(value="")
        ttk.Entry(conn, textvariable=self.var_host, width=24).grid(
            row=0, column=1, sticky="ew", padx=(8, 8))
        ttk.Label(conn, text="Port:").grid(row=0, column=2, sticky="w")
        self.var_port = tk.StringVar(value=str(core.HSFZ_PORT))
        ttk.Entry(conn, textvariable=self.var_port, width=8).grid(
            row=0, column=3, sticky="w", padx=(6, 8))
        self.btn_detect = ttk.Button(conn, text="Detect", command=self._detect)
        self.btn_detect.grid(row=0, column=4, padx=(0, 6))
        self.btn_test = ttk.Button(conn, text="Test", command=self._test_conn)
        self.btn_test.grid(row=0, column=5)
        self.free_buttons += [self.btn_detect, self.btn_test]
        ttk.Label(conn, text="Empty = auto-detect on the ENET network.", style="Sub.TLabel").grid(
            row=1, column=0, columnspan=6, sticky="w", pady=(8, 0))
        self.lbl_conn = ttk.Label(conn, text="Connection not checked yet.", style="Status.TLabel")
        self.lbl_conn.grid(row=2, column=0, columnspan=6, sticky="w", pady=(4, 0))

        actions = ttk.LabelFrame(root, text="Actions", padding=12)
        actions.grid(row=3, column=0, sticky="ew", pady=(12, 0))
        actions.columnconfigure(0, weight=1)
        actions.columnconfigure(1, weight=1)
        self.btn_enable = ttk.Button(actions, text="Unlock SSH", style="Enable.TButton",
                                     command=self._enable)
        self.btn_enable.grid(row=0, column=0, sticky="ew", padx=(0, 6))
        self.btn_revert = ttk.Button(actions, text="Restore factory (revert)", style="Revert.TButton",
                                     command=self._revert)
        self.btn_revert.grid(row=0, column=1, sticky="ew", padx=(6, 0))
        self.action_buttons = [self.btn_enable, self.btn_revert]
        ttk.Label(actions, text="Tick the safety confirmation below first.", style="Sub.TLabel").grid(
            row=1, column=0, columnspan=2, sticky="w", pady=(8, 0))

        adv = ttk.LabelFrame(root, text="Advanced (do not change unless needed)", padding=12)
        adv.grid(row=4, column=0, sticky="ew", pady=(12, 0))
        self.var_lbn = tk.StringVar(value="0")
        self.var_tester = tk.StringVar(value="00000000")
        self.var_ctrl = tk.StringVar(value="00001ffd")
        self.var_fp = tk.StringVar(value="00" * 13)
        self.var_reset = tk.StringVar(value="50")
        fields = [
            ("LBN (hex)", self.var_lbn),
            ("Tester ID (hex)", self.var_tester),
            ("Controller (hex)", self.var_ctrl),
            ("Fingerprint (hex)", self.var_fp),
            ("Reset timeout (s)", self.var_reset),
        ]
        for i, (label, var) in enumerate(fields):
            r, c = divmod(i, 2)
            ttk.Label(adv, text=label).grid(row=r, column=c * 2, sticky="w", pady=3)
            ttk.Entry(adv, textvariable=var, width=20).grid(
                row=r, column=c * 2 + 1, sticky="w", padx=(8, 24), pady=3)

        self.var_safe = tk.BooleanVar(value=False)
        safe = ttk.Checkbutton(
            root,
            text="I understand the risk: this writes to a unit in the car (possible brick).",
            variable=self.var_safe,
            command=self._update_buttons,
        )
        safe.grid(row=5, column=0, sticky="w", pady=(12, 4))

        logf = ttk.LabelFrame(root, text="Log", padding=(8, 8))
        logf.grid(row=6, column=0, sticky="nsew", pady=(8, 0))
        logf.columnconfigure(0, weight=1)
        logf.rowconfigure(0, weight=1)
        self.txt = tk.Text(logf, height=14, wrap="none", font=("Consolas", 9),
                           background="#111418", foreground="#d6d9dd",
                           insertbackground="#d6d9dd", relief="flat")
        self.txt.grid(row=0, column=0, sticky="nsew")
        ys = ttk.Scrollbar(logf, orient="vertical", command=self.txt.yview)
        ys.grid(row=0, column=1, sticky="ns")
        self.txt.configure(yscrollcommand=ys.set, state="disabled")
        self.txt.tag_configure("err", foreground="#ff6b6b")
        self.txt.tag_configure("ok", foreground="#7ee787")
        self.txt.tag_configure("dim", foreground="#8b949e")

        ssh_box = ttk.LabelFrame(root, text="After unlocking", padding=12)
        ssh_box.grid(row=7, column=0, sticky="ew", pady=(10, 0))
        ssh_box.columnconfigure(1, weight=1)
        ttk.Label(ssh_box, text=SSH_CMD, style="Mono.TLabel").grid(row=0, column=0, sticky="w")
        ttk.Button(ssh_box, text="Copy", command=self._copy_ssh).grid(row=0, column=2, padx=(8, 0))
        ttk.Label(ssh_box, text="Ports 22/23/8000, user root (no password).",
                  style="Sub.TLabel").grid(row=1, column=0, columnspan=3, sticky="w", pady=(4, 0))

        bar = ttk.Frame(root)
        bar.grid(row=8, column=0, sticky="ew", pady=(10, 0))
        bar.columnconfigure(0, weight=1)
        self.progress = ttk.Progressbar(bar, mode="determinate", maximum=100)
        self.progress.grid(row=0, column=0, sticky="ew")
        self.status_var = tk.StringVar(value="Ready.")
        ttk.Label(bar, textvariable=self.status_var, style="Status.TLabel").grid(
            row=1, column=0, sticky="w", pady=(4, 0))
        self.btn_selftest = ttk.Button(bar, text="Offline self-test", command=self._selftest)
        self.btn_selftest.grid(row=0, column=1, rowspan=2, padx=(10, 0))
        self.free_buttons.append(self.btn_selftest)

    def _update_buttons(self):
        for b in self.free_buttons:
            b.configure(state="disabled" if self.busy else "normal")
        enable_state = "normal" if (self.var_safe.get() and not self.busy) else "disabled"
        for b in self.action_buttons:
            b.configure(state=enable_state)
        if self.busy:
            self.btn_selftest.configure(state="disabled")

    def _append(self, text):
        self.txt.configure(state="normal")
        if "***" in text or "ERROR" in text or "FAILED" in text:
            tag = "err"
        elif "[..]" in text or "->" in text:
            tag = "ok"
        else:
            tag = None
        if tag:
            self.txt.insert("end", text, tag)
        else:
            self.txt.insert("end", text)
        self.txt.see("end")
        self.txt.configure(state="disabled")

    def _emit_log(self, msg):
        s = str(msg)
        self.q.put(("log", s + "\n"))
        m = re.search(r"block (\d+)/(\d+)", s)
        if m:
            i, n = int(m.group(1)), int(m.group(2))
            self.q.put(("progress", 100.0 * i / max(1, n)))

    def _clear_log(self):
        self.txt.configure(state="normal")
        self.txt.delete("1.0", "end")
        self.txt.configure(state="disabled")
        self.progress.configure(mode="determinate", value=0)

    def _drain(self):
        try:
            while True:
                item = self.q.get_nowait()
                kind = item[0]
                if kind == "log":
                    self._append(item[1])
                elif kind == "status":
                    self.status_var.set(item[1])
                elif kind == "progress":
                    value = item[1]
                    if value is None:
                        self.progress.stop()
                        self.progress.configure(mode="indeterminate")
                        self.progress.start(12)
                    else:
                        if str(self.progress.cget("mode")) != "determinate":
                            self.progress.stop()
                            self.progress.configure(mode="determinate", maximum=100)
                        self.progress["value"] = value
                elif kind == "hostfound":
                    ip, vin = item[1], item[2]
                    self.var_host.set(ip)
                    suffix = ("  VIN: " + vin) if vin else ""
                    self.lbl_conn.configure(text="Gateway: %s%s" % (ip, suffix))
                elif kind == "done":
                    self._finish(item[1], item[2], item[3])
        except queue.Empty:
            pass
        self.after(100, self._drain)

    def _finish(self, ok, title, msg):
        self.progress.stop()
        self.progress.configure(mode="determinate", value=0)
        self.busy = False
        self._update_buttons()
        self.status_var.set(("OK: " if ok else "ERROR: ") + msg)
        if ok:
            messagebox.showinfo(title, msg, parent=self)
        else:
            messagebox.showwarning(title, msg, parent=self)

    def _start(self, worker):
        if self.busy:
            return
        self.busy = True
        self._update_buttons()
        self._clear_log()
        self.progress.configure(mode="indeterminate")
        self.progress.start(12)
        self.status_var.set("Working...")
        threading.Thread(target=self._runner(worker), daemon=True).start()

    def _runner(self, worker):
        def run():
            try:
                res = worker()
                if isinstance(res, tuple) and len(res) == 2:
                    ok, msg = bool(res[0]), str(res[1])
                else:
                    ok, msg = bool(res), ""
                self.q.put(("done", ok, "Done" if ok else "Failed", msg))
            except SystemExit as e:
                self.q.put(("done", False, "Cancelled", str(e)))
            except BaseException as e:
                self.q.put(("log", "ERROR: %s\n" % e))
                self.q.put(("log", traceback.format_exc() + "\n"))
                self.q.put(("done", False, "Error", str(e)))
        return run

    def _parse_cfg(self):
        try:
            port = int(self.var_port.get().strip())
            if not (1 <= port <= 65535):
                raise ValueError("port out of range")
            lbn = int(self.var_lbn.get().strip() or "0", 16)
            tester = bytes.fromhex(self.var_tester.get().strip() or "00000000")
            controller = bytes.fromhex(self.var_ctrl.get().strip() or "00001ffd")
            fingerprint = bytes.fromhex(self.var_fp.get().strip() or ("00" * 13))
            reset = int(self.var_reset.get().strip() or "50")
        except Exception as e:
            messagebox.showerror("Invalid settings",
                                 "Check the advanced fields (hex / number):\n%s" % e, parent=self)
            return None
        return {
            "host": self.var_host.get().strip(),
            "port": port,
            "lbn": lbn,
            "tester": tester,
            "controller": controller,
            "fingerprint": fingerprint,
            "reset": reset,
        }

    def _resolve(self, cfg, log):
        host = cfg["host"]
        if host:
            return host, None
        self.q.put(("status", "Detecting gateway (6811 broadcast)..."))
        ip, vin = core.discover()
        if ip:
            log("Auto-detected gateway %s (VIN %s)." % (ip, vin or "?"))
            self.q.put(("hostfound", ip, vin))
            return ip, vin
        log("No gateway detected — using default %s." % core.DIAG_EDGE)
        return core.DIAG_EDGE, None

    def _confirm(self, phrase, title, text):
        dlg = tk.Toplevel(self)
        dlg.title(title)
        dlg.transient(self)
        dlg.resizable(False, False)
        frm = ttk.Frame(dlg, padding=16)
        frm.pack(fill="both", expand=True)
        ttk.Label(frm, text=text, wraplength=540, justify="left").pack(anchor="w")
        ttk.Label(frm, text='Type "%s" to confirm:' % phrase,
                  font=("Segoe UI Semibold", 10)).pack(anchor="w", pady=(12, 4))
        entry = ttk.Entry(frm, width=34)
        entry.pack(anchor="w")
        entry.focus_set()
        result = {"ok": False}
        btns = ttk.Frame(frm)
        btns.pack(fill="x", pady=(14, 0))
        okb = ttk.Button(btns, text="Confirm", state="disabled")
        cancel = ttk.Button(btns, text="Cancel", command=dlg.destroy)
        okb.pack(side="right")
        cancel.pack(side="right", padx=(0, 8))

        def accept(_event=None):
            if entry.get().strip() == phrase:
                result["ok"] = True
                dlg.destroy()

        def check(_event=None):
            okb.configure(state="normal" if entry.get().strip() == phrase else "disabled")

        okb.configure(command=accept)
        entry.bind("<KeyRelease>", check)
        entry.bind("<Return>", accept)
        dlg.update_idletasks()
        x = self.winfo_rootx() + (self.winfo_width() - dlg.winfo_width()) // 2
        y = self.winfo_rooty() + (self.winfo_height() - dlg.winfo_height()) // 2
        dlg.geometry("+%d+%d" % (max(0, x), max(0, y)))
        dlg.grab_set()
        self.wait_window(dlg)
        return result["ok"]

    def _enable(self):
        if not self.var_safe.get():
            return
        if not self._confirm("UNLOCK", "SSH Unlock", ENABLE_TEXT):
            return
        cfg = self._parse_cfg()
        if cfg:
            self._start(lambda: self._op("enable", cfg))

    def _revert(self):
        if not self.var_safe.get():
            return
        if not self._confirm("REVERT", "Restore Factory", REVERT_TEXT):
            return
        cfg = self._parse_cfg()
        if cfg:
            self._start(lambda: self._op("revert", cfg))

    def _op(self, kind, cfg):
        log = self._emit_log
        if kind == "revert":
            blob, sha = core.STOCK_GZ_B64, core.STOCK_SHA256
        else:
            blob, sha = core.OPTDEBUG_GZ_B64, core.OPTDEBUG_SHA256
        try:
            img = core.load_blob(blob, sha)
        except SystemExit as e:
            return False, "Blob integrity verification failed: %s" % e
        tgt, _name, is_live = core._container_target(img)
        log("Container: %d B -> %s [%s]" % (
            len(img), tgt or "?", "LIVE-DIRECT" if is_live else "STAGING"))
        host, _vin = self._resolve(cfg, log)
        log("Gateway: %s:%d" % (host, cfg["port"]))
        h = None
        connected = False
        wrote = False
        try:
            h = core.HSFZ(host, port=cfg["port"], timeout=6.0)
            log("Connecting...")
            h.open()
            connected = True
            log("Connected.")
            try:
                live_vin = core.read_vin(h)
                if live_vin:
                    log("VIN: %s" % live_vin)
            except Exception:
                pass
            wrote = core.apply_flash(
                h, img,
                reset_timeout=cfg["reset"],
                controller=cfg["controller"],
                lbn=cfg["lbn"],
                fingerprint=cfg["fingerprint"],
                tester_id=cfg["tester"],
                tp_period=None,
                log=log,
            )
        finally:
            if connected:
                self.q.put(("status", "Cleaning up / restoring application mode..."))
                try:
                    h.stop_tester_present()
                except Exception:
                    pass
                try:
                    core.teardown(h, log=log)
                except Exception as e:
                    log("[finalize warn] %s" % e)
                try:
                    h.close()
                except Exception:
                    pass
        if not wrote:
            return False, "Flash did not complete — the existing starter.cfg is untouched."
        if kind == "revert":
            return True, "Factory starter.cfg restored. Reboot the HU and verify SSH/telnet/qconn are closed."
        return True, "OPT_DEBUG write sent. Wait ~50 s for the restart, then: " + SSH_CMD

    def _detect(self):
        def worker():
            ip, vin = core.discover()
            if ip:
                self.q.put(("hostfound", ip, vin))
                return True, "Detected gateway %s%s" % (ip, ("  VIN: " + vin) if vin else "")
            return False, "No gateway detected (6811 broadcast). Check the ENET cable and the 169.254.x.x address."
        self._start(worker)

    def _test_conn(self):
        cfg = self._parse_cfg()
        if not cfg:
            return

        def worker():
            host, _vin = self._resolve(cfg, self._emit_log)
            h = core.HSFZ(host, port=cfg["port"], timeout=6.0)
            try:
                h.open()
                self._emit_log("TCP to %s:%d OK." % (host, cfg["port"]))
                try:
                    live = core.read_vin(h)
                except Exception:
                    live = None
                if live:
                    self.q.put(("hostfound", host, live))
                    return True, "Connection OK. VIN: %s" % live
                return True, "TCP OK, but the HU did not return a VIN. Make sure this is the right device."
            finally:
                try:
                    h.close()
                except Exception:
                    pass
        self._start(worker)

    def _selftest(self):
        def worker():
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                ok = core.selftest()
            for line in buf.getvalue().splitlines():
                self._emit_log(line)
            return bool(ok), ("All OK." if ok else "There are failures — see the log.")
        self._start(worker)

    def _copy_ssh(self):
        self.clipboard_clear()
        self.clipboard_append(SSH_CMD)
        self.status_var.set("Copied: " + SSH_CMD)


if __name__ == "__main__":
    App().mainloop()
