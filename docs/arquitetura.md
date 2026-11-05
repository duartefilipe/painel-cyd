# Arquitetura

## Divisão de trabalho

**O PC busca, a placa desenha.** A CYD não tem lógica de negócio: recebe uma mensagem
pronta por segundo e redesenha a tela. Três motivos:

1. **CPU e memória só existem no PC.** Alguém lá precisa ler esses números de qualquer jeito.
2. **O login do Google fica no PC.** OAuth numa placa sem navegador é trabalhoso, e
   guardar o token num ESP32 é menos seguro.
3. **Mudar a tela raramente exige regravar.** Formato de data, rótulos ("amanha 9:00"),
   corte de texto e quais eventos entram são decididos no Python.

## Programa do PC

`pc/app.py` roda quatro linhas de execução:

| Thread | Período | Faz |
|---|---|---|
| `serial` (`SerialLink`) | 1 s | lê as métricas, monta a mensagem, envia, lê a resposta |
| `weather` | 10 min | busca a previsão no Open-Meteo |
| `calendar` | 2 min | busca os próximos eventos no Google |
| Flask | sob demanda | painel web e API em `127.0.0.1:8765` |

Mudar a cidade ou as agendas pelo painel acorda o coletor na hora (`threading.Event`),
sem esperar o próximo ciclo.

A mensagem é montada **no momento do envio** (`build_payload`), não quando o dado chega.
Assim, rótulos que dependem do relógio ("agora", "amanha") mudam na virada do minuto
mesmo sem nova busca na agenda.

### API do painel

| Rota | Método | Uso |
|---|---|---|
| `/api/state` | GET | tudo que o painel mostra, inclusive a mensagem atual enviada à placa |
| `/api/config` | POST | altera configuração (parcial) |
| `/api/ports` | GET | portas seriais disponíveis |
| `/api/geocode?q=` | GET | busca de cidade |
| `/api/refresh` | POST | força previsão e agenda |
| `/api/google/secret` | POST | envia o `client_secret.json` (multipart) |
| `/auth/google` → `/oauth/callback` | GET | login OAuth |
| `/api/google/disconnect` | POST | apaga o token |
| `/api/calendars` | GET | agendas da conta |
| `/api/events` | POST | cria evento `{title, date, time?, minutes, calendar}` |

## Protocolo da serial

115200 baud, **uma linha de JSON por mensagem**, terminada em `\n`. Chaves curtas para
caber folgado no buffer de recepção da placa (4 KB; uma mensagem típica tem ~500 bytes).

```json
{
  "t": "14:32",
  "d": "qui, 05 nov",
  "cfg": {"br": 200, "inv": 0, "rot": 1},
  "sys": {"cpu": 23.5, "ram": 61.2, "ru": 10.1, "rt": 31.9, "dsk": 72, "dn": 123.4, "up": 12.3},
  "w":   {"ok": 1, "city": "Santa Maria", "tc": 24, "ic": "P", "day": 1, "mn": 17, "mx": 29,
          "h": [["15h", 25, "P"], ["16h", 25, "C"], ["17h", 24, "R"], ["18h", 22, "R"]]},
  "ev":  {"st": "ok", "msg": "", "l": [["agora", "Reuniao de equipe", 1], ["16:00", "Dentista", 0]]}
}
```

| Campo | Significado |
|---|---|
| `t`, `d` | hora e data já formatadas |
| `cfg` | brilho (0-255), inverter cores (0/1), rotação (1 ou 3) |
| `sys` | CPU %, RAM %, RAM usada/total (GB), disco %, rede ↓/↑ (KB/s) |
| `w.ic` | ícone: `S` sol, `P` parcialmente nublado, `C` nublado, `F` neblina, `R` chuva, `T` trovoada, `N` neve |
| `w.day` | 0 à noite (o firmware desenha lua no lugar do sol) |
| `ev.st` | `ok`, `off` (Google não conectado) ou `err` (detalhe em `ev.msg`) |
| `ev.l` | até 4 itens `[quando, título, acontecendo_agora]` |

A placa responde `OK` a cada mensagem válida e `ERR <motivo>` se o JSON não fechar. O
painel usa o `OK` para dizer se a placa está viva. Porta aberta não basta: ela abre mesmo
com outro firmware na placa.

**Cada mensagem é completa.** Não existe "diferença desde a última": a placa pode
reiniciar a qualquer momento e volta a desenhar tudo no segundo seguinte.

### Decisões que não são óbvias

- **Texto sem acento.** As fontes embutidas da TFT_eSPI só têm ASCII. O PC translitera
  (`unicodedata` NFKD) antes de enviar, então "Reunião" chega como "Reuniao". O símbolo de
  grau é desenhado como um círculo pela placa.
- **DTR e RTS baixos ao abrir a porta.** A CYD tem circuito de auto-reset ligado a esses
  sinais. Abrir com a combinação errada reinicia o ESP32 a cada conexão. Mesmo assim, a
  primeira mensagem depois de abrir costuma se perder.
- **Desenho por região com sprite.** Redesenhar direto na tela pisca. Cada região
  (cabeçalho, sistema, tempo, agenda) é desenhada num sprite em memória e enviada de uma
  vez. A maior região usa 64 KB, e um sprite de tela cheia não caberia com folga num ESP32
  sem PSRAM.
- **Indicador de dados velhos.** Se a placa passa 5 s sem mensagem, o cabeçalho fica
  vermelho ("sem dados do PC") e o relógio fica cinza. Os últimos dados continuam na tela.

## Layout da tela (320×240)

```
y=0    ┌───────────────────────────────────────────────┐
       │ ● qui, 05 nov                          14:32 │  cabeçalho (28 px)
y=28   ├───────────────────────┬───────────────────────┤
       │ SISTEMA       CPU 23% │ SANTA MARIA           │
       │ ▁▂▃▅▃▂▁ (72 s de CPU) │ ☀  24°C               │  meio (124 px)
       │ RAM   ████░░  61%     │    17 / 29            │
       │ DISCO ██████  72%     │ 15h  16h  17h  18h    │
       │ ▼ 120 KB/s ▲ 12 KB/s  │  ☀    ☁    🌧   🌧     │
y=152  ├───────────────────────┴───────────────────────┤
       │ AGENDA                                        │
       │ agora      Reuniao de equipe                  │  agenda (88 px)
       │ 16:00      Dentista                           │
y=240  └───────────────────────────────────────────────┘
```

As barras seguem semáforo: verde abaixo de 60%, amarelo até 85%, vermelho acima.

## Acrescentar um dado novo

Exemplo: temperatura da GPU.

1. **Coletar** em `pc/metrics.py`, devolvendo uma chave nova (`"gpu": 54`).
2. **Enviar:** nada a fazer se a chave estiver dentro de `sys`, porque o dicionário vai
   inteiro.
3. **Ler** em `handleLine` (`firmware/src/main.cpp`): campo novo em `struct State` e
   `S.gpu = sys["gpu"] | 0;`.
4. **Desenhar** na função da região (`drawSystem`), respeitando a altura de 124 px.
5. **Prévia:** atualizar `renderScreen` em `pc/web/index.html`, que espelha a tela no
   navegador.

Firmware antigo ignora chave que não conhece, então o passo 1 pode subir antes de regravar.
