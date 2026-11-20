@echo off
setlocal
pushd "%~dp0"
if errorlevel 1 exit /b 1

echo PROJECT ECHO
echo.
echo 1 - Windows: Release, keyboard prompts, tests
echo 2 - PS5: controller prompts, keyboard/IME disabled
echo 0 - Cancel
echo.
choice /C 120 /N /M "Select platform: "
if errorlevel 255 goto choice_failed
if errorlevel 3 goto cancelled
if errorlevel 2 goto ps5
if errorlevel 1 goto windows
goto cancelled

:windows
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-windows.ps1" -Configuration Release -RunTests
set "build_exit=%errorlevel%"
goto finished

:ps5
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-ps5.ps1" -Target Game -SkipPs5Keyboard -RestoreDependencies
set "build_exit=%errorlevel%"
goto finished

:choice_failed
echo Platform selection failed.
set "build_exit=1"
goto finished

:cancelled
popd
exit /b 0

:finished
echo.
if "%build_exit%"=="0" (echo Build succeeded.) else (echo Build failed with exit code %build_exit%.)
pause
popd
exit /b %build_exit%
