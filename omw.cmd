@echo off
rem The Windows twin of `omw`, which cmd and PowerShell resolve `omw` to through PATHEXT. `py` is the
rem launcher python.org's installer puts on the PATH, and `python` stands in where there is none.
where /q py
if %ERRORLEVEL% equ 0 (
    py -3 "%~dp0omw" %*
) else (
    python "%~dp0omw" %*
)
exit /b %ERRORLEVEL%
