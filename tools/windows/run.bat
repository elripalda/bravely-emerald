@echo off
rem Runs the game from its own folder (app0\ and userdata\ are found next to the exe).
cd /d "%~dp0"
start "" pokeemerald.exe %*
