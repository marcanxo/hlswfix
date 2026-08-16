@echo off
rem Double click this to install the fix.
rem
rem It finds your HLSW folder by itself, puts hlswfix.dll and hlswfix.ini
rem there, renames hlsw.exe to hlsw-real.exe and takes its place, so every
rem existing shortcut starts HLSW with the fix in it. uninstall.cmd puts it
rem all back. HLSW usually sits under Program Files, so Windows will ask for
rem administrator rights.
rem
rem This only starts install.ps1, which is a text file you can read first. The
rem ExecutionPolicy switch applies to that one call and changes no setting.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
echo.
pause
