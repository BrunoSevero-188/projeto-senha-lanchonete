@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo ============================================
echo   PAINEL DE SENHAS - Lanchonete
echo ============================================
where python >nul 2>nul
if errorlevel 1 (
  echo ERRO: Python nao foi encontrado no PATH.
  echo Instale o Python 3 e marque "Add Python to PATH".
  pause
  exit /b 1
)
python --version
python servidor_web.py
pause
