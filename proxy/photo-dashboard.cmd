@echo off
rem Double-click launcher for the Blipscope photo curation dashboard.
rem Starts the local server and opens the browser; close this window to stop it.
cd /d "%~dp0"
start "" http://127.0.0.1:8123
npm run dashboard
