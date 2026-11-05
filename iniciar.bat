@echo off
rem Sobe o painel sem janela de terminal e abre o navegador em http://localhost:8765
rem Se ele ja estiver rodando (iniciado com o Windows), so abre o navegador.
cd /d "%~dp0pc"
start "" pythonw app.py %*
