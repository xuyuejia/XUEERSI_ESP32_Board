@echo off
set IDF_PATH=C:\Users\ThinkPad\esp-idf-v5.4
set IDF_PYTHON_ENV_PATH=C:\Users\ThinkPad\.espressif\python_env\idf5.4_py3.12_env
set PATH=C:\Users\ThinkPad\.espressif\python_env\idf5.4_py3.12_env\Scripts;C:\Users\ThinkPad\esp-idf-v5.4\tools;%PATH%

cd /d C:\Users\ThinkPad\WorkBuddy\2026-06-25-16-58-51\xrs_lvgl_demo

echo === Setting target esp32 ===
python C:\Users\ThinkPad\esp-idf-v5.4\tools\idf.py set-target esp32
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo === Building ===
python C:\Users\ThinkPad\esp-idf-v5.4\tools\idf.py build
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo === BUILD SUCCESS ===
