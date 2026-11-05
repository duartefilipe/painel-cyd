#!/usr/bin/env bash
# Instala o Painel CYD no Linux e (opcional) liga o servico que sobe no login.
#   bash instalar-linux.sh              instala e liga o servico
#   bash instalar-linux.sh --sem-servico   so instala (rodar com ./iniciar.sh)
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
SERVICO=1
[[ "${1:-}" == "--sem-servico" ]] && SERVICO=0

ok()   { printf '  \033[32mok\033[0m  %s\n' "$*"; }
aviso(){ printf '  \033[33m!!\033[0m  %s\n' "$*"; }
erro() { printf '  \033[31mERRO\033[0m %s\n' "$*"; exit 1; }

echo "Painel CYD — instalacao em $DIR"

# 1. Python + ambiente virtual. Debian/Ubuntu bloqueiam pip no Python do sistema
#    (PEP 668), por isso tudo vai para .venv/.
command -v python3 >/dev/null || erro "python3 nao encontrado. Instale: sudo apt install python3 python3-venv"
if [[ ! -x "$DIR/.venv/bin/python" ]]; then
  python3 -m venv "$DIR/.venv" 2>/dev/null || erro "falha ao criar o .venv. No Debian/Ubuntu: sudo apt install python3-venv"
fi
"$DIR/.venv/bin/pip" install -q --upgrade pip
"$DIR/.venv/bin/pip" install -q -r "$DIR/pc/requirements.txt"
ok "dependencias instaladas em .venv/"

# 2. Permissao na porta serial. O grupo dono do /dev/ttyUSB* e dialout na maioria
#    das distros e uucp no Arch; pergunta ao proprio dispositivo quando ele existe.
GRUPO=dialout
for dev in /dev/ttyUSB* /dev/ttyACM*; do
  [[ -e "$dev" ]] && GRUPO="$(stat -c %G "$dev")" && break
done
getent group "$GRUPO" >/dev/null || GRUPO=uucp
if id -nG "$USER" | tr ' ' '\n' | grep -qx "$GRUPO"; then
  ok "usuario $USER esta no grupo $GRUPO (acesso a porta serial)"
else
  aviso "usuario $USER NAO esta no grupo $GRUPO: o programa nao vai conseguir abrir a porta."
  echo "        Rode:  sudo usermod -aG $GRUPO $USER   e depois saia e entre de novo na sessao."
fi

# 3. brltty (leitor de braille) sequestra o CH340 (1a86:7523) no Ubuntu 22.04+:
#    a placa conecta e a /dev/ttyUSB0 some logo em seguida.
if command -v dpkg >/dev/null && dpkg -s brltty >/dev/null 2>&1; then
  aviso "o pacote brltty esta instalado e costuma sequestrar o chip USB da CYD."
  echo "        Se a placa nao aparecer em /dev/ttyUSB*, rode:  sudo apt remove brltty"
fi

# 4. Placa conectada?
if ls /dev/ttyUSB* /dev/ttyACM* >/dev/null 2>&1; then
  ok "porta(s) serial encontrada(s): $(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | tr '\n' ' ')"
else
  aviso "nenhuma /dev/ttyUSB* agora. Tudo bem se a placa estiver desligada; o programa procura sozinho."
fi

# 5. Credencial do Google: data/ nao vem do git.
if [[ -f "$DIR/data/token.json" ]]; then
  ok "Google: token encontrado em data/"
else
  aviso "Google: sem data/token.json. Copie a pasta data/ do Windows ou conecte de novo pelo painel."
fi

# 6. Servico de usuario do systemd: sobe no login, reinicia se cair.
if [[ $SERVICO -eq 1 ]]; then
  command -v systemctl >/dev/null || erro "systemctl nao encontrado; use --sem-servico e ./iniciar.sh"
  UNIT_DIR="$HOME/.config/systemd/user"
  mkdir -p "$UNIT_DIR"
  cat > "$UNIT_DIR/painel-cyd.service" <<EOF
[Unit]
Description=Painel CYD (manda os dados para a placa)
After=network-online.target

[Service]
WorkingDirectory=$DIR/pc
ExecStart=$DIR/.venv/bin/python app.py --no-browser
Restart=on-failure
RestartSec=5

[Install]
WantedBy=default.target
EOF
  systemctl --user daemon-reload
  systemctl --user enable --now painel-cyd.service
  sleep 2
  if systemctl --user is-active --quiet painel-cyd.service; then
    ok "servico painel-cyd ativo; sobe sozinho a cada login"
  else
    aviso "o servico nao ficou ativo. Veja:  journalctl --user -u painel-cyd -n 50"
  fi
fi

chmod +x "$DIR/iniciar.sh" 2>/dev/null || true
echo
echo "Painel: http://localhost:8765   (ou ./iniciar.sh, que abre o navegador)"
