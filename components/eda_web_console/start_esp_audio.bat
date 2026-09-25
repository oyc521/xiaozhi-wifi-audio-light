@echo off
if not exist "%TEMP%\esp_loopback.py" curl -s "http://10.35.24.126/loopback.py" -o "%TEMP%\esp_loopback.py"
echo Installing Python deps (first time needs internet)...
python -m pip install pyaudiowpatch numpy -q
echo Starting stream. Close this window to stop.
python "%TEMP%\esp_loopback.py" 10.35.24.126 5004
if errorlevel 1 ( echo Failed. Make sure Python is installed and on PATH. )
pause
