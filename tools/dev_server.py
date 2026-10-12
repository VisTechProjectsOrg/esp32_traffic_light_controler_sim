"""Serve data/ with a simulated board behind it, so the page can be worked on without an ESP32.

    python tools/dev_server.py            then open http://localhost:8080
    python tools/dev_server.py --host 0.0.0.0 --port 8080    to reach it from a phone

The simulated board runs the light cycle and the pedestrian phase, answers the same routes
as the firmware and pushes the same websocket messages. Settings are kept in memory only.

Saving any file under data/ reloads open pages the way a real SPIFFS update does: the
socket drops, the page reconnects, sees a different SPIFFS version and reloads itself.

Stdlib only.
"""

import argparse
import base64
import hashlib
import json
import random
import struct
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

DATA = Path(__file__).resolve().parent.parent / "data"
WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
BLINK_S = 1.0       # blinkInterval in config.h
FDW_FLASH_S = 0.5   # pedFdwFlashInterval
NEXT_PHASE = {"red": "green", "green": "yellow", "yellow": "red", "off": "red"}


class Board:
    """The parts of the firmware the page can see."""

    def __init__(self):
        self.lock = threading.RLock()
        self.clients = set()
        self.reloads = 0
        self.cfg = {"delay_red": 5, "delay_yellow": 3, "delay_green": 6, "distance_max": 15.0,
                    "distance_warning": 5.0, "distance_danger": 2.0, "approach_min": 2.0,
                    "lamp_test": True, "distance_sensor_enabled": False}
        self.ped = {"walk": 7, "fdw": 5, "dw": 3, "delay": 0, "chained": True, "fit": True,
                    "chain_phase": "red"}
        self.blink = False
        self.blink_color = "none"
        self.blink_on = False
        self.blink_lamp = "all_off"
        self.cat = False
        self.test_mode = False
        self.phase = "off"
        self.phase_end = 0.0
        self.ped_state = "dont_walk"
        self.ped_steps = []     # (state, starts at) still to come in this movement
        self.next_blink = 0.0

    # ---- websocket fan-out

    def send(self, message):
        frame = ws_frame(json.dumps(message))
        with self.lock:
            for sock in list(self.clients):
                try:
                    sock.sendall(frame)
                except OSError:
                    self.clients.discard(sock)

    def drop_clients(self):
        with self.lock:
            for sock in list(self.clients):
                try:
                    sock.shutdown(2)
                except OSError:
                    pass
            self.clients.clear()

    # ---- what the routes report

    def spiffs_version(self):
        version = (DATA / "version.txt").read_text(encoding="utf-8").split()[0]
        return version + ("-dev%d" % self.reloads if self.reloads else "")

    def lamp(self):
        lamp = self.blink_lamp if self.blink else {"off": "all_off"}.get(self.phase, self.phase)
        return lamp + ("_cat" if self.cat else "")

    def config(self):
        return dict(self.cfg, distance_sensor_connected=False, version_firmware="sim",
                    version_spiffs=self.spiffs_version())

    def state(self):
        return {"light_mode": "blink_mode" if self.blink else "cycle_mode",
                "theme_mode": "cat_mode" if self.cat else "normal_mode",
                "state": self.lamp(), "blink_color": self.blink_color}

    def ped_report(self):
        p = self.ped
        return dict(p, ped_state=self.ped_state, phase_total=p["walk"] + p["fdw"] + p["dw"],
                    min_walk=0, test_mode=self.test_mode)

    # ---- the cycle

    def set_ped(self, state):
        if state != self.ped_state:
            self.ped_state = state
            self.send({"ped_state": state})

    def start_ped(self, now, available):
        """Same rules as startPedPhase(): the wait and the WALK give, the countdown never does."""
        p = self.ped
        min_walk = 0 if p["fit"] else p["walk"]
        if available < p["delay"] + p["fdw"] + min_walk:
            self.ped_steps = []
            self.set_ped("dont_walk")
            return
        walk = available - p["delay"] - p["fdw"]
        steps, t = [], now + p["delay"]
        if walk > 0:
            steps.append(("walk", t))
            t += walk
        steps += [("fdw", t), ("dont_walk", t + p["fdw"])]
        self.ped_steps = steps
        if p["delay"]:
            self.set_ped("dont_walk")

    def tick(self, now):
        with self.lock:
            if self.test_mode:
                return
            if self.blink:
                if now >= self.next_blink:
                    self.next_blink = now + BLINK_S
                    self.blink_on = not self.blink_on
                    color = self.blink_color
                    if color == "random":
                        self.blink_on = True
                        color = random.choice(["red", "yellow", "green"])
                    self.blink_lamp = ("all_on" if color == "all" else color) if self.blink_on else "all_off"
                    self.send({"state": self.lamp()})
            elif now >= self.phase_end:
                self.phase = NEXT_PHASE[self.phase]
                length = self.cfg["delay_" + self.phase]
                p = self.ped
                chained_here = p["chained"] and self.phase == p["chain_phase"]
                if chained_here and not p["fit"]:
                    length = max(length, p["delay"] + p["walk"] + p["fdw"])
                self.phase_end = now + length
                self.send({"state": self.lamp()})
                if chained_here:
                    self.start_ped(now, length)
                elif p["chained"]:
                    self.ped_steps = []
                    self.set_ped("dont_walk")

            if not self.ped["chained"] and not self.ped_steps:
                # free-running: walk, countdown, rest, repeat
                p = self.ped
                self.ped_steps = [("walk", now), ("fdw", now + p["walk"]),
                                  ("dont_walk", now + p["walk"] + p["fdw"]),
                                  (None, now + p["walk"] + p["fdw"] + p["dw"])]
            while self.ped_steps and now >= self.ped_steps[0][1]:
                state = self.ped_steps.pop(0)[0]
                if state:
                    self.set_ped(state)

    def restart_cycle(self):
        self.phase, self.phase_end, self.ped_steps = "off", 0.0, []

    # ---- routes

    def get(self, path, query):
        with self.lock:
            if path == "/get_config":
                return self.config()
            if path == "/get_current_state":
                return self.state()
            if path == "/identify":
                return {"project": "esp32_traffic_light_controler_sim", "firmware": "sim",
                        "spiffs": self.spiffs_version(), "mode": "sim"}
            if path == "/toggle_light_mode":
                self.blink = not self.blink
                self.next_blink = 0.0
                if self.blink and self.blink_color == "none":
                    self.blink_color = "all"
                if not self.blink:
                    self.restart_cycle()
                self.send({"light_mode": self.state()["light_mode"]})
                return {"message": "ok"}
            if path == "/blink_mode":
                color = (query.get("color") or [""])[0]
                if color not in ("red", "yellow", "green", "all", "random"):
                    return None
                self.blink, self.blink_color, self.next_blink = True, color, 0.0
                self.send({"light_mode": "blink_mode"})
                self.send({"blink_color": color})
                return {"message": "ok"}
            if path == "/toggle_theme_mode":
                self.cat = not self.cat
                self.send({"theme_mode": self.state()["theme_mode"]})
                self.send({"state": self.lamp()})
                return {"message": "ok"}
        return None

    def post(self, path, body):
        with self.lock:
            if path == "/reset_ota_state":
                return {"message": "ok"}
            if path == "/set_config":
                for key in self.cfg:
                    if key in body:
                        self.cfg[key] = body[key]
                return dict(self.config(), message="Config updated!")
            if path == "/ped_control":
                action = body.get("action")
                if action == "set_config":
                    for key in self.ped:
                        if key in body:
                            self.ped[key] = body[key]
                    self.ped["delay"] = max(0, min(30, int(self.ped["delay"])))
                elif action == "set_state":
                    self.ped_steps = []
                    self.set_ped(body.get("state", "dont_walk"))
                return self.ped_report()
            if path == "/test_mode":
                self.test_mode = bool(body.get("enabled"))
                if not self.test_mode:
                    self.restart_cycle()
                return {"test_mode": self.test_mode}
            if path == "/set_output":
                return {"error": "The simulated board has no relays"}
            if path == "/proximity_control":
                return {"state": "disabled", "connected": False, "baseline_valid": False}
        return None


def ws_frame(text):
    payload = text.encode("utf-8")
    head = b"\x81" + (bytes([len(payload)]) if len(payload) < 126 else b"\x7e" + struct.pack(">H", len(payload)))
    return head + payload


class Handler(SimpleHTTPRequestHandler):
    board = None

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(DATA), **kwargs)

    def log_message(self, fmt, *args):
        pass

    def end_headers(self):
        # Asset URLs carry a version that does not change between saves here.
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def json(self, body, status=200):
        raw = json.dumps(body).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def page(self, name, status=200):
        raw = (DATA / name).read_bytes()
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        url = urlparse(self.path)
        if url.path == "/ws":
            return self.websocket()
        if url.path == "/":
            return self.page("index.html")
        if url.path == "/update_firmware":
            return self.page("index_firmware_update.html")
        answer = self.board.get(url.path, parse_qs(url.query))
        if answer is not None:
            return self.json(answer)
        if (DATA / url.path.lstrip("/")).is_file():
            return super().do_GET()
        self.page("index_page_not_found.html", 404)

    def do_POST(self):
        raw = self.rfile.read(int(self.headers.get("Content-Length") or 0))
        try:
            body = json.loads(raw) if raw else {}
        except ValueError:
            return self.json({"error": "Invalid JSON"}, 400)
        answer = self.board.post(urlparse(self.path).path, body)
        if answer is None:
            return self.json({"error": "Not found"}, 404)
        self.json(answer)

    def websocket(self):
        key = self.headers.get("Sec-WebSocket-Key")
        if not key:
            return self.json({"error": "Expected a websocket"}, 400)
        accept = base64.b64encode(hashlib.sha1((key + WS_MAGIC).encode()).digest()).decode()
        self.send_response(101)
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        SimpleHTTPRequestHandler.end_headers(self)
        self.close_connection = True

        sock = self.connection
        with self.board.lock:
            self.board.clients.add(sock)
        try:
            # Like the firmware, ignore what the page sends; just notice it leaving.
            while True:
                head = self.rfile.read(2)
                if len(head) < 2 or head[0] & 0x0F == 0x8:
                    break
                size = head[1] & 0x7F
                if size == 126:
                    size = struct.unpack(">H", self.rfile.read(2))[0]
                elif size == 127:
                    size = struct.unpack(">Q", self.rfile.read(8))[0]
                self.rfile.read((4 if head[1] & 0x80 else 0) + size)
        except OSError:
            pass
        finally:
            with self.board.lock:
                self.board.clients.discard(sock)


def run_cycle(board):
    while True:
        board.tick(time.monotonic())
        time.sleep(0.05)


def watch_files(board):
    def snapshot():
        return {p: p.stat().st_mtime_ns for p in DATA.rglob("*") if p.is_file()}

    seen = snapshot()
    while True:
        time.sleep(0.5)
        try:
            now = snapshot()
        except OSError:
            continue    # a file vanished mid-save; look again next pass
        if now != seen:
            seen = now
            board.reloads += 1
            print("data/ changed - reloading open pages")
            board.drop_clients()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="127.0.0.1", help="0.0.0.0 to reach it from other devices")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()

    Handler.board = Board()
    for target in (run_cycle, watch_files):
        threading.Thread(target=target, args=(Handler.board,), daemon=True).start()

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    server.daemon_threads = True
    print("Simulated board at http://%s:%d  (Ctrl+C to stop)"
          % ("localhost" if args.host == "127.0.0.1" else args.host, args.port))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
