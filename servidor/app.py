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

SLOT_HALF = {"sys", "w", "wd", "fx", "none"}   # wd = tempo detalhado
SLOT_BOTTOM = {"ev", "none"}
# grid = 3 regioes; clock = relogio de mesa (animacao em tela cheia + hora pixelada);
# pixel = faixa de LEDs 32x8 com "apps" que se alternam
# station = estacao do tempo (ilustracao animada + hora em 7 segmentos + umidade/temperatura grandes)
KINDS = {"grid", "clock", "pixel", "station"}
SCENES = {"aquarium", "stars", "matrix", "fire", "rain", "sunset", "snow", "aurora", "none"}   # animacao de fundo do relogio
FONTS = {"pixel": 0, "dots": 1, "segment": 2, "arcade": 3, "clean": 4}
APP_TYPES = {"clock", "date", "weather", "quote", "event", "cpu", "text", "rain", "sun", "tomorrow", "humidity"}
MAX_NOTES = 5
TRANSITIONS = {"slide", "none"}
MAX_APPS = 8

# Fonte e icones pixelados (gerados por tools/pixel_assets.py), os mesmos do firmware.
import json as _json
PIXEL = _json.loads((ROOT / "pixel_assets.json").read_text(encoding="utf-8"))
PX_CHARS = set(PIXEL["font"])
PX_ICONS = set(PIXEL["icons"])
WEATHER_ICON = {"S": "sun", "P": "partly", "C": "cloud", "F": "fog", "R": "rain", "T": "storm", "N": "snow"}
QUOTE_ICON = {"USD": "dollar", "EUR": "euro", "BTC": "bitcoin"}
APP_ICON = {"clock": "clock", "date": "calendar", "event": "calendar", "cpu": "cpu", "text": "heart", "quote": "dollar",
            "rain": "rain", "sun": "sun", "tomorrow": "partly", "humidity": "drop"}


def px_text(s: str, n: int = 60) -> str:
    """Texto que a fonte pixelada sabe desenhar: maiusculas, sem acento, ° vira ~."""
    s = ascii_text(str(s).replace("°", "~"), n).upper()
    return "".join(c if c in PX_CHARS else " " for c in s)


def hex565(h: str, default: int = 0xFFE0) -> int:
    try:
        h = h.lstrip("#")
        r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3) or 1
    except (ValueError, IndexError, AttributeError):
        return default


def clean_hex(h, default: str) -> str:
    h = str(h or "")
    return h.lower() if len(h) == 7 and h[0] == "#" and all(c in "0123456789abcdefABCDEF" for c in h[1:]) else default

state = {
    "sys": None, "sys_at": None, "agent": None,
    "weather": None, "weather_at": None, "weather_err": None,
    "events": [], "cal_at": None, "cal_status": "off", "cal_err": None,
    "quotes": [], "quotes_at": None, "quotes_err": None,
    "device": {"at": None, "ip": None, "n": 0, "screen": None, "ldr": None},
    "notes": [],            # avisos ativos: {id, text, icon, color, until, at}
    "override": None,       # {screen, until}: tela escolhida pelo toque
}
note_seq = 0
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


def quote_pairs(cfg: dict) -> list:
    """Pares do card de cotacoes + os usados em apps de telas pixel."""
    pairs = list(cfg["quotes"])
    for s in cfg["screens"]:
        for a in s.get("apps", []) if s.get("kind") == "pixel" else []:
            if a.get("type") == "quote" and a.get("pair") in quotes.KNOWN and a["pair"] not in pairs:
                pairs.append(a["pair"])
    return pairs


def collect_quotes():
    try:
        q = quotes.fetch(quote_pairs(config.load()), limit=8)
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


def _minutes(hhmm: str) -> int:
    try:
        h, m = str(hhmm).split(":")
        return int(h) * 60 + int(m)
    except ValueError:
        return -1


def schedule_rule(cfg: dict, now: datetime | None = None) -> dict | None:
    """Primeira regra de horario que vale agora (de/ate podem passar da meia-noite)."""
    sch = cfg["schedule"]
    if not sch.get("enabled"):
        return None
    now = now or datetime.now(TZ)
    m = now.hour * 60 + now.minute
    for r in sch.get("rules", []):
        a, b = _minutes(r.get("from", "")), _minutes(r.get("to", ""))
        if a < 0 or b < 0 or a == b:
            continue
        if (a < b and a <= m < b) or (a > b and (m >= a or m < b)):
            return r
    return None


def touch_override() -> dict | None:
    with lock:
        ov = state["override"]
        if ov and time.time() >= ov["until"]:
            state["override"] = ov = None
    return ov


def mode_screen(cfg: dict) -> dict:
    """Tela pelo modo automatico/manual, sem toque nem horario."""
    d = cfg["display"]
    if d["mode"] == "manual":
        return find_screen(cfg, d["manual"])
    return find_screen(cfg, d["pc_on"] if pc_online(cfg) else d["pc_off"])


def active_screen(cfg: dict) -> dict:
    # prioridade: toque (temporario) > regra de horario > automatico/manual
    ov = touch_override()
    if ov:
        return find_screen(cfg, ov["screen"])
    r = schedule_rule(cfg)
    if r and r.get("screen"):
        return find_screen(cfg, r["screen"])
    return mode_screen(cfg)


def display_cfg(cfg: dict) -> dict:
    """Brilho/cores para a placa, ja com a regra de horario e o brilho automatico."""
    li = cfg["light"]
    out = {"br": int(cfg["brightness"]), "inv": int(cfg["invert"]), "rot": int(cfg["rotation"]),
           "ab": 1 if li.get("auto") else 0, "ld": int(li.get("dark", 3000)), "lb": int(li.get("bright", 300)),
           "bm": int(li.get("min", 15)), "bx": int(li.get("max", 255)), "off": 0}
    r = None if touch_override() else schedule_rule(cfg)  # tocar acorda a tela mesmo num horario "apagado"
    if r:
        if r.get("off"):
            out["off"] = 1
        b = int(r.get("brightness", -1))
        if b >= 0:  # regra com brilho fixo: vale mais que o automatico
            out.update(br=b, ab=0)
    return out


def current_note() -> dict | None:
    now = time.time()
    with lock:
        state["notes"] = [n for n in state["notes"] if n["until"] > now]
        return state["notes"][0] if state["notes"] else None


def fx_block(cfg: dict) -> dict:
    """Ate 4 linhas [rotulo, valor em R$ ja formatado, variacao %]."""
    with lock:
        q, err, at = list(state["quotes"]), state["quotes_err"], state["quotes_at"]
    q = [x for x in q if x["pair"] in cfg["quotes"]][:4]  # state tambem tem pares dos apps pixel
    if not q:
        return {"ok": 0, "msg": "sem cotacoes" if err else "buscando..."}
    hm = datetime.fromtimestamp(at, TZ).strftime("%H:%M") if at else ""
    return {"ok": 1, "at": hm, "l": [[x["lbl"], quotes.fmt_brl(x["bid"]), round(x["ch"], 2)] for x in q]}


def next_event(evs: list, now: datetime):
    for ev in evs:
        if ev["end"] > now:
            return ev, when_label(ev, now)
    return None, ("", False)


def pixel_apps(scr: dict, cfg: dict, now: datetime, online: bool) -> list:
    """Cada app vira [icone, cor565, segundos, eh_relogio, texto]. Apps sem dado ficam de fora."""
    with lock:
        w, evs, sysm, qs = state["weather"], list(state["events"]), state["sys"], {q["pair"]: q for q in state["quotes"]}
    out = []
    for a in scr.get("apps", []):
        t, icon, text, clk = a.get("type"), a.get("icon", "auto"), "", 0
        auto = APP_ICON.get(t, "heart")
        if t == "clock":
            clk = 1
        elif t == "date":
            text = f"{DIAS[now.weekday()]} {now.day:02d}"
        elif t == "weather":
            if not w:
                continue
            text = f"{w['tc']}~C"
            auto = "moon" if w["ic"] == "S" and not w["day"] else WEATHER_ICON.get(w["ic"], "cloud")
        elif t == "quote":
            q = qs.get(a.get("pair", "USD-BRL"))
            if not q:
                continue
            text = f"{q['bid'] / 1000:.0f}K" if q["bid"] >= 1000 else quotes.fmt_brl(q["bid"])
            auto = QUOTE_ICON.get(q["lbl"], "dollar")
        elif t == "event":
            ev, (lbl, _) = next_event(evs, now)
            if not ev:
                continue
            text = f"{lbl} {ev['title']}"
        elif t == "cpu":
            if not (online and sysm):
                continue
            text = f"{sysm['cpu']:.0f}%"
        elif t == "text":
            text = a.get("text", "")
        elif t in ("rain", "sun", "tomorrow", "humidity"):
            if not w:
                continue
            if t == "rain":
                text = f"{w.get('rain') or 0}% ({w.get('rain_max') or 0}%)"
            elif t == "humidity":
                text = f"{w.get('hum') or 0}%"
            elif t == "sun":
                # mostra o proximo evento do sol: nascer de manha, por depois do meio-dia
                hm = now.strftime("%H:%M")
                if w.get("sunrise") and hm < w["sunrise"]:
                    text, auto = w["sunrise"], "sunrise"
                else:
                    text, auto = w.get("sunset", ""), "sunset"
            else:
                tm = w.get("tomorrow") or {}
                text = f"{tm.get('mn', 0)}/{tm.get('mx', 0)}~ {tm.get('rain', 0)}%"
                auto = WEATHER_ICON.get(tm.get("ic", "C"), "cloud")
        out.append([auto if icon == "auto" else icon, hex565(a.get("color", "#ffffff")),
                    int(a.get("dur", 6)), clk, px_text(text, 60)])
    return out


def build_payload(screen: dict | None = None) -> dict:
    cfg = config.load()
    scr = screen or active_screen(cfg)
    now = datetime.now(TZ)
    online = pc_online(cfg)
    kind = scr.get("kind", "grid")
    # O relogio de mesa mostra tempo e proximo compromisso; o pixel monta os proprios textos.
    if kind == "clock":
        slots = {"w", "ev"}
    elif kind == "station":
        slots = {"w"}
    elif kind == "pixel":
        slots = set()
    else:
        slots = {scr.get("left"), scr.get("right"), scr.get("bottom")}
        if "wd" in slots:
            slots.add("w")  # o detalhado usa o mesmo bloco de tempo
    with lock:
        w, evs, st, err, sysm = state["weather"], list(state["events"]), state["cal_status"], state["cal_err"], state["sys"]

    if kind == "clock":
        lay = {"n": ascii_text(scr["name"], 14), "f": "clock",
               "sc": scr.get("scene", "aquarium"), "sec": 1 if scr.get("seconds", True) else 0,
               "col": hex565(scr.get("color", "#ffd23c")),
               "fn": FONTS.get(scr.get("font", "pixel"), 0),
               "cs": int(scr.get("csize", 12)), "cx": int(scr.get("cx", -1)), "cy": int(scr.get("cy", 14)),
               "ds": int(scr.get("dsize", 3)), "dx": int(scr.get("dx", 12)), "dy": int(scr.get("dy", 88)),
               "ws": int(scr.get("wsize", 3)), "wx": int(scr.get("wx", 238)), "wy": int(scr.get("wy", 88)),
               "es": int(scr.get("esize", 3)), "ex": int(scr.get("ex", 12)), "ey": int(scr.get("ey", 110))}
    elif kind == "station":
        lay = {"n": ascii_text(scr["name"], 14), "f": "station",
               "col": hex565(scr.get("color", "#ffffff")),     # hora
               "col2": hex565(scr.get("color2", "#5ad2ff")),   # umidade e temperatura
               "fn": FONTS.get(scr.get("font", "segment"), 2),
               "sec": 1 if scr.get("seconds", True) else 0,
               # dia da semana de 0 (domingo) a 6, para destacar na fileira DOM..SAB
               "wd": (now.weekday() + 1) % 7}
    elif kind == "pixel":
        lay = {"n": ascii_text(scr["name"], 14), "f": "pixel", "tr": scr.get("trans", "slide"),
               "a": pixel_apps(scr, cfg, now, online)}
    else:
        lay = {"n": ascii_text(scr["name"], 14), "l": scr.get("left", "none"),
               "r": scr.get("right", "none"), "b": scr.get("bottom", "none")}
    p = {
        "t": now.strftime("%H:%M"),
        "d": f"{DIAS[now.weekday()]}, {now.day:02d} {MESES[now.month - 1]}",
        # segundos desde 1970 no horario LOCAL: a placa conta os segundos sozinha a partir daqui
        "ep": int(now.timestamp() + now.utcoffset().total_seconds()),
        "cfg": display_cfg(cfg),
        "lay": lay,
        "pc": 1 if online else 0,
    }
    nt = current_note()
    if nt:
        p["nt"] = {"id": nt["id"], "tx": nt["text"], "ic": nt["icon"], "c": hex565(nt["color"]),
                   "s": max(1, int(nt["until"] - time.time()))}
    if "sys" in slots and online and sysm:
        p["sys"] = sysm
    if "w" in slots:
        if w:
            tm = w.get("tomorrow") or {}
            p["w"] = {
                "ok": 1, "city": ascii_text(cfg["city"]["name"], 22), "tc": w["tc"], "ic": w["ic"],
                "td": w.get("tcd", w["tc"]),  # temperatura com uma casa decimal
                "day": w["day"], "mn": w["mn"], "mx": w["mx"],
                # detalhes: umidade, chuva agora / maxima do dia, nascer e por do sol, amanha
                "hu": w.get("hum") or 0, "rp": w.get("rain") or 0, "rx": w.get("rain_max") or 0,
                "sr": w.get("sunrise", ""), "ss": w.get("sunset", ""),
                "tm": [tm.get("mn", 0), tm.get("mx", 0), tm.get("ic", "C"), tm.get("rain", 0)],
                "h": [[h["lbl"], h["temp"], h["ic"], h.get("rain") or 0] for h in w["hours"]],
            }
        else:
            p["w"] = {"ok": 0, "city": ascii_text(cfg["city"]["name"], 22)}
    if "fx" in slots:
        p["fx"] = fx_block(cfg)
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


DEFAULT_APPS = [
    {"type": "clock", "icon": "auto", "color": "#ffffff", "dur": 10},
    {"type": "weather", "icon": "auto", "color": "#ffd23c", "dur": 5},
    {"type": "date", "icon": "auto", "color": "#64a5ff", "dur": 4},
    {"type": "event", "icon": "auto", "color": "#ff8c28", "dur": 8},
]


def clean_apps(apps) -> list:
    if not isinstance(apps, list):
        return [dict(a) for a in DEFAULT_APPS]
    out = []
    for a in apps[:MAX_APPS]:
        if not isinstance(a, dict) or a.get("type") not in APP_TYPES:
            continue
        try:
            dur = max(2, min(60, int(a.get("dur", 6))))
        except (TypeError, ValueError):
            dur = 6
        out.append({
            "type": a["type"],
            "icon": a.get("icon") if a.get("icon") in PX_ICONS or a.get("icon") == "none" else "auto",
            "color": clean_hex(a.get("color"), "#ffffff"),
            "dur": dur,
            "text": px_text(a.get("text", ""), 60) if a["type"] == "text" else "",
            "pair": a.get("pair") if a.get("pair") in quotes.KNOWN else "USD-BRL",
        })
    return out


def _clamp(v, lo: int, hi: int, default: int) -> int:
    try:
        return max(lo, min(hi, int(v)))
    except (TypeError, ValueError):
        return default


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
            "color": clean_hex(s.get("color"), "#ffd23c"),
            "font": s.get("font") if s.get("font") in FONTS else "pixel",
            "csize": _clamp(s.get("csize", 12), 4, 18, 12),
            "cx": _clamp(s.get("cx", -1), -1, 300, -1),
            "cy": _clamp(s.get("cy", 14), 0, 225, 14),
            "dsize": _clamp(s.get("dsize", 3), 0, 8, 3),
            "dx": _clamp(s.get("dx", 12), 0, 300, 12),
            "dy": _clamp(s.get("dy", 88), 0, 228, 88),
            "wsize": _clamp(s.get("wsize", 3), 0, 8, 3),
            "wx": _clamp(s.get("wx", 238), 0, 300, 238),
            "wy": _clamp(s.get("wy", 88), 0, 228, 88),
            "esize": _clamp(s.get("esize", 3), 0, 8, 3),
            "ex": _clamp(s.get("ex", 12), 0, 300, 12),
            "ey": _clamp(s.get("ey", 110), 0, 228, 110),
            "trans": s.get("trans") if s.get("trans") in TRANSITIONS else "slide",
            "apps": clean_apps(s.get("apps")),
            "color2": clean_hex(s.get("color2"), "#5ad2ff"),
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


@app.get("/api/pixel_assets")
def api_pixel_assets():
    """Fonte e icones pixelados para a previa do painel (mesmos dados do firmware)."""
    return jsonify(PIXEL)


@app.get("/api/screen")
def api_screen():
    """O que a CYD busca a cada segundo."""
    cfg = config.load()
    scr = active_screen(cfg)
    ldr = request.args.get("ldr", type=int)  # leitura do sensor de luz, para calibrar pelo painel
    with lock:
        d = state["device"]
        d.update(at=time.time(), ip=request.remote_addr, n=d["n"] + 1, screen=scr["id"])
        if ldr is not None:
            d["ldr"] = ldr
    return jsonify(build_payload(scr))


# ---------- avisos ----------
@app.post("/api/notify")
def api_notify():
    """Mostra um aviso por cima de qualquer tela. Ex.:
    curl -H "Authorization: Bearer $TOKEN" -d '{"text":"Scan terminou","icon":"check","color":"#50c878"}' .../api/notify
    """
    global note_seq
    b = request.get_json(force=True, silent=True) or {}
    text = px_text(b.get("text", ""), 80).strip()
    if not text:
        return jsonify({"error": "campo 'text' obrigatorio"}), 400
    icon = b.get("icon") if b.get("icon") in PX_ICONS else "bell"
    dur = _clamp(b.get("dur", 15), 3, 3600, 15)
    with lock:
        note_seq += 1
        note = {"id": note_seq, "text": text, "icon": icon, "color": clean_hex(b.get("color"), "#ffffff"),
                "until": time.time() + dur, "at": time.time()}
        state["notes"] = (state["notes"] + [note])[-MAX_NOTES:]
    log.info("aviso %d: %s", note_seq, text)
    return jsonify({"ok": True, "id": note_seq})


@app.get("/api/notify")
def api_notify_list():
    current_note()  # limpa os vencidos
    with lock:
        return jsonify(list(state["notes"]))


@app.delete("/api/notify")
def api_notify_clear():
    with lock:
        state["notes"] = []
    return jsonify({"ok": True})


# ---------- toque na tela ----------
@app.post("/api/touch")
def api_touch():
    """A placa avisa um toque. next: proxima tela por touch_minutes; wake: acende a tela
    apagada pelo horario; auto: volta ao normal; dismiss: fecha o aviso que esta na tela."""
    a = (request.get_json(force=True, silent=True) or {}).get("a", "")
    cfg = config.load()
    if a == "dismiss":
        with lock:
            if state["notes"]:
                state["notes"].pop(0)
    elif a == "next":
        ids = [s["id"] for s in cfg["screens"]]
        cur = active_screen(cfg)["id"]
        nxt = ids[(ids.index(cur) + 1) % len(ids)] if cur in ids else ids[0]
        with lock:
            state["override"] = {"screen": nxt, "until": time.time() + cfg["touch_minutes"] * 60}
    elif a == "wake":
        # tela apagada pelo horario: acende mostrando a tela normal por touch_minutes
        with lock:
            state["override"] = {"screen": mode_screen(cfg)["id"], "until": time.time() + cfg["touch_minutes"] * 60}
    elif a == "auto":
        with lock:
            state["override"] = None
    else:
        return jsonify({"error": "acao invalida"}), 400
    return jsonify({"ok": True, "screen": active_screen(cfg)["id"]})


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
    ov = touch_override()
    return jsonify({
        "now": time.time(),
        "config": cfg,
        "notes": s["notes"],
        "override": ov,
        "rule": schedule_rule(cfg),
        "display": display_cfg(cfg),
        "icons": sorted(PX_ICONS),
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
    if "light" in changes:
        li = changes["light"] if isinstance(changes["light"], dict) else {}
        changes["light"] = {k: v for k, v in {
            "auto": bool(li["auto"]) if "auto" in li else None,
            "dark": _clamp(li["dark"], 0, 4095, 3000) if "dark" in li else None,
            "bright": _clamp(li["bright"], 0, 4095, 300) if "bright" in li else None,
            "min": _clamp(li["min"], 0, 255, 15) if "min" in li else None,
            "max": _clamp(li["max"], 0, 255, 255) if "max" in li else None,
        }.items() if v is not None}
    if "schedule" in changes:
        sc = changes["schedule"] if isinstance(changes["schedule"], dict) else {}
        out = {}
        if "enabled" in sc:
            out["enabled"] = bool(sc["enabled"])
        if "rules" in sc:
            rules = []
            for r in sc["rules"] if isinstance(sc["rules"], list) else []:
                if _minutes(r.get("from", "")) < 0 or _minutes(r.get("to", "")) < 0:
                    return jsonify({"error": "horario invalido (use HH:MM)"}), 400
                rules.append({"from": r["from"], "to": r["to"], "screen": str(r.get("screen") or "")[:24],
                              "brightness": _clamp(r.get("brightness", -1), -1, 255, -1), "off": bool(r.get("off"))})
            out["rules"] = rules[:8]
        changes["schedule"] = out
    if "touch_minutes" in changes:
        changes["touch_minutes"] = _clamp(changes["touch_minutes"], 1, 240, 15)
    before = config.load()
    cfg = config.update(changes)
    if cfg["city"] != before["city"]:
        wake["weather"].set()
    if cfg["calendars"] != before["calendars"] or cfg["days_ahead"] != before["days_ahead"]:
        wake["calendar"].set()
    if quote_pairs(cfg) != quote_pairs(before):
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
