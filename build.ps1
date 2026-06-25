$env:IDF_PATH = "C:\Users\ThinkPad\esp-idf-v5.4"
$env:IDF_PYTHON_ENV_PATH = "C:\Users\ThinkPad\.espressif\python_env\idf5.4_py3.12_env"

Set-Location "C:\Users\ThinkPad\WorkBuddy\2026-06-25-16-58-51\xrs_lvgl_demo"

$idf_py = Join-Path $env:IDF_PATH "tools\idf.py"
$python = "C:\Users\ThinkPad\.espressif\python_env\idf5.4_py3.12_env\Scripts\python.exe"

Write-Host "=== Setting target ==="
& $python $idf_py set-target esp32
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "=== Building ==="
& $python $idf_py build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "=== BUILD SUCCESS ==="
