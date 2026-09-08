@echo off
setlocal
cd /d "%~dp0"
echo Map: http://localhost:8000/gaode.html
echo Keep this window open while using the ground station.
where py >nul 2>nul
if not errorlevel 1 (
    py -3 -m http.server 8000 --bind 127.0.0.1
) else (
    python -m http.server 8000 --bind 127.0.0.1
)
pause
