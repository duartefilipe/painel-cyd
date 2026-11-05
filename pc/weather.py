"""Previsao pelo Open-Meteo: gratis e sem chave de API."""
from datetime import datetime

import requests

TZ = "America/Sao_Paulo"


def icon_for(code: int) -> str:
    """Codigo WMO -> letra que o firmware sabe desenhar."""
    if code == 0:
        return "S"            # ceu limpo
    if code in (1, 2):
        return "P"            # parcialmente nublado
    if code == 3:
        return "C"            # nublado
    if code in (45, 48):
        return "F"            # neblina
    if code in (71, 73, 75, 77, 85, 86):
        return "N"            # neve
    if code >= 95:
        return "T"            # trovoada
    if code >= 51:
        return "R"            # chuva / garoa / pancadas
    return "C"


DESC = {
    "S": "Ceu limpo", "P": "Parcialmente nublado", "C": "Nublado", "F": "Neblina",
    "R": "Chuva", "T": "Trovoada", "N": "Neve",
}


def fetch(lat: float, lon: float) -> dict:
    r = requests.get(
        "https://api.open-meteo.com/v1/forecast",
        params={
            "latitude": lat,
            "longitude": lon,
            "current": "temperature_2m,weather_code,is_day,relative_humidity_2m",
            "hourly": "temperature_2m,weather_code,precipitation_probability",
            "daily": "temperature_2m_max,temperature_2m_min",
            "timezone": TZ,
            "forecast_days": 2,
        },
        timeout=15,
    )
    r.raise_for_status()
    j = r.json()
    cur, hourly, daily = j["current"], j["hourly"], j["daily"]

    # As 4 proximas horas cheias depois de agora.
    now = datetime.now().replace(minute=0, second=0, microsecond=0)
    hours = []
    for i, ts in enumerate(hourly["time"]):
        t = datetime.fromisoformat(ts)
        if t > now:
            hours.append({
                "lbl": f"{t.hour}h",
                "temp": round(hourly["temperature_2m"][i]),
                "ic": icon_for(hourly["weather_code"][i]),
                "rain": hourly["precipitation_probability"][i],
            })
        if len(hours) == 4:
            break

    ic = icon_for(cur["weather_code"])
    return {
        "tc": round(cur["temperature_2m"]),
        "hum": cur.get("relative_humidity_2m"),
        "ic": ic,
        "desc": DESC.get(ic, ""),
        "day": int(cur.get("is_day", 1)),
        "mn": round(daily["temperature_2m_min"][0]),
        "mx": round(daily["temperature_2m_max"][0]),
        "hours": hours,
    }


def geocode(q: str) -> list:
    r = requests.get(
        "https://geocoding-api.open-meteo.com/v1/search",
        params={"name": q, "count": 8, "language": "pt", "format": "json"},
        timeout=15,
    )
    r.raise_for_status()
    return [
        {
            "name": x["name"],
            "admin": x.get("admin1", ""),
            "country": x.get("country", ""),
            "lat": x["latitude"],
            "lon": x["longitude"],
        }
        for x in r.json().get("results", [])
    ]
