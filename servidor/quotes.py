"""Cotacoes em reais pela AwesomeAPI (gratis, sem chave, uma chamada para todos os pares)."""
import requests

# Pares aceitos pelo painel (codigo -> rotulo curto na tela).
KNOWN = {
    "USD-BRL": "USD", "EUR-BRL": "EUR", "GBP-BRL": "GBP", "ARS-BRL": "ARS",
    "BTC-BRL": "BTC", "ETH-BRL": "ETH", "JPY-BRL": "JPY", "CNY-BRL": "CNY",
}


def fetch(pairs: list[str]) -> list[dict]:
    pairs = [p for p in pairs if p in KNOWN][:4]
    if not pairs:
        return []
    r = requests.get(
        "https://economia.awesomeapi.com.br/json/last/" + ",".join(pairs),
        timeout=15,
        headers={"User-Agent": "painel-cyd"},
    )
    r.raise_for_status()
    j = r.json()
    out = []
    for p in pairs:
        q = j.get(p.replace("-", ""))
        if not q:
            continue
        out.append({
            "pair": p,
            "lbl": KNOWN[p],
            "bid": float(q["bid"]),
            "ch": float(q["pctChange"]),
            "at": q.get("create_date", ""),
        })
    return out


def fmt_brl(v: float) -> str:
    """Valores grandes sem centavos (BTC), pequenos com 2 ou 3 casas."""
    dec = 0 if v >= 1000 else (2 if v >= 1 else 3)
    s = f"{v:,.{dec}f}"
    return s.replace(",", "_").replace(".", ",").replace("_", ".")
