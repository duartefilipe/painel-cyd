"""Agente do PC: le CPU/RAM/disco/rede e manda para o servidor do painel a cada segundo.

E so isto que precisa rodar no PC. Enquanto ele envia, o servidor considera o PC
"conectado"; quando o PC desliga, o servidor troca de tela sozinho.

Configuracao em agente/agente.json (fora do git):
  {"server": "http://192.168.31.229:8765", "token": "<PAINEL_TOKEN>"}
ou pelas variaveis PAINEL_SERVER e PAINEL_TOKEN.
"""
import json
import logging
import os
import socket
import time
from pathlib import Path

import requests

from metrics import Metrics

HERE = Path(__file__).resolve().parent
CONF = HERE / "agente.json"

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s",
    handlers=[logging.FileHandler(HERE / "agente.log", encoding="utf-8"), logging.StreamHandler()],
)
log = logging.getLogger("agente")


def load_conf() -> tuple[str, str]:
    cfg = {}
    if CONF.exists():
        cfg = json.loads(CONF.read_text(encoding="utf-8"))
    server = os.environ.get("PAINEL_SERVER") or cfg.get("server", "")
    token = os.environ.get("PAINEL_TOKEN") or cfg.get("token", "")
    if not server or not token:
        raise SystemExit(f"Configure {CONF} (copie agente.exemplo.json) ou PAINEL_SERVER/PAINEL_TOKEN.")
    return server.rstrip("/"), token


def main():
    server, token = load_conf()
    s = requests.Session()
    s.headers["Authorization"] = f"Bearer {token}"
    m = Metrics()
    host = socket.gethostname()
    ok = None  # ultimo estado, para logar so as mudancas
    log.info("enviando metricas para %s", server)
    while True:
        t0 = time.time()
        try:
            r = s.post(f"{server}/api/metrics", json={**m.read(), "host": host}, timeout=3)
            if r.status_code == 401:
                if ok is not False:
                    log.error("token recusado pelo servidor (confira PAINEL_TOKEN)")
                ok = False
            else:
                r.raise_for_status()
                if ok is not True:
                    log.info("conectado ao servidor")
                ok = True
        except requests.RequestException as e:
            if ok is not False:
                log.warning("servidor inacessivel: %s", e)
            ok = False
        time.sleep(max(0.1, 1.0 - (time.time() - t0)))


if __name__ == "__main__":
    main()
