@echo off
rem Double click this to install the fix.
rem
rem It finds your HLSW folder by itself, puts the two files there, and makes
rem every existing shortcut start HLSW with the fix in it. uninstall.cmd puts
rem everything back.
rem
rem This only starts install.ps1, which is a text file you can read first. The
rem ExecutionPolicy switch applies to that one call and changes no setting.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
echo.
pause
