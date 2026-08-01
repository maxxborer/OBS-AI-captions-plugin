@echo off
setlocal

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-AICaptionPlugin.ps1" %*
set "installerExitCode=%ERRORLEVEL%"

if not "%installerExitCode%"=="0" (
  echo.
  echo AI Caption Plugin installation failed. Review the message above.
  pause
)

exit /b %installerExitCode%
