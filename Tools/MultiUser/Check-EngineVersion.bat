@echo off
REM ---------------------------------------------------------------------------
REM  Print the engine build fingerprint for Multi-User session pre-checks.
REM
REM  ASCII only on purpose: cmd.exe parses .bat files in the OEM codepage and
REM  mangles UTF-8 Korean text badly enough to break control flow.
REM  All logic and messages live in Check-EngineVersion.ps1.
REM ---------------------------------------------------------------------------
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Check-EngineVersion.ps1"
