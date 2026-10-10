@echo off
rem The wgf tool on Windows, in cmd and PowerShell: `wgf <command>` runs the wgf script beside
rem this file with Python, the py launcher's Python 3 when it is there, else `python`, every
rem argument passed and its exit code returned. Its only job is finding Python; what wgf does
rem is tools/wgf/cli.py's (CONVENTIONS.md, "Tooling").
setlocal
where py >nul 2>nul
if %errorlevel% equ 0 (
    py -3 "%~dp0wgf" %*
) else (
    python "%~dp0wgf" %*
)
exit /b %errorlevel%
