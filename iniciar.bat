@echo off
rem Sobe o painel sem janela de terminal e abre o navegador em http://localhost:8765
cd /d "%~dp0pc"
start "" pythonw app.py
