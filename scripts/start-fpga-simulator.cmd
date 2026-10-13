@echo off
setlocal
set "vvSimulator=%~dp0VaporViewFpgaSimulator.exe"
if not exist "%vvSimulator%" set "vvSimulator=%~dp0..\build\Release\VaporViewFpgaSimulator.exe"
if not exist "%vvSimulator%" (
    echo Build build/Release/VaporViewFpgaSimulator.exe first.
    pause
    exit /b 1
)
"%vvSimulator%" %*
set "vvExit=%ERRORLEVEL%"
if not "%vvExit%"=="0" pause
exit /b %vvExit%
