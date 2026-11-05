#!/usr/bin/env bash
# Abre o painel. Se o programa ja estiver rodando (servico do systemd), so abre o
# navegador; senao, sobe o programa em segundo plano. Equivalente do iniciar.bat.
DIR="$(cd "$(dirname "$0")" && pwd)"
PY="$DIR/.venv/bin/python"
[[ -x "$PY" ]] || { echo "Rode antes: bash instalar-linux.sh"; exit 1; }
cd "$DIR/pc" || exit 1
mkdir -p "$DIR/data"
nohup "$PY" app.py "$@" >>"$DIR/data/painel.out" 2>&1 &
