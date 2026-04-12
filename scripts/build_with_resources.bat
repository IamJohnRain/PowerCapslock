@echo off
setlocal

echo PowerCapslock resources are copied by CMake after each build.
echo Forwarding to scripts\build.bat...
echo.

call "%~dp0build.bat"
exit /b %errorlevel%
