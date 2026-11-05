# Painel CYD

Transforma uma **CYD** (ESP32-2432S028, a "Cheap Yellow Display") numa tela de mesa que
mostra, ao vivo:

- **Sistema do PC:** uso de CPU com histórico de 72 s, memória, disco e rede
- **Previsão do tempo:** agora, mínima e máxima do dia e as próximas 4 horas
- **Google Agenda:** os 4 próximos compromissos, com destaque para o que está acontecendo

Junto vem um **painel de controle no navegador** (`http://localhost:8765`) para conectar
a conta Google, escolher as agendas exibidas, criar eventos, trocar a cidade e ajustar
brilho, cores e rotação da tela.

```
 PC (Windows)                                           CYD
┌───────────────────────────────────────┐            ┌──────────────────────────┐
│ pc/app.py                             │            │ firmware/src/main.cpp    │
│  ├ psutil ......... CPU/RAM/disco/rede │  USB       │                          │
│  ├ Open-Meteo ..... previsão           │ ─────────► │  desenha a tela          │
│  ├ Google Calendar  compromissos       │ 1 JSON/s   │  responde "OK"           │
│  └ Flask .......... painel web         │ ◄───────── │                          │
└───────────────────────────────────────┘            └──────────────────────────┘
```

A placa **não usa Wi-Fi**: tudo chega pelo cabo USB que já a alimenta. Por isso não é
preciso configurar rede nem guardar a senha do Google na placa. A contrapartida é que o
painel só funciona com o PC ligado.

## Começo rápido

Precisa de: CYD com cabo USB, Python 3.11+ e [PlatformIO](https://platformio.org/install/cli).

```bash
# 1. gravar o firmware (a CYD aparece como porta COM; o PlatformIO acha sozinho)
cd firmware
pio run -t upload

# 2. instalar e subir o programa do PC
cd ../pc
pip install -r requirements.txt
python app.py
```

O navegador abre no painel. No Windows, depois da primeira vez, basta dar duplo clique em
`iniciar.bat`, que sobe o programa sem janela de terminal.

**No Linux**, o caminho é outro: `bash instalar-linux.sh` cria o ambiente, confere a
permissão da porta serial e liga um serviço do systemd que sobe a cada login. Detalhes e
armadilhas (grupo `dialout`, `brltty` no Ubuntu) em [docs/linux.md](docs/linux.md).

Para os compromissos aparecerem, conecte o Google Agenda:
[docs/google-agenda.md](docs/google-agenda.md).

## O que precisa estar rodando

| Peça | Onde vive | Precisa subir toda vez? |
|---|---|---|
| Firmware | memória flash da placa | **não**: gravou uma vez, a placa liga já com ele |
| Programa do PC (`pc/app.py`) | PC | **sim**: é ele que manda os dados |
| Painel web | servido pelo mesmo programa | não sobe sozinho; vem junto com o programa |

A placa não busca nada sozinha. Ligada sem o programa do PC, ela mostra
**"aguardando o PC..."**. O painel web é só a tela de configuração desse programa: não
precisa ficar aberto no navegador para a placa funcionar.

Por isso, o normal é o programa **iniciar com o sistema**, em segundo plano: atalho na
inicialização no Windows ([como configurar](docs/instalacao.md#iniciar-junto-com-o-windows)),
serviço do systemd no Linux ([docs/linux.md](docs/linux.md)). Para abrir o painel
depois, dê duplo clique em `iniciar.bat`: se o programa já estiver rodando, ele só abre
o navegador.

Brilho, cores e rotação ficam salvos no PC (`data/config.json`) e vão para a placa em
cada mensagem. Até o programa subir, a placa usa o padrão.

## Documentação

| Documento | Conteúdo |
|---|---|
| [docs/instalacao.md](docs/instalacao.md) | Hardware, gravação do firmware, programa do PC, iniciar com o Windows |
| [docs/linux.md](docs/linux.md) | Instalar no Linux, porta serial, serviço do systemd, levar o login do Google |
| [docs/google-agenda.md](docs/google-agenda.md) | Criar a credencial no Google Cloud e conectar a conta |
| [docs/arquitetura.md](docs/arquitetura.md) | Como as peças conversam, o protocolo da serial e como acrescentar um dado novo |
| [docs/problemas.md](docs/problemas.md) | Sintoma → causa → solução |

## Estrutura

```
firmware/            firmware da CYD (PlatformIO + TFT_eSPI + ArduinoJson)
  platformio.ini     pinagem da tela vai por build flag, sem editar a biblioteca
  src/main.cpp
pc/                  programa do PC (Python)
  app.py             servidor web, coletores e montagem da mensagem
  serial_link.py     envio pela USB e detecção da placa
  metrics.py         CPU, memória, disco e rede
  weather.py         Open-Meteo (previsão e busca de cidade)
  gcal.py            Google Agenda (OAuth, leitura e criação de eventos)
  config.py          configuração em data/config.json
  web/index.html     painel de controle
data/                criada na primeira execução; fica FORA do git
                     (credencial do Google, token, configuração e log)
iniciar.bat          sobe o painel sem janela (Windows)
iniciar.sh           o mesmo no Linux
instalar-linux.sh    .venv, checagem da porta serial e serviço do systemd
```

## Privacidade

- O servidor escuta **só em `127.0.0.1`**. Nenhum outro aparelho da rede acessa o painel
  nem a sua agenda.
- A credencial do Google (`client_secret.json`) e o token da sua conta (`token.json`)
  ficam em `data/`, que está no `.gitignore`.
- A previsão usa o [Open-Meteo](https://open-meteo.com/), que não pede cadastro nem chave.
