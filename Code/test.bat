@echo off
setlocal
set "TEMP=%~dp0.qa\core-temp"
set "TMP=%TEMP%"
if not exist "%TEMP%" mkdir "%TEMP%"
call "%~dp0build-project.bat" check "%~dp0build\core"
exit /b %ERRORLEVEL%
