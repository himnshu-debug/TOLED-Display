@echo off
cd /d "%~dp0"
echo Demo_Depth_CT - water bottle direction + distance demo
echo.
echo Before this starts, make sure:
echo   - The Arduino is plugged in (expected on COM3)
echo   - The Windows Camera app is fully closed (it locks the webcam)
echo   - First run downloads the Depth Anything model (needs internet, a bit slower to start)
echo.
python detect_depth_and_point.py
echo.
echo Program exited. Press any key to close this window.
pause >nul
