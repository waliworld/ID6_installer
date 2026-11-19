@echo off
cd /d "%~dp0"
where py >nul 2>nul && (py app.py & goto :eof)
where python >nul 2>nul && (python app.py & goto :eof)
echo Python is not installed. Download the double-click .exe from the repo's Releases page instead,
echo or install Python 3 from https://www.python.org/downloads/ and run this again.
pause
