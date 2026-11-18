"""Google Agenda por OAuth. A credencial do app (client_secret.json) e o token
do usuario ficam no diretorio de dados (volume do container), fora do git.

O servidor roda em outra maquina da rede, e o Google so aceita redirecionar um
cliente "App para computador" para localhost. Por isso o login termina assim:
o navegador cai em http://localhost:.../oauth/callback?code=... (pagina que nao
abre) e o usuario cola esse endereco no painel. Alternativa: enviar o token.json
gerado no PC.
"""
import json
import os
from datetime import date, datetime, timedelta

from google.auth.exceptions import RefreshError
from google.auth.transport.requests import Request
from google.oauth2.credentials import Credentials
from google_auth_oauthlib.flow import Flow
from googleapiclient.discovery import build

from config import DATA, TZ, TZ_NAME

# O redirect e http://localhost; o oauthlib so aceita http com esta flag.
os.environ.setdefault("OAUTHLIB_INSECURE_TRANSPORT", "1")

SCOPES = [
    "https://www.googleapis.com/auth/calendar.readonly",   # listar agendas
    "https://www.googleapis.com/auth/calendar.events",     # ler e criar eventos
]
SECRET = DATA / "client_secret.json"
TOKEN = DATA / "token.json"
REDIRECT_URI = "http://localhost:8765/oauth/callback"


class NeedsReconnect(Exception):
    pass


class GCal:
    def __init__(self):
        self._flow = None
        self.account = None

    # ---------- estado ----------
    def has_secret(self) -> bool:
        return SECRET.exists()

    def save_secret(self, raw: bytes):
        data = json.loads(raw)
        if "installed" not in data and "web" not in data:
            raise ValueError("Arquivo nao parece um client_secret do Google (falta 'installed').")
        SECRET.write_bytes(raw)

    def save_token(self, raw: bytes):
        data = json.loads(raw)
        if "refresh_token" not in data:
            raise ValueError("Arquivo nao parece um token.json do painel (falta 'refresh_token').")
        TOKEN.write_bytes(raw)
        self.account = None

    def connected(self) -> bool:
        return TOKEN.exists()

    def _creds(self) -> Credentials:
        if not TOKEN.exists():
            raise NeedsReconnect("Google nao conectado")
        creds = Credentials.from_authorized_user_file(str(TOKEN), SCOPES)
        if not creds.valid:
            if creds.expired and creds.refresh_token:
                try:
                    creds.refresh(Request())
                except RefreshError as e:
                    # Token revogado, ou app em modo "Teste" no Google Cloud
                    # (nesse modo o refresh token vence em 7 dias).
                    raise NeedsReconnect(f"Reconecte o Google ({e.args[0] if e.args else 'token invalido'})")
                TOKEN.write_text(creds.to_json(), encoding="utf-8")
            else:
                raise NeedsReconnect("Reconecte o Google")
        return creds

    def _svc(self):
        return build("calendar", "v3", credentials=self._creds(), cache_discovery=False)

    # ---------- login ----------
    def auth_url(self) -> str:
        self._flow = Flow.from_client_secrets_file(str(SECRET), scopes=SCOPES, redirect_uri=REDIRECT_URI)
        url, _ = self._flow.authorization_url(access_type="offline", prompt="consent", include_granted_scopes="true")
        return url

    def finish_auth(self, pasted_url: str):
        if not self._flow:
            raise RuntimeError("Login expirou; clique em Conectar de novo.")
        pasted_url = pasted_url.strip().replace("127.0.0.1", "localhost")
        if "code=" not in pasted_url:
            raise ValueError("O endereco colado nao tem 'code='. Copie a barra de endereco inteira.")
        self._flow.fetch_token(authorization_response=pasted_url)
        TOKEN.write_text(self._flow.credentials.to_json(), encoding="utf-8")
        self._flow = None
        self.account = None

    def disconnect(self):
        if TOKEN.exists():
            TOKEN.unlink()
        self.account = None

    # ---------- leitura ----------
    def account_email(self) -> str:
        if not self.account:
            self.account = self._svc().calendars().get(calendarId="primary").execute()["id"]
        return self.account

    def calendars(self) -> list:
        items = self._svc().calendarList().list().execute().get("items", [])
        return [
            {
                "id": c["id"],
                "name": c.get("summaryOverride") or c.get("summary", c["id"]),
                "primary": bool(c.get("primary")),
                "color": c.get("backgroundColor", "#888"),
                "writable": c.get("accessRole") in ("owner", "writer"),
            }
            for c in items
        ]

    def upcoming(self, calendar_ids: list, days: int) -> list:
        svc = self._svc()
        now = datetime.now(TZ)
        tmin = now.replace(hour=0, minute=0, second=0, microsecond=0)
        tmax = tmin + timedelta(days=days + 1)
        out = []
        for cid in calendar_ids:
            res = svc.events().list(
                calendarId=cid, timeMin=tmin.isoformat(), timeMax=tmax.isoformat(),
                singleEvents=True, orderBy="startTime", maxResults=25,
            ).execute()
            for e in res.get("items", []):
                if e.get("status") == "cancelled":
                    continue
                me = next((a for a in e.get("attendees", []) if a.get("self")), None)
                if me and me.get("responseStatus") == "declined":
                    continue
                ev = _normalize(e, cid)
                if ev["end"] > now:  # ja terminou: fora
                    out.append(ev)
        out.sort(key=lambda x: (x["start"], not x["all_day"]))
        return out

    # ---------- escrita ----------
    def create(self, calendar_id: str, title: str, day: str, start: str | None, minutes: int) -> dict:
        body = {"summary": title}
        if start:
            s = datetime.fromisoformat(f"{day}T{start}")
            e = s + timedelta(minutes=minutes)
            body["start"] = {"dateTime": s.isoformat(), "timeZone": TZ_NAME}
            body["end"] = {"dateTime": e.isoformat(), "timeZone": TZ_NAME}
        else:
            d = date.fromisoformat(day)
            body["start"] = {"date": d.isoformat()}
            body["end"] = {"date": (d + timedelta(days=1)).isoformat()}
        return self._svc().events().insert(calendarId=calendar_id, body=body).execute()


def _normalize(e: dict, cid: str) -> dict:
    s, en = e["start"], e["end"]
    if "dateTime" in s:
        start = datetime.fromisoformat(s["dateTime"]).astimezone(TZ)
        end = datetime.fromisoformat(en["dateTime"]).astimezone(TZ)
        all_day = False
    else:
        start = datetime.fromisoformat(s["date"]).replace(tzinfo=TZ)
        end = datetime.fromisoformat(en["date"]).replace(tzinfo=TZ)
        all_day = True
    return {
        "id": e["id"],
        "cal": cid,
        "title": e.get("summary", "(sem titulo)"),
        "start": start,
        "end": end,
        "all_day": all_day,
        "link": e.get("htmlLink"),
    }
