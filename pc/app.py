"""Painel CYD — programa do PC.

Junta metricas do sistema, previsao do tempo e Google Agenda, manda tudo para a
CYD pela serial e serve um painel de controle em http://localhost:8765.
O servidor escuta so em 127.0.0.1: ele tem acesso a sua agenda.
"""
import logging
import threading
import time
import unicodedata
import webbrowser
from datetime import datetime, timedelta

from flask import Flask, jsonify, redirect, request, send_from_directory

import config
import weather
from config import DATA, ROOT
from gcal import GCal, NeedsReconnect
from metrics import Metrics
from serial_link import SerialLink, ports

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(name)s %(levelname)s %(message)s",
    handlers=[logging.FileHandler(DATA / "painel.log", encoding="utf-8"), logging.StreamHandler()],
)
log = logging.getLogger("painel")
for noisy in ("werkzeug", "googleapiclient.discovery_cache"):
    logging.getLogger(noisy).setLevel(logging.WARNING)

DIAS = ["seg", "ter", "qua", "qui", "sex", "sab", "dom"]
MESES = ["jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez"]

WEATHER_EVERY = 600     # 10 min
CALENDAR_EVERY = 120    # 2 min

state = {
    "sys": None,
    "weather": None, "weather_at": None, "weather_err": None,
    "events": [], "cal_at": None, "cal_status": "off", "cal_err": None,
}
lock = threading.Lock()
wake_weather = threading.Event()
wake_calendar = threading.Event()
gcal = GCal()
metrics = Metrics()


def ascii_text(s: str, n: int = 47) -> str:
    """A fonte da CYD so tem ASCII: 'Reunião' vira 'Reuniao'."""
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
def weather_loop():
    while True:
        city = config.load()["city"]
        try:
            w = weather.fetch(city["lat"], city["lon"])
            with lock:
                state.update(weather=w, weather_at=time.time(), weather_err=None)
        except Exception as e:
            log.warning("tempo: %s", e)
            with lock:
                state["weather_err"] = str(e)
        wake_weather.wait(WEATHER_EVERY)
        wake_weather.clear()


def calendar_loop():
    while True:
        cfg = config.load()
        if not gcal.connected():
            with lock:
                state.update(events=[], cal_status="off", cal_err=None)
        else:
            try:
                evs = gcal.upcoming(cfg["calendars"] or ["primary"], cfg["days_ahead"])
                with lock:
                    state.update(events=evs, cal_at=time.time(), cal_status="ok", cal_err=None)
            except NeedsReconnect as e:
                with lock:
                    state.update(cal_status="err", cal_err=str(e))
            except Exception as e:
                log.warning("agenda: %s", e)
                with lock:
                    state.update(cal_status="err", cal_err=str(e))
        wake_calendar.wait(CALENDAR_EVERY)
        wake_calendar.clear()


def tick():
    m = metrics.read()
    with lock:
        state["sys"] = m


def build_payload() -> dict:
    cfg = config.load()
    now = datetime.now().astimezone()
    with lock:
        w, evs, st, err, sysm = state["weather"], list(state["events"]), state["cal_status"], state["cal_err"], state["sys"]

    p = {
        "t": now.strftime("%H:%M"),
        "d": f"{DIAS[now.weekday()]}, {now.day:02d} {MESES[now.month - 1]}",
        "cfg": {"br": int(cfg["brightness"]), "inv": int(cfg["invert"]), "rot": int(cfg["rotation"])},
    }
    if sysm:
        p["sys"] = sysm
    if w:
        p["w"] = {
            "ok": 1, "city": ascii_text(cfg["city"]["name"], 22), "tc": w["tc"], "ic": w["ic"],
            "day": w["day"], "mn": w["mn"], "mx": w["mx"],
            "h": [[h["lbl"], h["temp"], h["ic"]] for h in w["hours"]],
        }
    else:
        p["w"] = {"ok": 0, "city": ascii_text(cfg["city"]["name"], 22)}

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


# ---------- web ----------
app = Flask(__name__, static_folder=None)
link = SerialLink(config.load, build_payload, tick)


def redirect_uri() -> str:
    return f"http://localhost:{config.load()['web_port']}/oauth/callback"


@app.get("/")
def index():
    return send_from_directory(ROOT / "pc" / "web", "index.html")


@app.get("/api/state")
def api_state():
    cfg = config.load()
    with lock:
        s = dict(state)
    now = datetime.now().astimezone()
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
    return jsonify({
        "config": cfg,
        "link": link.status,
        "sys": s["sys"],
        "weather": s["weather"], "weather_at": s["weather_at"], "weather_err": s["weather_err"],
        "google": {
            "has_secret": gcal.has_secret(), "connected": gcal.connected(),
            "status": s["cal_status"], "error": s["cal_err"], "account": account,
            "updated_at": s["cal_at"], "redirect_uri": redirect_uri(),
        },
        "events": events,
        "payload": build_payload(),
    })


@app.post("/api/config")
def api_config():
    changes = request.get_json(force=True) or {}
    before = config.load()
    cfg = config.update(changes)
    if cfg["city"] != before["city"]:
        wake_weather.set()
    if cfg["calendars"] != before["calendars"] or cfg["days_ahead"] != before["days_ahead"]:
        wake_calendar.set()
    return jsonify(cfg)


@app.get("/api/ports")
def api_ports():
    return jsonify(ports())


@app.get("/api/geocode")
def api_geocode():
    try:
        return jsonify(weather.geocode(request.args.get("q", "")))
    except Exception as e:
        return jsonify({"error": str(e)}), 502


@app.post("/api/refresh")
def api_refresh():
    wake_weather.set()
    wake_calendar.set()
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


@app.get("/auth/google")
def auth_google():
    if not gcal.has_secret():
        return redirect("/")
    return redirect(gcal.auth_url(redirect_uri()))


@app.get("/oauth/callback")
def oauth_callback():
    if request.args.get("error"):
        return redirect("/?google=" + request.args["error"])
    try:
        gcal.finish_auth(request.url.replace("127.0.0.1", "localhost"))
    except Exception as e:
        log.warning("oauth: %s", e)
        return redirect("/?google=erro")
    wake_calendar.set()
    return redirect("/?google=ok")


@app.post("/api/google/disconnect")
def api_disconnect():
    gcal.disconnect()
    wake_calendar.set()
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
    wake_calendar.set()
    return jsonify({"ok": True, "link": ev.get("htmlLink")})


def main():
    import sys
    port = config.load()["web_port"]
    threading.Thread(target=weather_loop, daemon=True, name="weather").start()
    threading.Thread(target=calendar_loop, daemon=True, name="calendar").start()
    link.start()
    if "--no-browser" not in sys.argv:
        threading.Timer(1.0, lambda: webbrowser.open(f"http://localhost:{port}")).start()
    log.info("painel em http://localhost:%s", port)
    app.run(host="127.0.0.1", port=port, debug=False, use_reloader=False, threaded=True)


if __name__ == "__main__":
    main()
