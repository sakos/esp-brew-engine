# Hardkódolt verzió és target beállítások (mivel nem változnak)
$target = "esp32"
$version = Get-Content version | Select-Object -First 1
$version_file = $version -replace '\.', '_'

Write-Host ">>> Gyors Upgrade Build indítása... [$target | V$version]" -ForegroundColor Cyan

# Release mappa ellenőrzése
if (!(Test-Path -Path release)) {
    New-Item -ItemType Directory -Path release | Out-Null
}

# Villámgyors inkrementális build (csak a módosított kódot fordítja)
idf.py build

# Másolás és ellenőrzés
$output_bin = "build/esp-brew-engine.bin"
if (Test-Path -Path $output_bin) {
    Copy-Item $output_bin -Destination "release/esp-brew-engine_${target}_${version_file}.bin"
    Write-Host "`n[SIKER] Az upgrade bináris elkészült a release mappában!" -ForegroundColor Green
} else {
    Write-Host "`n[HIBA] A build sikertelen volt, a bináris nem található!" -ForegroundColor Red
}
