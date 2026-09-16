# Hardcoded configuration
$target = "esp32"
$version = Get-Content version | Select-Object -First 1
$version_file = $version -replace '\.', '_'
$source_file = "./main/main.cpp" # Ensure this path matches your main C file

Write-Host ">>> Starting Fast Upgrade Build... [$target | V$version]" -ForegroundColor Cyan

# 1. Ensure release directory exists
if (!(Test-Path -Path release)) {
    New-Item -ItemType Directory -Path release | Out-Null
}

# 2. Force the compiler to run by touching the main source file
# This prevents the "missing binary" error during check_sizes.py
if (Test-Path -Path $source_file) {
    (Get-Item $source_file).LastWriteTime = [System.DateTime]::Now
} else {
    Write-Host "[WARNING] Main source file not found at $source_file. Build might skip." -ForegroundColor Yellow
}

# 3. Fast incremental build
idf.py build

# Check if the compiler itself failed
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[ERROR] idf.py build failed with exit code $LASTEXITCODE!" -ForegroundColor Red
    exit $LASTEXITCODE
}

# 4. Copy and final verification
$output_bin = "build/esp-brew-engine.bin"
if (Test-Path -Path $output_bin) {
    Copy-Item $output_bin -Destination "release/esp-brew-engine_${target}_${version_file}.bin" -Force
    Write-Host "`n[SUCCESS] The upgrade binary is ready in the release folder!" -ForegroundColor Green
} else {
    Write-Host "`n[ERROR] Build finished but the binary was not found!" -ForegroundColor Red
    exit 1
}
