"""Bench test GUI for the traffic light + pedestrian signal.

Drives the ESP32 over HTTP. Two jobs:

  Wiring test  - puts the controller in test mode and asserts individual relay
                 channels, so you can confirm which channel lights which wire
                 before trusting the cycle.
  Cycle test   - leaves test mode and exercises the real ped state machine
                 (WALK -> FDW -> DON'T WALK) on the board.

Standard library only. Run with: python tools/signal_test_gui.py
"""

import json
import queue
import threading
import tkinter as tk
from tkinter import ttk
from urllib import error, request

DEFAULT_HOST = "192.168.1.50"
TIMEOUT = 3

OUTPUTS = [
    ("red", "RED", "#d32f2f"),
    ("yellow", "YELLOW", "#f9a825"),
    ("green", "GREEN", "#388e3c"),
    ("walk", "WALK (blue)", "#1976d2"),
    ("dont_walk", "DON'T WALK (orange)", "#e65100"),
    ("cd_walk", "COUNTDOWN walk (blue)", "#1976d2"),
    ("cd_dont_walk", "COUNTDOWN hand (orange)", "#e65100"),
]

PED_STATES = [("walk", "WALK"), ("fdw", "FDW"), ("dont_walk", "DON'T WALK"), ("off", "OFF")]


class Controller:
    """Blocking HTTP calls, always used from a worker thread."""

    def __init__(self):
        self.host = DEFAULT_HOST

    def post(self, path, payload):
        url = "http://%s%s" % (self.host, path)
        body = json.dumps(payload).encode()
        req = request.Request(url, data=body, headers={"Content-Type": "application/json"})
        with request.urlopen(req, timeout=TIMEOUT) as resp:
            raw = resp.read().decode()
        return json.loads(raw) if raw else {}

    def get(self, path):
        url = "http://%s%s" % (self.host, path)
        with request.urlopen(url, timeout=TIMEOUT) as resp:
            raw = resp.read().decode()
        return json.loads(raw) if raw else {}


class SignalTestGui:
    def __init__(self, root):
        self.root = root
        self.ctrl = Controller()
        self.events = queue.Queue()

        # local mirror of what we have asserted, for the drawing only
        self.asserted = {key: False for key, _, _ in OUTPUTS}
        self.test_mode = False

        # simulated countdown, mirrors how the real module learns then counts
        self.sim_learned = None
        self.sim_count = None

        root.title("Traffic + Ped Signal Bench Test")
        root.minsize(760, 560)

        self._build_connection_row()
        self._build_body()
        self._build_log()

        self._redraw()
        self.root.after(80, self._drain_events)

    # ---------- layout ----------

    def _build_connection_row(self):
        row = ttk.Frame(self.root, padding=8)
        row.pack(fill="x")

        ttk.Label(row, text="ESP32 host:").pack(side="left")
        self.host_var = tk.StringVar(value=DEFAULT_HOST)
        ttk.Entry(row, textvariable=self.host_var, width=18).pack(side="left", padx=(4, 12))

        self.test_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            row,
            text="Test mode (suspends the automatic cycle)",
            variable=self.test_var,
            command=self._toggle_test_mode,
        ).pack(side="left")

        self.status_var = tk.StringVar(value="not connected")
        ttk.Label(row, textvariable=self.status_var, foreground="#666").pack(side="right")

    def _build_body(self):
        body = ttk.Frame(self.root, padding=(8, 0))
        body.pack(fill="both", expand=True)

        self.canvas = tk.Canvas(body, width=280, height=380, bg="#1a1a1a", highlightthickness=0)
        self.canvas.pack(side="left", padx=(0, 12))

        panel = ttk.Frame(body)
        panel.pack(side="left", fill="both", expand=True)

        self._build_wiring_panel(panel)
        self._build_cycle_panel(panel)

    def _build_wiring_panel(self, parent):
        box = ttk.LabelFrame(parent, text="Wiring test - assert one channel at a time", padding=8)
        box.pack(fill="x", pady=(0, 10))

        ttk.Label(
            box,
            text="Needs test mode on. Ring out each wire against the channel it lights.",
            foreground="#666",
            wraplength=420,
        ).pack(anchor="w", pady=(0, 6))

        self.output_vars = {}
        for key, label, colour in OUTPUTS:
            var = tk.BooleanVar(value=False)
            self.output_vars[key] = var
            ttk.Checkbutton(
                box,
                text=label,
                variable=var,
                command=lambda k=key: self._set_output(k),
            ).pack(anchor="w")

        ttk.Button(box, text="All off", command=self._all_off).pack(anchor="w", pady=(6, 0))

    def _build_cycle_panel(self, parent):
        box = ttk.LabelFrame(parent, text="Cycle test - runs on the board", padding=8)
        box.pack(fill="x")

        ttk.Label(
            box,
            text="Needs test mode off. A jumped state runs WALK -> FDW -> DW on its own timers; "
            "untick 'chained' first or the next vehicle phase change will restart it.",
            foreground="#666",
            wraplength=420,
        ).pack(anchor="w", pady=(0, 6))

        jump = ttk.Frame(box)
        jump.pack(fill="x", pady=(0, 8))
        ttk.Label(jump, text="Jump to ped state:").pack(side="left", padx=(0, 6))
        for key, label in PED_STATES:
            ttk.Button(jump, text=label, width=11, command=lambda k=key: self._set_ped_state(k)).pack(
                side="left", padx=2
            )

        cfg = ttk.Frame(box)
        cfg.pack(fill="x")

        ttk.Label(cfg, text="WALK (s):").grid(row=0, column=0, sticky="w")
        self.walk_var = tk.IntVar(value=7)
        ttk.Spinbox(cfg, from_=1, to=120, textvariable=self.walk_var, width=6).grid(row=0, column=1, padx=(4, 16))

        ttk.Label(cfg, text="FDW (s):").grid(row=0, column=2, sticky="w")
        self.fdw_var = tk.IntVar(value=15)
        ttk.Spinbox(cfg, from_=3, to=99, textvariable=self.fdw_var, width=6).grid(row=0, column=3, padx=(4, 16))

        ttk.Label(cfg, text="(FDW is the countdown number)", foreground="#666").grid(
            row=0, column=4, sticky="w"
        )

        self.chained_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(cfg, text="Chained to vehicle cycle", variable=self.chained_var).grid(
            row=1, column=0, columnspan=2, sticky="w", pady=(8, 0)
        )

        ttk.Label(cfg, text="WALK during:").grid(row=1, column=2, sticky="e", pady=(8, 0))
        self.phase_var = tk.StringVar(value="red")
        ttk.Combobox(
            cfg, textvariable=self.phase_var, values=["red", "green"], width=7, state="readonly"
        ).grid(row=1, column=3, sticky="w", pady=(8, 0))

        ttk.Button(box, text="Apply ped config", command=self._apply_ped_config).pack(
            anchor="w", pady=(10, 0)
        )

    def _build_log(self):
        box = ttk.LabelFrame(self.root, text="Log", padding=6)
        box.pack(fill="both", expand=True, padx=8, pady=8)
        self.log_text = tk.Text(box, height=7, wrap="word", state="disabled", font=("Consolas", 9))
        self.log_text.pack(fill="both", expand=True)

    # ---------- drawing ----------

    def _redraw(self):
        c = self.canvas
        c.delete("all")

        on = self.asserted

        # vehicle head
        c.create_rectangle(20, 20, 100, 200, fill="#2b2b2b", outline="#000")
        lamps = [("red", "#ff3b30", 50), ("yellow", "#ffcc00", 110), ("green", "#34c759", 170)]
        for key, colour, cy in lamps:
            fill = colour if on[key] else "#3a3a3a"
            c.create_oval(30, cy - 20, 90, cy + 20, fill=fill, outline="#111")

        # ped combo head
        c.create_rectangle(130, 20, 260, 140, fill="#2b2b2b", outline="#000")
        hand = "#ff7a1a" if on["dont_walk"] else "#3a3a3a"
        man = "#e8e8e8" if on["walk"] else "#3a3a3a"
        c.create_text(165, 80, text="✋", font=("Segoe UI Emoji", 34), fill=hand)
        c.create_text(225, 80, text="\U0001f6b6", font=("Segoe UI Emoji", 34), fill=man)

        # countdown head
        c.create_rectangle(130, 150, 260, 250, fill="#2b2b2b", outline="#000")
        text = "%02d" % self.sim_count if self.sim_count is not None else "  "
        c.create_text(195, 200, text=text, font=("Consolas", 40, "bold"), fill="#ff7a1a")

        note = "test mode" if self.test_mode else "cycle running on board"
        c.create_text(140, 300, text=note, anchor="w", fill="#888", font=("Segoe UI", 9))
        c.create_text(20, 300, text="simulated", anchor="w", fill="#555", font=("Segoe UI", 9))
        c.create_text(
            20,
            330,
            text="countdown shown here is a model,\nthe real module learns it itself",
            anchor="nw",
            fill="#555",
            font=("Segoe UI", 8),
        )

    # ---------- actions ----------

    def _log(self, msg):
        self.log_text.configure(state="normal")
        self.log_text.insert("end", msg + "\n")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _run(self, fn, *args):
        self.ctrl.host = self.host_var.get().strip()

        def worker():
            try:
                result = fn(*args)
                self.events.put(("ok", result))
            except error.HTTPError as exc:
                self.events.put(("err", "HTTP %s: %s" % (exc.code, exc.read().decode(errors="replace"))))
            except Exception as exc:  # noqa: BLE001 - surface whatever the socket layer gives us
                self.events.put(("err", str(exc)))

        threading.Thread(target=worker, daemon=True).start()

    def _drain_events(self):
        while True:
            try:
                kind, payload = self.events.get_nowait()
            except queue.Empty:
                break
            if kind == "ok":
                self.status_var.set("connected")
                self._log("<- %s" % json.dumps(payload))
                self._absorb(payload)
            else:
                self.status_var.set("error")
                self._log("!! %s" % payload)
        self._redraw()
        self.root.after(80, self._drain_events)

    def _absorb(self, payload):
        """Take authoritative output state back from the board when it sends it."""
        for key in self.asserted:
            if key in payload:
                self.asserted[key] = bool(payload[key])
                if key in self.output_vars:
                    self.output_vars[key].set(self.asserted[key])
        if "test_mode" in payload:
            self.test_mode = bool(payload["test_mode"])
            self.test_var.set(self.test_mode)

    def _toggle_test_mode(self):
        enabled = self.test_var.get()
        self.test_mode = enabled
        if not enabled:
            for key in self.asserted:
                self.asserted[key] = False
            for var in self.output_vars.values():
                var.set(False)
        self._log("-> test_mode %s" % enabled)
        self._run(self.ctrl.post, "/test_mode", {"enabled": enabled})

    def _set_output(self, key):
        if not self.test_var.get():
            self._log("!! enable test mode first")
            self.output_vars[key].set(False)
            return
        state = self.output_vars[key].get()
        self.asserted[key] = state
        self._log("-> %s %s" % (key, "on" if state else "off"))
        self._run(self.ctrl.post, "/set_output", {"output": key, "state": state})

    def _all_off(self):
        for var in self.output_vars.values():
            var.set(False)
        for key in self.asserted:
            self.asserted[key] = False
        self._log("-> all off")
        self._run(self.ctrl.post, "/set_output", {"output": "all_off", "state": False})

    def _set_ped_state(self, state):
        if self.test_var.get():
            self._log("!! turn test mode off to run the ped state machine")
            return
        self._log("-> ped %s" % state)
        self._model_countdown(state)
        self._run(self.ctrl.post, "/ped_control", {"action": "set_state", "state": state})

    def _model_countdown(self, state):
        """Mirror the real module: blank until it has seen one FDW, then count."""
        if state == "fdw":
            if self.sim_learned is None:
                self.sim_learned = self.fdw_var.get()
                self.sim_count = None
                self._log("   countdown learning this cycle, display stays blank")
            else:
                self.sim_count = self.sim_learned
                self._tick_countdown()
        else:
            self.sim_count = None

    def _tick_countdown(self):
        if self.sim_count is None:
            return
        if self.sim_count <= 0:
            self.sim_count = None
            return
        self.sim_count -= 1
        self.root.after(1000, self._tick_countdown)

    def _apply_ped_config(self):
        payload = {
            "action": "set_config",
            "walk": self.walk_var.get(),
            "fdw": self.fdw_var.get(),
            "chained": self.chained_var.get(),
            "chain_phase": self.phase_var.get(),
        }
        # a changed FDW means the real module has to relearn, so reset the model too
        if self.sim_learned != self.fdw_var.get():
            self.sim_learned = None
            self._log("   FDW changed, countdown will relearn for one cycle")
        self._log("-> ped config %s" % json.dumps(payload))
        self._run(self.ctrl.post, "/ped_control", payload)


def main():
    root = tk.Tk()
    SignalTestGui(root)
    root.mainloop()


if __name__ == "__main__":
    main()
