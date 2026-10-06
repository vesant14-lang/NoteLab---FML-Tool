@echo off
setlocal
call "%~dp0build-project.bat" app "%~dp0build\app"
exit /b %ERRORLEVEL%
