@echo off
setlocal
for /r "%~dp0\..\devices" %%F in (cerf-user.json) do (
    echo Deleting "%%F"
    del /f /q "%%F"
)
endlocal
