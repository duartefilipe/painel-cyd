# Problemas comuns

O cabeçalho da placa fica vermelho e mostra o motivo quando passa 5 s sem dados novos.

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| Placa: **"sem Wi-Fi"** | rede fora do ar, senha trocada ou rede só 5 GHz | a placa reconecta sozinha; se a senha mudou, segure **BOOT** ao ligar e reconfigure |
| Placa: **"servidor fora do ar"** | container parado, IP do servidor mudou, ou porta 8765 bloqueada | veja a stack no Portainer; fixe o IP do servidor no DHCP; reconfigure o endereço (BOOT ao ligar) |
| Placa: **"token recusado"** | `PAINEL_TOKEN` da placa diferente do da stack | reconfigure a placa (BOOT ao ligar) |
| Placa: **"endereco invalido"** | servidor digitado sem `http://` | informe `http://IP:8765` no portal |
| Placa fica em **"Configurar"** | sem Wi-Fi salvo, ou a conexão falhou | conecte na rede `CYD-Painel` com a senha da tela; depois de 5 min sem configurar ela reinicia e tenta de novo |
| Tela **Sistema** diz **"PC desconectado"** com o PC ligado | agente parado, token errado no `agente.json`, ou firewall | veja `agente/agente.log`; o card **Placa** do painel mostra quando o PC enviou por último |
| Modo automático não troca de tela | o agente ainda está enviando, ou o tempo-limite ainda não passou | a troca acontece `pc_timeout` segundos (padrão 10) depois do último envio |
| Navegador pede senha em loop | senha diferente do `PAINEL_TOKEN` | usuário qualquer, senha = token da stack |
| Stack não sobe: **"defina PAINEL_TOKEN"** | variável não criada na stack | Portainer → stack → **Environment variables** |
| Redeploy: **"pull access denied for painel-cyd"** | compose antigo com `image:`; o Portainer tentou baixar a imagem do Docker Hub | atualize a stack para a versão atual do `docker-compose.yml` (`pull_policy: build`); enquanto isso, desligue **Re-pull image** no redeploy |
| Stack não sobe: erro ao clonar | repositório privado sem autenticação | ligue **Authentication** na stack com um token do GitHub de leitura |
| Relógio sem animação (metade de baixo preta) | faltou memória para o sprite da animação | veja `sem memoria para a animacao` no monitor serial; escolha **Sem animacao** ou reinicie a placa |
| `pio run -e cyd-ota` falha com **"Authentication Failed"** | `PAINEL_TOKEN` do ambiente diferente do gravado na placa | use o mesmo token configurado no portal da placa |
| `pio run -e cyd-ota` falha com **"No response from device"** | placa ainda com firmware sem OTA, IP errado, ou firewall do Windows bloqueando a resposta | primeira gravação pelo USB; confira o IP no card Placa; libere o Python no firewall |
| Toque **não responde** | firmware sem toque, ou servidor antigo sem `/api/touch` | grave o firmware atual; faça **Pull and redeploy** da stack; o monitor serial mostra `toque: next -> 200` |
| Brilho automático **não muda** | sensor sem calibração (escuro e claro com o mesmo valor) | no card **Brilho automático**, use "Usar leitura atual" com o sensor coberto e depois iluminado |
| Tela **apagou** sozinha | regra de horário com **apagar** | um toque acende por `touch_minutes`; ajuste no card **Horários** |
| Aviso **não aparece** | `text` vazio, token errado (401) ou servidor antigo | confira a resposta do `curl`; o card **Avisos na tela** lista a fila |
| Cores em **negativo** (fundo claro) | lote da CYD com painel invertido | marque **inverter** no card Placa |
| Tela **toda branca** depois de gravar | variante da CYD com controlador ST7789 | troque o driver no `platformio.ini` ([instalacao.md](instalacao.md#hardware)) |
| Google: **"Erro 403: access_denied"** no login | app em "Testando" e o seu e-mail não está em Usuários de teste | adicione o e-mail ([google-agenda.md](google-agenda.md#2-tela-de-consentimento)) |
| Google: depois do login, **página não abre** em `localhost` | esperado: o servidor está em outra máquina | copie o endereço da barra e cole no painel ([google-agenda.md](google-agenda.md#4-conectar-pelo-painel)) |
| Agenda: **"Reconecte o Google"** | token venceu (app em "Testando" vence em 7 dias) ou foi revogado | refaça o login pelo painel |
| Cotações: **"sem cotacoes"** | AwesomeAPI fora, ou sem internet no servidor | tenta de novo em 5 min; o erro aparece no card Cotações |
| Título aparece **sem acento** | proposital: a fonte da placa só tem ASCII | — |

## Ver o que a placa recebe

A placa só faz `GET /api/screen`. O mesmo pedido, de qualquer máquina da rede:

```powershell
curl.exe -H "Authorization: Bearer SEU_TOKEN" http://IP-DO-SERVIDOR:8765/api/screen
```

O monitor serial (`pio device monitor`) mostra o IP da placa e erros de JSON. Os logs do
servidor ficam no Portainer (**Containers → painel-cyd → Logs**) e em `/data/painel.log`.
