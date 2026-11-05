"""Configuracao persistida em data/config.json. Tudo que o painel web muda passa por aqui."""
import json
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "data"
DATA.mkdir(exist_ok=True)
CONFIG_FILE = DATA / "config.json"

DEFAULTS = {
    "port": "auto",          # "auto" procura o CH340/CP210x da CYD; ou "COM3"
    "paused": False,         # True solta a porta (para gravar firmware)
    "brightness": 200,       # 0-255
    "invert": 0,             # alguns lotes da CYD vem com as cores invertidas
    "rotation": 1,           # 1 = paisagem; 3 = paisagem de cabeca para baixo
    "city": {
        "name": "Santa Maria",
        "admin": "Rio Grande do Sul",
        "lat": -29.6842,
        "lon": -53.8069,
    },
    "calendars": ["primary"],
    "days_ahead": 7,
    "web_port": 8765,
}

_lock = threading.Lock()


def load() -> dict:
    with _lock:
        cfg = json.loads(json.dumps(DEFAULTS))
        if CONFIG_FILE.exists():
            try:
                cfg.update(json.loads(CONFIG_FILE.read_text(encoding="utf-8")))
            except (OSError, ValueError):
                pass
        return cfg


def update(changes: dict) -> dict:
    cfg = load()
    for k, v in changes.items():
        if k in DEFAULTS:
            cfg[k] = v
    with _lock:
        CONFIG_FILE.write_text(json.dumps(cfg, indent=2, ensure_ascii=False), encoding="utf-8")
    return cfg
