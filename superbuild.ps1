# --- CONFIG ---
$target = "esp32"
$version = Get-Content version | Select-Object -First 1
$version_file = $version -replace '\.', '_'
$component_dir = "./components/brew-engine"
$source_file = "./main/main.c"

$web_timestamp_file = "build/.web_build_timestamp"
$web_source_folder = "./web/src"

# --- 1. WEB ELLENŐRZÉS ÉS FORDÍTÁS ---
$need_web_build = $false
if (!(Test-Path -Path "$component_dir/index.html.gz") -or !(Test-Path -Path $web_timestamp_file)) {
    $need_web_build = $true
} else {
    $last_web_build = (Get-Item $web_timestamp_file).LastWriteTime
    $latest_change = Get-ChildItem -Path $web_source_folder -Recurse | 
                     Measure-Object -Property LastWriteTime -Maximum | 
                     Select-Object -ExpandProperty Maximum

    if ($latest_change -gt $last_web_build) { $need_web_build = $true }
}

if ($need_web_build) {
    Write-Host ">>> Változást észleltem a webes kódban, fordítás indítása..." -ForegroundColor Yellow
    Push-Location web
    npm run build
    Pop-Location

    if (Test-Path -Path "$component_dir/index.html.gz") { Remove-Item "$component_dir/index.html.gz" }
    Copy-Item -Path "./web/dist/index.html" -Destination "$component_dir/index.html"

    # Tiszta GZIP tömörítés
    $input_file = [System.IO.File]::OpenRead((Resolve-Path "$component_dir/index.html"))
    $output_file = [System.IO.File]::Create(([System.IO.Path]::GetFullPath("$component_dir/index.html.gz")))
    $gzip_stream = [System.IO.Compression.GZipStream]::new($output_file, [System.IO.Compression.CompressionMode]::Compress)
    $input_file.CopyTo($gzip_stream)
    $gzip_stream.Close(); $output_file.Close(); $input_file.Close()

    Remove-Item "$component_dir/index.html"
    Copy-Item -Path "./web/dist/manifest.json" -Destination "$component_dir/manifest.json"

    if (!(Test-Path -Path build)) { New-Item -ItemType Directory -Path build | Out-Null }
    New-Item -ItemType File -Path $web_timestamp_file -Force | Out-Null

    if (Test-Path -Path $source_file) { (Get-Item $source_file).LastWriteTime = [System.DateTime]::Now }
} else {
    Write-Host ">>> A webes kód változatlan, frontend build ÁTUGRÁSA." -ForegroundColor Green
}

# --- 2. ESP-IDF GYORS TOTAL BUILD ---
Write-Host "`n>>> Gyors Teljes Build indítása... [$target | V$version]" -ForegroundColor Cyan

if (!(Test-Path -Path release)) { New-Item -ItemType Directory -Path release | Out-Null }

# Az idf.py build alapból megépíti mindhárom binárist inkrementálisan!
idf.py build

# Fájlok elérési útjai a build mappában
$app_bin = "build/esp-brew-engine.bin"
$boot_bin = "build/bootloader/bootloader.bin"
$part_bin = "build/partition_table/partition-table.bin"

# Ellenőrzés és másolás a release mappába (külön nevesítve)
if (Test-Path -Path $app_bin) {
    # 1. Fő alkalmazás másolása
    Copy-Item $app_bin -Destination "release/esp-brew-engine_${target}_${version_file}.bin"
    
    # 2. Bootloader másolása (ha létezik)
    if (Test-Path -Path $boot_bin) {
        Copy-Item $boot_bin -Destination "release/bootloader_${target}_${version_file}.bin"
    }
    
    # 3. Partíciós tábla másolása (ha létezik)
    if (Test-Path -Path $part_bin) {
        Copy-Item $part_bin -Destination "release/partition-table_${target}_${version_file}.bin"
    }

    Write-Host "`n[SIKER] Mindhárom gyári bináris elkészült a release mappában!" -ForegroundColor Green
} else {
    Write-Host "`n[HIBA] A build sikertelen volt, a fájlok hiányoznak!" -ForegroundColor Red
}
