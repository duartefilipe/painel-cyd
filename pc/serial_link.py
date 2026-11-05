"""Manda uma linha de JSON por segundo para a CYD e escuta o "OK" de volta."""
import json
import logging
import threading
import time

import serial
from serial.tools import list_ports

log = logging.getLogger("serial")

# USB-serial usados nas CYD: CH340 (1A86:7523) e CP210x (10C4:EA60).
KNOWN = {(0x1A86, 0x7523), (0x10C4, 0xEA60)}


def ports() -> list:
    return [
        {"device": p.device, "desc": p.description, "cyd": (p.vid, p.pid) in KNOWN}
        for p in list_ports.comports()
    ]


def find_cyd() -> str | None:
    for p in list_ports.comports():
        if (p.vid, p.pid) in KNOWN:
            return p.device
    return None


class SerialLink(threading.Thread):
    def __init__(self, get_config, build_payload, on_tick):
        super().__init__(daemon=True, name="serial")
        self.get_config = get_config
        self.build_payload = build_payload
        self.on_tick = on_tick          # roda todo segundo, mesmo com a porta solta
        self.ser = None
        self.status = {"state": "iniciando", "port": None, "error": None,
                       "last_sent": None, "last_ack": None, "sent": 0, "acks": 0}

    def _close(self):
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None

    def _open(self, port: str):
        s = serial.Serial()
        s.port, s.baudrate, s.timeout, s.write_timeout = port, 115200, 0, 2
        # DTR/RTS baixos: o circuito de auto-reset da CYD reinicia o ESP32 se
        # a porta abrir com eles em combinacao errada.
        s.dtr = False
        s.rts = False
        s.open()
        self.ser = s
        log.info("porta %s aberta", port)

    def run(self):
        next_retry = 0.0
        while True:
            t0 = time.time()
            try:
                self.on_tick()
            except Exception:
                log.exception("on_tick")
            cfg = self.get_config()

            if cfg.get("paused"):
                if self.ser:
                    self._close()
                self.status.update(state="pausado", port=None, error=None)
            else:
                if not self.ser and t0 >= next_retry:
                    port = find_cyd() if cfg.get("port", "auto") == "auto" else cfg["port"]
                    if not port:
                        self.status.update(state="sem placa", port=None, error="Nenhuma CYD encontrada no USB")
                        next_retry = t0 + 3
                    else:
                        try:
                            self._open(port)
                            self.status.update(state="conectado", port=port, error=None)
                        except Exception as e:
                            self.status.update(state="erro", port=port, error=str(e))
                            next_retry = t0 + 3
                if self.ser:
                    try:
                        line = json.dumps(self.build_payload(), separators=(",", ":"), ensure_ascii=True)
                        self.ser.write(line.encode("ascii") + b"\n")
                        self.status["last_sent"] = time.time()
                        self.status["sent"] += 1
                        data = self.ser.read(self.ser.in_waiting or 1)
                        if b"OK" in data:
                            self.status["last_ack"] = time.time()
                            self.status["acks"] += 1
                        if b"ERR" in data:
                            log.warning("placa: %s", data.decode(errors="replace").strip())
                    except Exception as e:
                        log.warning("serial caiu: %s", e)
                        self.status.update(state="erro", error=str(e))
                        self._close()
                        next_retry = t0 + 3

            time.sleep(max(0.05, 1.0 - (time.time() - t0)))
