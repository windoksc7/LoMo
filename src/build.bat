@echo off
cl /utf-8 main.c document.c editor.c render.c fileio.c parser.c logic.c /Fe:LoMo.exe user32.lib gdi32.lib imm32.lib comdlg32.lib
if %errorlevel% neq 0 pause