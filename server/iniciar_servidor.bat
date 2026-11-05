@echo off
title Circl3DS Online Relay Server
cd /d "%~dp0"
echo Iniciando Circl3DS Master & Relay Server...
"C:\Users\Diyel\AppData\Local\Python\bin\python3.exe" server.py
if errorlevel 1 (
    echo.
    echo Ocurrio un error al ejecutar Python.
    pause
)
pause
