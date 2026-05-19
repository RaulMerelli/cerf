@echo off
:: Thin wrapper around build.ps1. All build logic lives there; this file
:: exists so `build` (or `build debug`) works from a plain cmd.exe without
:: typing the powershell incantation. Forwards every arg verbatim.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
exit /b %ERRORLEVEL%
