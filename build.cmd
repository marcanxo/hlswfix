@echo off
rem Builds hlswfix.dll and hlswfix.exe into build\.
rem
rem Both have to be 32 bit, because hlsw.exe is PE32: a 64 bit library cannot
rem be loaded into it, and a 64 bit launcher would look up LoadLibraryA at an
rem address that does not exist over there.
rem
rem -static links the compiler runtime in, so neither file needs anything from
rem the toolchain folder at run time and the two can simply be copied.
rem
rem Set MINGW to the bin folder of a 32 bit MinGW-w64 toolchain, or leave it
rem unset and gcc is taken from the PATH:
rem
rem     set MINGW=C:\mingw32\bin
rem     build.cmd

setlocal
set OUT=%~dp0build
set SRC=%~dp0src
set CFLAGS=-O2 -Wall -Wextra -static -static-libgcc

if "%MINGW%"=="" (set GCC=gcc) else (set GCC=%MINGW%\gcc.exe)

"%GCC%" --version >nul 2>&1
if errorlevel 1 (
    echo gcc was not found.
    echo.
    echo Set MINGW to the bin folder of a 32 bit MinGW-w64 toolchain, or put
    echo one on the PATH. A portable build unpacks anywhere and needs no
    echo installation.
    exit /b 1
)

rem A 64 bit toolchain will happily accept the sources and produce something
rem that cannot be loaded into HLSW at all, which is a confusing way to find
rem out. Better to say so here.
for /f %%i in ('"%GCC%" -dumpmachine') do set TARGET=%%i
echo %TARGET% | findstr /i "i686 i586 i486 i386" >nul
if errorlevel 1 (
    echo This gcc targets %TARGET%, which is not 32 bit.
    echo.
    echo hlsw.exe is a 32 bit program: a 64 bit dll cannot be loaded into it.
    echo Use an i686 toolchain.
    exit /b 1
)

if not exist "%OUT%" mkdir "%OUT%"

echo Building hlswfix.dll
"%GCC%" %CFLAGS% -shared -o "%OUT%\hlswfix.dll" "%SRC%\hlswfix.c" -lws2_32
if errorlevel 1 exit /b 1

echo Building hlswfix.exe
"%GCC%" %CFLAGS% -mwindows -o "%OUT%\hlswfix.exe" "%SRC%\launcher.c" -lws2_32
if errorlevel 1 exit /b 1

echo.
echo Built into %OUT%.
echo.
echo Copy hlswfix.exe, hlswfix.dll and hlswfix.ini into the folder HLSW is
echo installed in, or run install.ps1 -Dir "C:\Path\To\HLSW".
endlocal
