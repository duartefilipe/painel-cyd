#!/usr/bin/env bash
# Sobe o agente do painel em segundo plano (Linux). Log em agente/agente.log.
cd "$(dirname "$0")"
nohup python3 agente.py >/dev/null 2>&1 &
echo "agente iniciado (pid $!)"
