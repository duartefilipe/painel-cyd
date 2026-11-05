# Problemas comuns

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| Tela **"aguardando o PC..."** e não sai disso | o programa do PC não está rodando, ou está numa porta errada | rode `iniciar.bat`; no painel, card **Placa**, confira a porta |
| Tela **"sem dados do PC"** (já mostrou dados antes) | o programa do PC caiu, ou a porta foi liberada | veja `data/painel.log`; clique em **Retomar** se estiver pausado |
| Painel diz **"porta aberta, mas a placa não respondeu"** | a placa está com outro firmware | grave o firmware (`pio run -t upload`) |
| Painel diz **"Nenhuma CYD encontrada no USB"** | cabo só de carga, driver ausente, ou conversor não reconhecido | troque o cabo; instale o driver CH340; escolha a porta manualmente |
| `pio run -t upload` falha com **"could not open port"** / **"Access denied"** | o programa do PC está segurando a porta | **Liberar porta** no painel, gravar, **Retomar** |
| Cores em **negativo** (fundo claro) | lote da CYD com painel invertido | marque **inverter** no card Placa |
| Tela **toda branca** depois de gravar | variante da CYD com controlador ST7789 | troque o driver no `platformio.ini` (ver [instalacao.md](instalacao.md#hardware)) |
| Google: **"Erro 403: access_denied"** no login | app em "Testando" e o seu e-mail não está em Usuários de teste | adicione o e-mail ([google-agenda.md](google-agenda.md#2-tela-de-consentimento)) |
| Agenda: **"Reconecte o Google"** | token venceu (app em modo "Testando" vence em 7 dias) ou foi revogado | **Reconectar** no painel |
| Agenda vazia, mas há eventos | a agenda do evento não está marcada, ou o evento passa de 7 dias | marque a agenda no card Google Agenda |
| Título aparece **sem acento** | proposital: a fonte da placa só tem ASCII | — |
| Previsão: **"sem previsao"** | sem internet, ou Open-Meteo fora | o painel mostra o erro no card Tempo; tenta de novo em 10 min |
| `http://localhost:8765` não abre | programa parado, ou porta 8765 ocupada | rode `python pc/app.py` num terminal para ver o erro; a porta muda em `data/config.json` (`web_port`) |

## Ver o que a placa recebe

Com o programa do PC **parado** (ou a porta liberada):

```bash
cd firmware
pio device monitor
```

Ao reiniciar, a placa imprime `CYD-PAINEL READY`. Para testar a tela sem o programa do PC,
cole uma linha do [protocolo](arquitetura.md#protocolo-da-serial) no monitor e tecle Enter.

A mensagem que está sendo enviada agora também aparece em `GET /api/state`, no campo
`payload`.
