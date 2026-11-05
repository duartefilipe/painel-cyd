"""Uso de CPU, memoria, disco e rede do PC."""
import os
import time

import psutil


class Metrics:
    def __init__(self):
        psutil.cpu_percent(interval=None)  # a primeira leitura sempre volta 0
        self._net = psutil.net_io_counters()
        self._t = time.monotonic()
        self._disk = os.environ.get("SystemDrive", "C:") + "\\"

    def read(self) -> dict:
        now = time.monotonic()
        net = psutil.net_io_counters()
        dt = max(now - self._t, 0.001)
        dn = (net.bytes_recv - self._net.bytes_recv) / dt / 1024
        up = (net.bytes_sent - self._net.bytes_sent) / dt / 1024
        self._net, self._t = net, now

        mem = psutil.virtual_memory()
        disk = psutil.disk_usage(self._disk)
        return {
            "cpu": round(psutil.cpu_percent(interval=None), 1),
            "ram": round(mem.percent, 1),
            "ru": round(mem.used / 1024**3, 1),
            "rt": round(mem.total / 1024**3, 1),
            "dsk": round(disk.percent, 1),
            "dn": round(max(dn, 0), 1),
            "up": round(max(up, 0), 1),
        }
