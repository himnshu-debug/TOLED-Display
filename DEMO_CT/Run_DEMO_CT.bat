@echo off
cd /d "%~dp0"
echo DEMO_CT - water bottle direction demo
echo.
echo Before this starts, make sure:
echo   - The Arduino is plugged in (expected on COM3)
echo   - The Windows Camera app is fully closed (it locks the webcam)
echo.
python detect_and_point.py
echo.
echo Program exited. Press any key to close this window.
pause >nul
