@echo off
chcp 65001 >nul
cd /d "%~dp0"

echo.
echo ============================================================
echo  PAINEL DE SENHAS - Preparando arquivos web...
echo ============================================================

:: Renomeia arquivos se necessário
if exist index.txt if not exist index.html ren index.txt index.html
if exist script.txt if not exist script.js ren script.txt script.js
if exist style.txt if not exist style.css ren style.txt style.css

echo.
echo Iniciando servidor web...
echo.

python servidor_web.py

pause