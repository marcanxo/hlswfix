@echo off
rem Double click this to remove the fix again.
rem
rem HLSW is left exactly as it was: hlsw-real.exe goes back to being hlsw.exe
rem and the hlswfix files are deleted. HLSW itself is not touched, and nothing
rem this ever did was written to disk inside it.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -Uninstall %*
echo.
pause
