@echo off
REM ============================================================================
REM  Build the client program: one standalone TunedByWali-NBT-Toolbox.exe
REM
REM  Run this ONCE on any Windows PC that has Python 3 installed (your shop PC).
REM  Your CLIENTS do NOT need Python - they just get the .exe this produces.
REM
REM  Double-click this file, or run it from a command prompt. When it finishes,
REM  the program is at:  dist\TunedByWali-NBT-Toolbox.exe
REM ============================================================================
setlocal
cd /d "%~dp0"

echo.
echo === TunedByWali NBT Evo Toolbox - Windows build ===
echo.

REM --- find Python ---
set PY=
where py >nul 2>nul && set PY=py
if "%PY%"=="" ( where python >nul 2>nul && set PY=python )
if "%PY%"=="" (
  echo Python 3 is not installed on THIS PC ^(only needed here to build, not by clients^).
  echo Install it from https://www.python.org/downloads/  ^(tick "Add Python to PATH"^), then run this again.
  echo.
  pause
  exit /b 1
)

echo Using Python: %PY%
echo Installing the build tool ^(PyInstaller^)...
%PY% -m pip install --upgrade pip pyinstaller || (
  echo.
  echo Could not install PyInstaller. Check this PC's internet connection and try again.
  pause
  exit /b 1
)

echo.
echo Building ^(this takes a few minutes^)...
%PY% -m PyInstaller --noconfirm packaging\toolbox.spec || (
  echo.
  echo Build failed. Scroll up for the error, or send it to your TunedByWali contact.
  pause
  exit /b 1
)

echo.
echo ============================================================================
echo  DONE.  Your client program is:
echo     dist\TunedByWali-NBT-Toolbox.exe
echo.
echo  Give that single .exe to clients. They double-click it; the toolbox opens
echo  in their browser. No Python needed on their end. OpenSSH is built into
echo  Windows 10/11 already.
echo ============================================================================
echo.
pause
