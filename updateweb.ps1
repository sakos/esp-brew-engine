# Build web
cd ./web
npm run build
cd ..

# Copy index.html from web/dist to components/brew-engine
Copy-Item -Path "./web/dist/index.html" -Destination "./components/brew-engine/index.html"

# Compress index.html using gzip
tar -czf "./components/brew-engine/index.html.gz" "./components/brew-engine/index.html"
Remove-Item "./components/brew-engine/index.html"

# Copy manifest.json from web/dist to components/brew-engine
Copy-Item -Path "./web/dist/manifest.json" -Destination "./components/brew-engine/manifest.json"