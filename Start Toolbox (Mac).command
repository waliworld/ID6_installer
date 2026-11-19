#!/bin/bash
cd "$(dirname "$0")"
command -v python3 >/dev/null 2>&1 && exec python3 app.py
echo "Python 3 is not installed. Download the double-click app from the repo's Releases page,"
echo "or install Python 3 from https://www.python.org/downloads/ and run this again."
read -r -p "Press Enter to close."
