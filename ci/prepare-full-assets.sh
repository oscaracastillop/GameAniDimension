#!/usr/bin/env bash
set -e

ASSETS=src/main/assets/defaults
UPDATES=src/main/assets/update
mkdir -p "$ASSETS"
mkdir -p "$UPDATES"

echo "=== [1/4] Downloading font ==="
curl --retry 5 --connect-timeout 30 --location -o NotoSansJP-Regular.otf https://raw.githubusercontent.com/ProjectIgnis/Distribution/master/fonts/NotoSansJP-Regular.otf
lua ci/bin2c.lua "NotoSansJP-Regular.otf" "deps/edopro/gframe/CGUITTFont/bundled_font.cpp"

echo "=== [2/4] Cloning Distribution assets (BGM, sound, textures, expansions) ==="
git clone --depth 1 https://github.com/ProjectIgnis/Distribution.git /tmp/Distribution

echo "Copying assets into $ASSETS..."
cp -r /tmp/Distribution/sound "$ASSETS/"
cp -r /tmp/Distribution/textures "$ASSETS/"
cp -r /tmp/Distribution/skin "$ASSETS/"
cp -r /tmp/Distribution/fonts "$ASSETS/"
cp -r /tmp/Distribution/expansions "$ASSETS/"
if [ -d "/tmp/Distribution/config" ]; then
    cp -r /tmp/Distribution/config "$ASSETS/" || true
fi

echo "=== [3/4] Downloading WindBot resources ==="
curl --retry 5 --connect-timeout 30 --location -o /tmp/WindBotIgnite-Resources.7z https://github.com/ProjectIgnis/windbot/releases/download/20250412/WindBotIgnite-Resources.7z
7z x /tmp/WindBotIgnite-Resources.7z -o"$ASSETS" -y
7z x /tmp/WindBotIgnite-Resources.7z -o"$UPDATES" -y

echo "=== [4/4] Generating index and filelist ==="
cd "$ASSETS"
touch .nomedia
find -L . ! -path . -type d | sed -E "s|^\./||" > ../index.txt
find -L . ! -path . | sed -E "s|^\./||" > ../filelist.txt

cd ../update
touch .nomedia
find -L . ! -path . -type d | sed -E "s|^\./||" > ../indexu.txt
find -L . ! -path . | sed -E "s|^\./||" > ../filelistu.txt

echo "=== Assets prepared successfully! ==="
