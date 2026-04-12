@echo off
setlocal

echo PowerCapslock now uses the CMake build pipeline.
echo Forwarding to scripts\build.bat...
echo.

call "%~dp0build.bat"
exit /b %errorlevel%
