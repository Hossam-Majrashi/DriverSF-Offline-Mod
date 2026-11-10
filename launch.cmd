@echo off
setlocal
pushd "%~dp0.." || exit /b 1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-windows.ps1"
if errorlevel 1 (
  echo Preparation failed. Game was not launched.
  popd
  pause
  exit /b 1
)
rem Official offline option; ownership requirements are unchanged.
"%CD%\Driver.exe" -offline %*
set "result=%ERRORLEVEL%"
popd
exit /b %result%
