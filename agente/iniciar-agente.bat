@echo off
rem Sobe o agente do painel sem janela de terminal. Para iniciar com o Windows,
rem coloque um atalho deste arquivo em shell:startup.
cd /d "%~dp0"
start "" pythonw agente.py
