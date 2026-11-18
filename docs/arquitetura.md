# Arquitetura

## Divisão de trabalho

**O servidor busca, a placa desenha.** A CYD não tem lógica de negócio: a cada segundo
ela pede ao servidor a tela pronta e redesenha.

1. **O servidor fica sempre ligado.** Com o PC desligado, tempo, cotações e agenda
   continuam chegando.
2. **O login do Google fica no servidor.** OAuth numa placa sem navegador é trabalhoso, e
   guardar o token num ESP32 é menos seguro.
3. **Mudar a tela raramente exige regravar.** Telas, rótulos ("amanha 9:00"), corte de
   texto e formatação de números são decididos no Python.
4. **A placa só fala HTTP na rede local.** Quem faz as chamadas HTTPS externas é o
   servidor. Assim a placa não gasta memória com TLS (ESP32 sem PSRAM).

## Servidor (`servidor/app.py`)

| Thread | Período | Faz |
|---|---|---|
| `weather` | 10 min | previsão no Open-Meteo |
| `calendar` | 2 min | próximos eventos no Google |
| `quotes` | 5 min | cotações na AwesomeAPI |
| waitress | sob demanda | API e painel web em `0.0.0.0:8765` |

Mudar cidade, agendas ou pares no painel acorda o coletor na hora (`threading.Event`).

A tela é montada **no momento do pedido** (`build_payload`), então rótulos que dependem
do relógio ("agora", "amanha") viram na hora certa mesmo sem nova busca.

### Telas e escolha automática

Cada tela define o widget de cada região:

| Região | Tamanho | Widgets |
|---|---|---|
| `left`, `right` | 160×124 | `sys` (Sistema), `w` (Tempo), `fx` (Cotações), `none` |
| `bottom` | 320×88 | `ev` (Agenda), `none` |

`display.mode`:

- `auto`: o PC está **conectado** se o agente mandou métricas nos últimos `pc_timeout`
  segundos (padrão 10). Conectado → `display.pc_on`; desconectado → `display.pc_off`.
- `manual`: sempre `display.manual`.

Uma tela com `sys` e o PC desconectado mostra "PC desconectado" nessa região.

### Autenticação

Toda rota, menos `/healthz`, exige o `PAINEL_TOKEN`:

- placa e agente: `Authorization: Bearer <token>`;
- navegador: HTTP Basic, com usuário qualquer e senha = token.

A comparação usa `hmac.compare_digest`. Sem `PAINEL_TOKEN` (ou com menos de 12
caracteres), o servidor nem sobe.

### API

| Rota | Método | Quem usa | Uso |
|---|---|---|---|
| `/api/screen` | GET | placa | tela ativa já montada (formato abaixo) |
| `/api/metrics` | POST | agente | `{cpu, ram, ru, rt, dsk, dn, up, host}` |
| `/api/state` | GET | painel | tudo, inclusive a prévia de cada tela (`payloads`) |
| `/api/config` | POST | painel | altera configuração (parcial): `screens`, `display`, `quotes`, `city`... |
| `/api/geocode?q=` | GET | painel | busca de cidade |
| `/api/refresh` | POST | painel | força todos os coletores |
| `/api/google/secret` | POST | painel | envia o `client_secret.json` (multipart) |
| `/api/google/token` | POST | painel | envia um `token.json` pronto (multipart) |
| `/auth/google` | GET | painel | começa o login (redireciona ao Google) |
| `/api/google/finish` | POST | painel | `{url}` colado da barra de endereço depois do login |
| `/api/google/disconnect` | POST | painel | apaga o token |
| `/api/calendars` | GET | painel | agendas da conta |
| `/api/events` | POST | painel | cria evento `{title, date, time?, minutes, calendar}` |

## Formato da tela (`GET /api/screen`)

```json
{
  "t": "14:32", "d": "qui, 05 nov", "pc": 1,
  "cfg": {"br": 200, "inv": 0, "rot": 1},
  "lay": {"n": "Trabalho", "l": "sys", "r": "w", "b": "ev"},
  "sys": {"cpu": 23.5, "ram": 61.2, "ru": 10.1, "rt": 31.9, "dsk": 72, "dn": 123.4, "up": 12.3},
  "w":   {"ok": 1, "city": "Santa Maria", "tc": 24, "ic": "P", "day": 1, "mn": 17, "mx": 29,
          "h": [["15h", 25, "P"], ["16h", 25, "C"], ["17h", 24, "R"], ["18h", 22, "R"]]},
  "fx":  {"ok": 1, "at": "14:30", "l": [["USD", "5,83", 0.95], ["BTC", "366.621", -0.94]]},
  "ev":  {"st": "ok", "msg": "", "l": [["agora", "Reuniao de equipe", 1], ["16:00", "Dentista", 0]]}
}
```

Só vêm os blocos que a tela ativa usa. `sys` só vem com o PC conectado.

| Campo | Significado |
|---|---|
| `t`, `d` | hora e data já formatadas |
| `pc` | 1 se o agente está enviando (o ponto do cabeçalho fica verde; azul se não) |
| `cfg` | brilho (0-255), inverter cores (0/1), rotação (1 ou 3) |
| `lay` | nome da tela e widget de cada região |
| `w.ic` | `S` sol, `P` parcialmente nublado, `C` nublado, `F` neblina, `R` chuva, `T` trovoada, `N` neve |
| `fx.l` | até 4 `[rótulo, valor em R$ já formatado, variação % do dia]` |
| `ev.st` | `ok`, `off` (Google não conectado) ou `err` (detalhe em `ev.msg`) |

**Cada resposta é completa.** A placa pode reiniciar a qualquer momento e volta a
desenhar tudo no pedido seguinte.

## Firmware

- **Wi-Fi:** WiFiManager. Sem configuração (ou com BOOT segurado ao ligar), a placa abre
  o AP `CYD-Painel` com senha sorteada e mostrada na tela. Servidor e token ficam na NVS
  (`Preferences`, namespace `painel`).
- **Desenho por região com sprite:** cada região é desenhada num sprite em memória e
  enviada de uma vez, para não piscar. A maior usa 56 KB.
- **Dados velhos:** 5 s sem resposta, o cabeçalho fica vermelho com o motivo ("sem
  Wi-Fi", "servidor fora do ar", "token recusado") e os últimos dados continuam na tela.
- **Texto sem acento:** as fontes embutidas da TFT_eSPI só têm ASCII. O servidor
  translitera antes de enviar.

## Acrescentar um widget

Exemplo: um widget `net` com o status de serviços da rede.

1. **Coletar** no servidor: um módulo novo (como `quotes.py`), um campo em `state` e um
   `_loop(...)` em `main()`.
2. **Registrar:** pôr `net` em `SLOT_HALF` (`app.py`) e montar o bloco em `build_payload`
   quando `"net" in slots`.
3. **Painel:** pôr em `WIDGETS_HALF` e um ramo em `widget()` no `web/index.html` (prévia).
4. **Firmware:** campos em `struct State`, leitura em `handlePayload` e uma função
   `wNet()` chamada em `drawHalf`, desenhando em 160×124.

Firmware antigo ignora widget que não conhece (a região fica vazia), então o servidor pode
subir antes de regravar a placa.
