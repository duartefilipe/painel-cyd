# Painel CYD

Transforma uma **CYD** (ESP32-2432S028, a "Cheap Yellow Display") numa tela de mesa ligada
por **Wi-Fi** a um servidor que fica sempre ligado (Docker no Portainer). A placa não
depende do PC: com o PC desligado ela continua mostrando informação, só troca de tela.

Widgets disponíveis:

- **Sistema do PC:** CPU com histórico de 72 s, memória, disco e rede (vem do agente do PC)
- **Tempo:** agora, mínima e máxima do dia e as próximas 4 horas (Open-Meteo)
- **Cotações:** até 4 pares em reais, com variação do dia (USD, EUR, BTC... via AwesomeAPI)
- **Agenda:** os 4 próximos compromissos do Google Agenda

Você monta **telas** no painel web (o que vai à esquerda, à direita e embaixo) e escolhe
como exibir:

- **Automático:** PC conectado → tela X (ex.: "Trabalho"); PC desconectado → tela Y (ex.: "Anakin")
- **Manual:** sempre a tela escolhida

Uma tela também pode ser um **relógio de mesa**: hora grande, data, tempo, próximo
compromisso e, embaixo, uma animação em pixel art (aquário, céu estrelado, chuva Matrix
ou fogo).

Depois da primeira gravação pelo USB, o firmware pode ser atualizado **pelo Wi-Fi**
([OTA](docs/instalacao.md#atualizar-o-firmware-pelo-wi-fi-ota)).

```
 PC (Windows/Linux)              Servidor (Docker, sempre ligado)              CYD
┌──────────────────┐  POST 1/s  ┌──────────────────────────────────┐  GET 1/s  ┌────────────┐
│ agente/agente.py │ ─────────► │ servidor/app.py                  │ ◄──────── │ firmware   │
│  CPU/RAM/disco   │  metricas  │  Open-Meteo, Google, AwesomeAPI  │  Wi-Fi    │ desenha a  │
└──────────────────┘            │  telas + escolha automatica      │ ────────► │ tela       │
                                │  painel web :8765                │   JSON    └────────────┘
                                └──────────────────────────────────┘
```

## Peças

| Peça | Onde vive | Precisa ficar rodando? |
|---|---|---|
| Servidor (`servidor/`) | container no Portainer | **sim**, sempre (é de onde a placa lê) |
| Agente (`agente/`) | PC | só quando você quer o widget Sistema; sem ele, o PC conta como "desconectado" |
| Firmware (`firmware/`) | flash da placa | gravou uma vez, a placa liga já com ele |

## Começo rápido

1. **Servidor** no Portainer, apontando para este repositório:
   [docs/instalacao.md](docs/instalacao.md#1-servidor-no-portainer).
2. **Firmware:** `cd firmware && pio run -t upload`. Na primeira vez a placa abre a rede
   `CYD-Painel`; conecte pelo celular e informe Wi-Fi, servidor e token
   ([detalhes](docs/instalacao.md#2-firmware-na-cyd)).
3. **Agente** no PC (opcional): [docs/instalacao.md](docs/instalacao.md#3-agente-no-pc).
4. **Google Agenda:** [docs/google-agenda.md](docs/google-agenda.md).

O painel web fica em `http://<ip-do-servidor>:8765` (usuário qualquer, senha =
`PAINEL_TOKEN`).

## Documentação

| Documento | Conteúdo |
|---|---|
| [docs/instalacao.md](docs/instalacao.md) | Portainer, gravação do firmware, configuração do Wi-Fi, agente do PC |
| [docs/google-agenda.md](docs/google-agenda.md) | Credencial no Google Cloud e login a partir do servidor |
| [docs/arquitetura.md](docs/arquitetura.md) | Como as peças conversam, API, formato da tela, como criar um widget |
| [docs/problemas.md](docs/problemas.md) | Sintoma → causa → solução |

## Estrutura

```
servidor/            roda no Docker (Python + Flask/waitress)
  app.py             coletores, telas, API e autenticacao
  weather.py         Open-Meteo
  quotes.py          cotacoes (AwesomeAPI)
  gcal.py            Google Agenda (OAuth, leitura e criacao de eventos)
  config.py          configuracao em /data/config.json
  web/index.html     painel web (editor de telas, previa, configuracoes)
agente/              roda no PC: manda CPU/RAM/disco/rede para o servidor
firmware/            CYD (PlatformIO + TFT_eSPI + ArduinoJson + WiFiManager)
Dockerfile, docker-compose.yml   stack do Portainer
```

## Segurança

- Tudo que a placa e o agente fazem exige o `PAINEL_TOKEN` (`Authorization: Bearer`); o
  painel web pede o mesmo token como senha. Defina o token nas variáveis da stack no
  Portainer, **nunca** no git.
- O painel tem acesso à sua agenda: deixe a porta 8765 só na rede local, sem
  redirecionamento no roteador.
- A credencial e o token do Google ficam no volume `painel-data` do container.
- O HTTP na rede local não é cifrado. Quem estiver na mesma rede pode ver o token, o que
  é aceitável numa rede de casa. Se for usar fora dela, coloque um proxy com HTTPS na frente.
