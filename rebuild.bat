@echo off
taskkill /F /IM mangosd.exe 2>nul
del /q C:\vmangos\core\bin\Release\mangosd.exe 2>nul
cd /d C:\vmangos\core\build
cmake --build . --config Release --target mangosd
pause