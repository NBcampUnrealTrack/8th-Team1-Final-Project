@echo off
REM ---------------------------------------------------------------------------
REM  Multi-User Editing editor launcher (all participants).
REM
REM  ASCII only on purpose: cmd.exe parses .bat files in the OEM codepage and
REM  mangles UTF-8 Korean text badly enough to break control flow.
REM  All logic and messages live in Start-MU-Editor.ps1.
REM ---------------------------------------------------------------------------
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-MU-Editor.ps1"
