@echo off
rem Generates the recompiled C for the three games from the ROMs in Reference\StrikeHDCollection\rom.
cd /d "%~dp0..\Reference\StrikeHDCollection" || exit /b 1
for %%g in (desert jungle urban) do (
  python tools\trace.py --game %%g || exit /b 1
  python tools\recomp.py --game %%g || exit /b 1
)
