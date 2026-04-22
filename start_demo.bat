@echo off
setlocal

set "ROOT=D:\RX_Gateway\RX_ESP32_Firestore"
set "PYTHON=%ROOT%\.venv\Scripts\python.exe"
set "PIO=C:\Users\HOANGANH\.platformio\penv\Scripts\platformio.exe"

cd /d "%ROOT%"

echo.
echo ========================================
echo RX Gateway Demo Launcher
echo ========================================
echo.
echo Starting MQTT -> Firestore bridge...
echo.

start "MQTT Bridge" powershell.exe -NoExit -ExecutionPolicy Bypass -Command "Set-Location '%ROOT%'; & '%PYTHON%' 'mqtt_to_firebase.py'"

echo Demo windows started.
echo Keep the bridge window open during demo.
echo.

endlocal
