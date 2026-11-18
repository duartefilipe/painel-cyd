"""Configuracao persistida em <dados>/config.json. Tudo que o painel web muda passa por aqui.

No container, <dados> e o volume montado em /data (variavel PAINEL_DATA).
"""
import json
import os
import threading
from pathlib import Path
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parent
DATA = Path(os.environ.get("PAINEL_DATA", ROOT.parent / "data"))
DATA.mkdir(parents=True, exist_ok=True)
CONFIG_FILE = DATA / "config.json"

# Fuso explicito: o container nao depende do relogio/zoneinfo do host.
TZ_NAME = os.environ.get("PAINEL_TZ", "America/Sao_Paulo")
TZ = ZoneInfo(TZ_NAME)

DEFAULTS = {
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
    # Telas: cada uma escolhe o que vai em cada regiao da CYD.
    #   left/right (160x124): sys | w | fx | none      (fx = cotacoes)
    #   bottom     (320x88):  ev | none
    # kind "clock": relogio de mesa em tela cheia com animacao (scene) na metade de baixo.
    #   scene: aquarium | stars | matrix | fire | none
    "screens": [
        {"id": "trabalho", "name": "Trabalho", "kind": "grid", "left": "sys", "right": "w", "bottom": "ev"},
        {"id": "anakin", "name": "Anakin", "kind": "grid", "left": "fx", "right": "w", "bottom": "ev"},
        {"id": "relogio", "name": "Relogio", "kind": "clock", "scene": "aquarium", "seconds": True},
    ],
    # auto: PC conectado -> pc_on, desconectado -> pc_off.  manual: sempre "manual".
    "display": {"mode": "auto", "manual": "trabalho", "pc_on": "trabalho", "pc_off": "anakin"},
    "pc_timeout": 10,        # segundos sem metricas do agente = PC desconectado
    # Ate 4 pares exibidos no bloco de cotacoes (ver quotes.KNOWN).
    "quotes": ["USD-BRL", "EUR-BRL", "BTC-BRL"],
}

_lock = threading.Lock()


def load() -> dict:
    with _lock:
        cfg = json.loads(json.dumps(DEFAULTS))
        if CONFIG_FILE.exists():
            try:
                saved = json.loads(CONFIG_FILE.read_text(encoding="utf-8"))
                for k, v in saved.items():
                    if k in DEFAULTS:
                        if isinstance(DEFAULTS[k], dict) and isinstance(v, dict) and k not in ("city",):
                            cfg[k].update(v)
                        else:
                            cfg[k] = v
            except (OSError, ValueError):
                pass
        return cfg


def update(changes: dict) -> dict:
    cfg = load()
    for k, v in changes.items():
        if k not in DEFAULTS:
            continue
        if isinstance(DEFAULTS[k], dict) and isinstance(v, dict) and k != "city":
            cfg[k].update({kk: vv for kk, vv in v.items() if kk in DEFAULTS[k]})
        else:
            cfg[k] = v
    with _lock:
        CONFIG_FILE.write_text(json.dumps(cfg, indent=2, ensure_ascii=False), encoding="utf-8")
    return cfg
