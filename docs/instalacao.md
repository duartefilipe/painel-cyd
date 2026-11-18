# Instalação

## 1. Servidor no Portainer

O Portainer constrói a imagem direto deste repositório (`Dockerfile` +
`docker-compose.yml`).

1. Gere um token longo e aleatório (vale como senha do painel):

   ```powershell
   python -c "import secrets; print(secrets.token_urlsafe(24))"
   ```

2. No Portainer: **Stacks → Add stack → Repository**.
   - **Repository URL:** `https://github.com/duartefilipe/painel-cyd`
   - **Repository reference:** `refs/heads/main` (ou a branch que quiser)
   - **Compose path:** `docker-compose.yml`
   - Repositório privado: ligue **Authentication** e use um token do GitHub *fine-grained*
     com acesso só de leitura (**Contents: Read**) a este repositório.
3. Em **Environment variables**, adicione:

   | Nome | Valor |
   |---|---|
   | `PAINEL_TOKEN` | o token gerado no passo 1 |
   | `PAINEL_TZ` | opcional, padrão `America/Sao_Paulo` |

   > As *Environments* e *Secrets* do GitHub **não** chegam ao Portainer: elas só existem
   > para o GitHub Actions. O token fica na stack.

4. **Deploy the stack.** Para atualizar depois de um `git push`, use **Pull and redeploy**
   na stack, ou ligue **GitOps updates** para o Portainer verificar o repositório sozinho.

Teste: abra `http://<ip-do-servidor>:8765`. O navegador pede usuário e senha: use
qualquer usuário e o `PAINEL_TOKEN` como senha.

Configuração, credencial e token do Google ficam no volume `painel-data` e sobrevivem a
redeploys.

## 2. Firmware na CYD

Precisa do [PlatformIO](https://platformio.org/install/cli) e de um cabo USB **de dados**.

```powershell
cd firmware
pio run -t upload
```

### Configurar Wi-Fi, servidor e token (primeira vez)

1. A placa liga e mostra **Configurar**, com a rede `CYD-Painel` e uma senha de 8
   dígitos. A senha é sorteada a cada boot e só aparece na tela.
2. Conecte o celular nessa rede. Se o portal não abrir sozinho, acesse `192.168.4.1`.
3. **Configure WiFi**: escolha a rede (só **2,4 GHz**), digite a senha e preencha:
   - **Servidor:** `http://<ip-do-servidor>:8765`
   - **PAINEL_TOKEN:** o mesmo token da stack
4. Salve. A placa entra no Wi-Fi e começa a desenhar em ~1 s.

Para reconfigurar: **segure o botão BOOT** e ligue a placa (ou aperte RESET com BOOT
segurado). Isso apaga Wi-Fi e token e abre o portal de novo.

### Hardware

- CYD **ESP32-2432S028** com tela ILI9341 (a pinagem vai por *build flag* no
  `platformio.ini`).
- Alguns lotes vêm com as cores invertidas: marque **inverter** no card **Placa** do painel.
- Variante com controlador ST7789 (tela branca depois de gravar): troque
  `-DILI9341_2_DRIVER=1` por `-DST7789_DRIVER=1` no `platformio.ini`.

## 3. Agente no PC

O agente só manda CPU/RAM/disco/rede. Sem ele, o servidor considera o PC "desconectado"
(e o modo automático mostra a tela de PC desconectado).

```powershell
cd agente
pip install -r requirements.txt
copy agente.exemplo.json agente.json
```

Edite `agente.json` com o endereço do servidor e o `PAINEL_TOKEN`. O arquivo está no
`.gitignore`. Rode `python agente.py` para testar; o log fica em `agente/agente.log`.

### Iniciar junto com o Windows

1. `Win + R` → `shell:startup`.
2. Crie ali um atalho para `agente\iniciar-agente.bat`, que sobe o agente sem janela.

### Linux

`bash agente/iniciar-agente.sh`, ou um serviço de usuário do systemd com
`ExecStart=/usr/bin/python3 /caminho/agente/agente.py`.

## Migrando da versão USB

1. Encerre o programa antigo (`pythonw app.py`) e tire o atalho dele de `shell:startup`.
2. Leve o login do Google: no painel novo, card **Google Agenda**, envie o
   `client_secret.json` e o `token.json` da pasta `data/` do PC.
3. A cidade e as agendas escolhidas não migram sozinhas: escolha de novo no painel.
