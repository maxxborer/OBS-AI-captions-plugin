@echo off
setlocal

set "AI_CAPTION_INSTALLER_SCRIPT=%~dp0Install-AICaptionPlugin.ps1"

pwsh.exe -NoLogo -NoProfile -NonInteractive -Command "$ErrorActionPreference = 'Stop'; Unblock-File -LiteralPath $env:AI_CAPTION_INSTALLER_SCRIPT"
set "installerExitCode=%ERRORLEVEL%"
if not "%installerExitCode%"=="0" goto installationFailed

pwsh.exe -NoLogo -NoProfile -File "%AI_CAPTION_INSTALLER_SCRIPT%" %*
set "installerExitCode=%ERRORLEVEL%"

if "%installerExitCode%"=="0" exit /b 0

:installationFailed
echo.
echo AI Caption Plugin installation failed. Review the message above.
pause

exit /b %installerExitCode%
