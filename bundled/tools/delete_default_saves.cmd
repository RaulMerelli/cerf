@echo off
rem Delete every state.img save under ..\devices (recursive).
setlocal
for /r "%~dp0\..\devices" %%F in (state.img) do (
    echo Deleting "%%F"
    del /f /q "%%F"
)
endlocal
