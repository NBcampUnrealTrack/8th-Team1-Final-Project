@echo off
REM ---------------------------------------------------------------------------
REM  Multi-User Editing server launcher (host PC only).
REM
REM  ASCII only on purpose: cmd.exe parses .bat files in the OEM codepage and
REM  mangles UTF-8 Korean text badly enough to break control flow.
REM  All logic and messages live in Start-MU-Server.ps1.
REM ---------------------------------------------------------------------------
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-MU-Server.ps1"
