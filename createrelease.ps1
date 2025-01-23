# Prompt for target with default esp32
$target = Read-Host "Target [esp32]"
if ([string]::IsNullOrWhiteSpace($target)) {
    $target = "esp32"
}

# Read version from version file
$version = Get-Content version | Select-Object -First 1
$version_file = $version -replace '\.', '_'

Write-Host "Building Version: $version"

# Create release directory if it doesn't exist
if (!(Test-Path -Path release)) {
    New-Item -ItemType Directory -Path release | Out-Null
}

# Clean old config
if (Test-Path -Path sdkconfig) {
    Remove-Item sdkconfig
}

if (Test-Path -Path loader/sdkconfig) {
    Remove-Item loader/sdkconfig
}

# Build Engine
idf.py fullclean
idf.py set-target $target
idf.py build

# Build Loader
Push-Location loader
idf.py fullclean
idf.py set-target $target
idf.py build
Pop-Location

# Copy to release
Copy-Item build/esp-brew-engine.bin -Destination "release/esp-brew-engine_${target}_${version_file}.bin"

# Combine Loader Release
esptool.py --chip $target merge_bin `
  -o "release/esp-brew-engine-loader_${target}_${version_file}.bin" `
  --flash_mode dio `
  --flash_freq 40m `
  --flash_size 4MB `
  0x1000 loader/build/bootloader/bootloader.bin `
  0x8000 loader/build/partition_table/partition-table.bin `
  0x40000 loader/build/esp-brew-engine-loader.bin `
  0x35000 loader/build/ota_data_initial.bin

# Combine LoaderFull Release
esptool.py --chip $target merge_bin `
  -o "release/esp-brew-engine-full_${target}_${version_file}.bin" `
  --flash_mode dio `
  --flash_freq 40m `
  --flash_size 4MB `
  0x1000 loader/build/bootloader/bootloader.bin `
  0x8000 loader/build/partition_table/partition-table.bin `
  0x40000 loader/build/esp-brew-engine-loader.bin `
  0x35000 misc/ota_boot_ota0.bin `
  0x110000 build/esp-brew-engine.bin
