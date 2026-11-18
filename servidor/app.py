"""Painel CYD - servidor (roda no Docker do Portainer, sempre ligado).

Junta previsao do tempo, Google Agenda, cotacoes e as metricas que o agente
do PC envia, monta a tela escolhida e entrega para a CYD por HTTP na rede local.

Acesso:
  - CYD e agente:   cabecalho "Authorization: Bearer <PAINEL_TOKEN>"
  - painel web:     usuario qualquer, senha = PAINEL_TOKEN (login do navegador)
"""
import base64
import hmac
import logging
import os
import sys
import threading
import time
import unicodedata
from datetime import datetime, timedelta

from flask import Flask, Response, jsonify, redirect, request, send_from_directory

import config
import quotes
import weather
from config import DATA, ROOT, TZ
from gcal import GCal, NeedsReconnect

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(name)s %(levelname)s %(message)s",
    handlers=[logging.FileHandler(DATA / "painel.log", encoding="utf-8"), logging.StreamHandler()],
)
log = logging.getLogger("painel")
for noisy in ("werkzeug", "googleapiclient.discovery_cache", "waitress"):
    logging.getLogger(noisy).setLevel(logging.WARNING)

TOKEN = os.environ.get("PAINEL_TOKEN", "").strip()
if len(TOKEN) < 12:
    sys.exit("Defina PAINEL_TOKEN (12+ caracteres) no ambiente. Ex.: python -c \"import secrets;print(secrets.token_urlsafe(24))\"")

DIAS = ["seg", "ter", "qua", "qui", "sex", "sab", "dom"]
MESES = ["jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez"]

WEATHER_EVERY = 600     # 10 min
CALENDAR_EVERY = 120    # 2 min
QUOTES_EVERY = 300      # 5 min

SLOT_HALF = {"sys", "w", "fx", "none"}
SLOT_BOTTOM = {"ev", "none"}
KINDS = {"grid", "clock"}            # grid = 3 regioes; clock = relogio de mesa em tela cheia
SCENES = {"aquarium", "stars", "matrix", "fire", "none"}   # animacao na metade de baixo do relogio

state = {
    "sys": None, "sys_at": None, "agent": None,
    "weather": None, "weather_at": None, "weather_err": None,
    "events": [], "cal_at": None, "cal_status": "off", "cal_err": None,
    "quotes": [], "quotes_at": None, "quotes_err": None,
    "device": {"at": None, "ip": None, "n": 0, "screen": None},
}
lock = threading.Lock()
wake = {k: threading.Event() for k in ("weather", "calendar", "quotes")}
gcal = GCal()


def ascii_text(s: str, n: int = 47) -> str:
    """A fonte da CYD so tem ASCII: 'Reuniao' em vez de 'Reunião'."""
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode("ascii")
    return " ".join(s.split())[:n]


def when_label(ev: dict, now: datetime) -> tuple[str, bool]:
    start, end = ev["start"], ev["end"]
    ongoing = start <= now < end
    today = now.date()
    d = start.date()
    if ev["all_day"]:
        if ongoing or d <= today:
            return "hoje", ongoing
        if d == today + timedelta(days=1):
            return "amanha", False
        return f"{DIAS[d.weekday()]} {d.day:02d}", False
    if ongoing:
        return "agora", True
    hm = f"{start.hour}:{start.minute:02d}"
    if d == today:
        return hm, False
    if d == today + timedelta(days=1):
        return f"amanha {hm}", False
    return f"{DIAS[d.weekday()]} {hm}", False


# ---------- coletores ----------
def _loop(name: str, every: int, fn):
    def run():
        while True:
            try:
                fn()
            except Exception:
                log.exception("coletor %s", name)
            wake[name].wait(every)
            wake[name].clear()
    threading.Thread(target=run, daemon=True, name=name).start()


def collect_weather():
    city = config.load()["city"]
    try:
        w = weather.fetch(city["lat"], city["lon"])
        with lock:
            state.update(weather=w, weather_at=time.time(), weather_err=None)
    except Exception as e:
        log.warning("tempo: %s", e)
        with lock:
            state["weather_err"] = str(e)


def collect_calendar():
    cfg = config.load()
    if not gcal.connected():
        with lock:
            state.update(events=[], cal_status="off", cal_err=None)
        return
    try:
        evs = gcal.upcoming(cfg["calendars"] or ["primary"], cfg["days_ahead"])
        with lock:
            state.update(events=evs, cal_at=time.time(), cal_status="ok", cal_err=None)
    except Exception as e:
        if not isinstance(e, NeedsReconnect):
            log.warning("agenda: %s", e)
        with lock:
            state.update(cal_status="err", cal_err=str(e))


def collect_quotes():
    try:
        q = quotes.fetch(config.load()["quotes"])
        with lock:
            state.update(quotes=q, quotes_at=time.time(), quotes_err=None)
    except Exception as e:
        log.warning("cotacoes: %s", e)
        with lock:
            state["quotes_err"] = str(e)


# ---------- tela ----------
def pc_online(cfg: dict) -> bool:
    with lock:
        at = state["sys_at"]
    return bool(at) and time.time() - at < cfg["pc_timeout"]


def find_screen(cfg: dict, sid: str) -> dict:
    for s in cfg["screens"]:
        if s["id"] == sid:
            return s
    return cfg["screens"][0] if cfg["screens"] else {"id": "-", "name": "-", "kind": "grid", "left": "none", "right": "none", "bottom": "none"}


def active_screen(cfg: dict) -> dict:
    d = cfg["display"]
    if d["mode"] == "manual":
        return find_screen(cfg, d["manual"])
    return find_screen(cfg, d["pc_on"] if pc_online(cfg) else d["pc_off"])


def fx_block() -> dict:
    """Ate 4 linhas [rotulo, valor em R$ ja formatado, variacao %]."""
    with lock:
        q, err, at = list(state["quotes"]), state["quotes_err"], state["quotes_at"]
    if not q:
        return {"ok": 0, "msg": "sem cotacoes" if err else "buscando..."}
    hm = datetime.fromtimestamp(at, TZ).strftime("%H:%M") if at else ""
    return {"ok": 1, "at": hm, "l": [[x["lbl"], quotes.fmt_brl(x["bid"]), round(x["ch"], 2)] for x in q]}


def build_payload(screen: dict | None = None) -> dict:
    cfg = config.load()
    scr = screen or active_screen(cfg)
    now = datetime.now(TZ)
    online = pc_online(cfg)
    clock = scr.get("kind") == "clock"
    # O relogio mostra tempo e proximo compromisso numa linha cada.
    slots = {"w", "ev"} if clock else {scr.get("left"), scr.get("right"), scr.get("bottom")}
    with lock:
        w, evs, st, err, sysm = state["weather"], list(state["events"]), state["cal_status"], state["cal_err"], state["sys"]

    if clock:
        lay = {"n": ascii_text(scr["name"], 14), "f": "clock",
               "sc": scr.get("scene", "aquarium"), "sec": 1 if scr.get("seconds", True) else 0}
    else:
        lay = {"n": ascii_text(scr["name"], 14), "l": scr.get("left", "none"),
               "r": scr.get("right", "none"), "b": scr.get("bottom", "none")}
    p = {
        "t": now.strftime("%H:%M"),
        "d": f"{DIAS[now.weekday()]}, {now.day:02d} {MESES[now.month - 1]}",
        # segundos desde 1970 no horario LOCAL: a placa conta os segundos sozinha a partir daqui
        "ep": int(now.timestamp() + now.utcoffset().total_seconds()),
        "cfg": {"br": int(cfg["brightness"]), "inv": int(cfg["invert"]), "rot": int(cfg["rotation"])},
        "lay": lay,
        "pc": 1 if online else 0,
    }
    if "sys" in slots and online and sysm:
        p["sys"] = sysm
    if "w" in slots:
        if w:
            p["w"] = {
                "ok": 1, "city": ascii_text(cfg["city"]["name"], 22), "tc": w["tc"], "ic": w["ic"],
                "day": w["day"], "mn": w["mn"], "mx": w["mx"],
                "h": [[h["lbl"], h["temp"], h["ic"]] for h in w["hours"]],
            }
        else:
            p["w"] = {"ok": 0, "city": ascii_text(cfg["city"]["name"], 22)}
    if "fx" in slots:
        p["fx"] = fx_block()
    if "ev" in slots:
        lst = []
        for ev in evs:
            if ev["end"] <= now:
                continue
            lbl, on = when_label(ev, now)
            lst.append([lbl, ascii_text(ev["title"]), 1 if on else 0])
            if len(lst) == 4:
                break
        msg = ""
        if st == "err":
            msg = ascii_text(err or "erro na agenda", 38)
        elif st == "off":
            msg = "Google nao conectado"
        p["ev"] = {"st": st, "msg": msg, "l": lst}
    return p


def clean_screens(screens) -> list:
    out, seen = [], set()
    for s in screens if isinstance(screens, list) else []:
        sid = "".join(c for c in str(s.get("id", "")).lower() if c.isalnum() or c in "-_")[:24]
        if not sid or sid in seen:
            continue
        seen.add(sid)
        out.append({
            "id": sid,
            "name": str(s.get("name") or sid)[:24],
            "kind": s.get("kind") if s.get("kind") in KINDS else "grid",
            "left": s.get("left") if s.get("left") in SLOT_HALF else "none",
            "right": s.get("right") if s.get("right") in SLOT_HALF else "none",
            "bottom": s.get("bottom") if s.get("bottom") in SLOT_BOTTOM else "none",
            "scene": s.get("scene") if s.get("scene") in SCENES else "aquarium",
            "seconds": bool(s.get("seconds", True)),
        })
    if not out:
        raise ValueError("Precisa existir pelo menos uma tela")
    return out


# ---------- web ----------
app = Flask(__name__, static_folder=None)
OPEN = {"/healthz"}


def _ok_token(given: str) -> bool:
    return bool(given) and hmac.compare_digest(given.encode(), TOKEN.encode())


@app.before_request
def auth():
    if request.path in OPEN:
        return None
    h = request.headers.get("Authorization", "")
    if h.startswith("Bearer ") and _ok_token(h[7:].strip()):
        return None
    if h.startswith("Basic "):
        try:
            _, _, pw = base64.b64decode(h[6:]).decode().partition(":")
            if _ok_token(pw):
                return None
        except Exception:
            pass
    return Response("Login necessario (senha = PAINEL_TOKEN)", 401, {"WWW-Authenticate": 'Basic realm="Painel CYD"'})


@app.get("/healthz")
def healthz():
    return "ok"


@app.get("/")
def index():
    return send_from_directory(ROOT / "web", "index.html")


@app.get("/api/screen")
def api_screen():
    """O que a CYD busca a cada segundo."""
    cfg = config.load()
    scr = active_screen(cfg)
    with lock:
        d = state["device"]
        d.update(at=time.time(), ip=request.remote_addr, n=d["n"] + 1, screen=scr["id"])
    return jsonify(build_payload(scr))


@app.post("/api/metrics")
def api_metrics():
    """O agente do PC manda CPU/RAM/disco/rede a cada segundo."""
    b = request.get_json(force=True, silent=True) or {}
    m = {k: float(b[k]) for k in ("cpu", "ram", "ru", "rt", "dsk", "dn", "up") if isinstance(b.get(k), (int, float))}
    if "cpu" not in m:
        return jsonify({"error": "metricas invalidas"}), 400
    with lock:
        state.update(sys=m, sys_at=time.time(), agent={"ip": request.remote_addr, "host": ascii_text(str(b.get("host", "")), 32)})
    return jsonify({"ok": True})


@app.get("/api/state")
def api_state():
    cfg = config.load()
    with lock:
        s = {k: (dict(v) if isinstance(v, dict) else v) for k, v in state.items()}
    now = datetime.now(TZ)
    events = []
    for ev in s["events"]:
        lbl, on = when_label(ev, now)
        events.append({**ev, "start": ev["start"].isoformat(), "end": ev["end"].isoformat(), "label": lbl, "now": on})
    account = None
    if gcal.connected() and s["cal_status"] == "ok":
        try:
            account = gcal.account_email()
        except Exception:
            pass
    active = active_screen(cfg)
    return jsonify({
        "now": time.time(),
        "config": cfg,
        "pc": {"online": pc_online(cfg), "at": s["sys_at"], "agent": s["agent"]},
        "device": s["device"],
        "active": active["id"],
        "sys": s["sys"],
        "weather": s["weather"], "weather_at": s["weather_at"], "weather_err": s["weather_err"],
        "quotes": s["quotes"], "quotes_at": s["quotes_at"], "quotes_err": s["quotes_err"],
        "quote_pairs": list(quotes.KNOWN),
        "google": {
            "has_secret": gcal.has_secret(), "connected": gcal.connected(),
            "status": s["cal_status"], "error": s["cal_err"], "account": account,
            "updated_at": s["cal_at"],
        },
        "events": events,
        "payloads": {sc["id"]: build_payload(sc) for sc in cfg["screens"]},
    })


@app.post("/api/config")
def api_config():
    changes = request.get_json(force=True) or {}
    if "screens" in changes:
        try:
            changes["screens"] = clean_screens(changes["screens"])
        except ValueError as e:
            return jsonify({"error": str(e)}), 400
    if "display" in changes:
        d = changes["display"]
        if d.get("mode") not in (None, "auto", "manual"):
            return jsonify({"error": "modo invalido"}), 400
    if "quotes" in changes:
        q = [p for p in changes["quotes"] if p in quotes.KNOWN] if isinstance(changes["quotes"], list) else []
        changes["quotes"] = list(dict.fromkeys(q))[:4]
    before = config.load()
    cfg = config.update(changes)
    if cfg["city"] != before["city"]:
        wake["weather"].set()
    if cfg["calendars"] != before["calendars"] or cfg["days_ahead"] != before["days_ahead"]:
        wake["calendar"].set()
    if cfg["quotes"] != before["quotes"]:
        wake["quotes"].set()
    return jsonify(cfg)


@app.get("/api/geocode")
def api_geocode():
    try:
        return jsonify(weather.geocode(request.args.get("q", "")))
    except Exception as e:
        return jsonify({"error": str(e)}), 502


@app.post("/api/refresh")
def api_refresh():
    for e in wake.values():
        e.set()
    return jsonify({"ok": True})


@app.post("/api/google/secret")
def api_secret():
    f = request.files.get("file")
    if not f:
        return jsonify({"error": "Nenhum arquivo enviado"}), 400
    try:
        gcal.save_secret(f.read())
    except Exception as e:
        return jsonify({"error": str(e)}), 400
    return jsonify({"ok": True})


@app.post("/api/google/token")
def api_token():
    f = request.files.get("file")
    if not f:
        return jsonify({"error": "Nenhum arquivo enviado"}), 400
    try:
        gcal.save_token(f.read())
    except Exception as e:
        return jsonify({"error": str(e)}), 400
    wake["calendar"].set()
    return jsonify({"ok": True})


@app.get("/auth/google")
def auth_google():
    if not gcal.has_secret():
        return redirect("/")
    return redirect(gcal.auth_url())


@app.post("/api/google/finish")
def api_google_finish():
    url = (request.get_json(force=True, silent=True) or {}).get("url", "")
    try:
        gcal.finish_auth(url)
    except Exception as e:
        log.warning("oauth: %s", e)
        return jsonify({"error": str(e)}), 400
    wake["calendar"].set()
    return jsonify({"ok": True})


@app.post("/api/google/disconnect")
def api_disconnect():
    gcal.disconnect()
    wake["calendar"].set()
    return jsonify({"ok": True})


@app.get("/api/calendars")
def api_calendars():
    try:
        return jsonify(gcal.calendars())
    except NeedsReconnect as e:
        return jsonify({"error": str(e)}), 401
    except Exception as e:
        return jsonify({"error": str(e)}), 502


@app.post("/api/events")
def api_events():
    b = request.get_json(force=True) or {}
    title = (b.get("title") or "").strip()
    if not title or not b.get("date"):
        return jsonify({"error": "Titulo e data sao obrigatorios"}), 400
    try:
        ev = gcal.create(b.get("calendar") or "primary", title, b["date"], b.get("time") or None, int(b.get("minutes") or 60))
    except NeedsReconnect as e:
        return jsonify({"error": str(e)}), 401
    except Exception as e:
        return jsonify({"error": str(e)}), 502
    wake["calendar"].set()
    return jsonify({"ok": True, "link": ev.get("htmlLink")})


def main():
    port = int(os.environ.get("PAINEL_PORT", "8765"))
    _loop("weather", WEATHER_EVERY, collect_weather)
    _loop("calendar", CALENDAR_EVERY, collect_calendar)
    _loop("quotes", QUOTES_EVERY, collect_quotes)
    log.info("painel em http://0.0.0.0:%s", port)
    from waitress import serve
    serve(app, host="0.0.0.0", port=port, threads=8)


if __name__ == "__main__":
    main()
