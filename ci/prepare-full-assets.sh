#!/usr/bin/env bash
set -e

ASSETS=src/main/assets/defaults
UPDATES=src/main/assets/update
mkdir -p "$ASSETS"
mkdir -p "$UPDATES"

echo "=== [1/6] Downloading font ==="
curl --retry 5 --connect-timeout 30 --location -o NotoSansJP-Regular.otf https://raw.githubusercontent.com/ProjectIgnis/Distribution/master/fonts/NotoSansJP-Regular.otf
if command -v python3 >/dev/null 2>&1; then
    python3 ci/bin2c.py "NotoSansJP-Regular.otf" "deps/edopro/gframe/CGUITTFont/bundled_font.cpp"
elif command -v lua >/dev/null 2>&1; then
    lua ci/bin2c.lua "NotoSansJP-Regular.otf" "deps/edopro/gframe/CGUITTFont/bundled_font.cpp"
fi

echo "=== [2/6] Cloning Distribution assets (BGM, sound, textures, expansions, skin, config) ==="
git clone --depth 1 https://github.com/ProjectIgnis/Distribution.git /tmp/Distribution

echo "Copying Distribution assets into $ASSETS and $UPDATES..."
cp -r /tmp/Distribution/sound "$ASSETS/"
cp -r /tmp/Distribution/textures "$ASSETS/"
cp -r /tmp/Distribution/skin "$ASSETS/"
cp -r /tmp/Distribution/fonts "$ASSETS/"
cp -r /tmp/Distribution/expansions "$ASSETS/"
if [ -d "/tmp/Distribution/config" ]; then
    cp -r /tmp/Distribution/config "$ASSETS/" || true
fi

echo "=== [3/6] Downloading WindBot base resources ==="
curl --retry 5 --connect-timeout 30 --location -o /tmp/WindBotIgnite-Resources.7z https://github.com/ProjectIgnis/windbot/releases/download/20250412/WindBotIgnite-Resources.7z
7z x /tmp/WindBotIgnite-Resources.7z -o"$ASSETS" -y

echo "=== [4/6] Pre-populating field spell illustrations ==="
mkdir -p "$ASSETS/pics/field"
FIELD_CODES=(59197169 86318356 87430998 23424603 22702055 50913601 47355498 37694547 43175858 75376965 76375976 62265044 22804644 39284521 35371948 41440148 47594939 41418852 92246806 74892653 62015408 75041269 80987696 20624263 71645242 67616300 97574404 90011152 295517 60519422 56433456 95440946 38468214 32354768 93302695 42713844 6568731 56673480 24094653 84941194 74725513)
for code in "${FIELD_CODES[@]}"; do
    curl -s --connect-timeout 10 -o "$ASSETS/pics/field/$code.jpg" "https://images.ygoprodeck.com/images/cards_cropped/$code.jpg" || true
done

echo "=== [5/6] Integrating Ani Dimension Game Assets (cards.cdb, scripts, banlists, decks, series) ==="

# 1. Master card database
if [ -f "src/main/assets/WindBot/cards.cdb" ]; then
    echo "Installing cards.cdb into defaults/ and expansions/..."
    cp "src/main/assets/WindBot/cards.cdb" "$ASSETS/cards.cdb"
    mkdir -p "$ASSETS/expansions"
    cp "src/main/assets/WindBot/cards.cdb" "$ASSETS/expansions/cards.cdb"
fi

# 2. Official Card Lua Scripts
echo "Cloning official CardScripts..."
git clone --depth 1 https://github.com/ProjectIgnis/CardScripts.git /tmp/CardScripts
mkdir -p "$ASSETS/script"
cp -r /tmp/CardScripts/official "$ASSETS/script/" 2>/dev/null || true
cp -r /tmp/CardScripts/*.lua "$ASSETS/script/" 2>/dev/null || true
cp -r /tmp/CardScripts/* "$ASSETS/script/" 2>/dev/null || true
rm -rf "$ASSETS/script/.git" "$ASSETS/script/.github" 2>/dev/null || true

# 3. LFLists (Forbidden & Limited lists)
echo "Cloning official LFLists..."
git clone --depth 1 https://github.com/ProjectIgnis/LFLists.git /tmp/LFLists
mkdir -p "$ASSETS/lflists"
cp /tmp/LFLists/*.conf "$ASSETS/lflists/" 2>/dev/null || true
cp /tmp/LFLists/*.conf "$ASSETS/" 2>/dev/null || true
rm -rf "$ASSETS/lflists/.git" 2>/dev/null || true

# 4. Custom Textures (Series logos, background, etc.)
if [ -d "src/main/assets/textures" ]; then
    echo "Copying custom textures (series logos, bg)..."
    mkdir -p "$ASSETS/textures"
    cp -r src/main/assets/textures/* "$ASSETS/textures/"
fi

# 5. User and NPC Decks
if [ -d "src/main/assets/deck" ]; then
    echo "Copying deck collections..."
    mkdir -p "$ASSETS/deck"
    cp -r src/main/assets/deck/* "$ASSETS/deck/"
fi

# 6. WindBot Ani Dimension Overrides (Decks, bots.json, Executors)
if [ -d "src/main/assets/WindBot" ]; then
    echo "Copying Ani Dimension WindBot packages..."
    mkdir -p "$ASSETS/WindBot"
    cp -r src/main/assets/WindBot/* "$ASSETS/WindBot/"
fi

# 7. Strings config copy
if [ -f "$ASSETS/config/strings.conf" ]; then
    cp "$ASSETS/config/strings.conf" "$ASSETS/expansions/strings.conf" 2>/dev/null || true
fi

# 8. Ani Dimension Config Overrides (Exclusive Server, No Repos, Salas Duelos)
if [ -d "src/main/assets/config" ]; then
    echo "Applying Ani Dimension custom config overrides..."
    mkdir -p "$ASSETS/config"
    cp -r src/main/assets/config/* "$ASSETS/config/"
    cp "$ASSETS/config/strings.conf" "$ASSETS/expansions/strings.conf" 2>/dev/null || true
    cp "$ASSETS/config/strings.conf" "$ASSETS/strings.conf" 2>/dev/null || true
fi

echo "=== [6/6] Generating initial index and filelist ==="
cd "$ASSETS"
touch .nomedia
find -L . ! -path . -type d | sed -E "s|^\./||" > ../index.txt
find -L . ! -path . | sed -E "s|^\./||" > ../filelist.txt

cd ../update
touch .nomedia
find -L . ! -path . -type d | sed -E "s|^\./||" > ../indexu.txt
find -L . ! -path . | sed -E "s|^\./||" > ../filelistu.txt

echo "=== Assets prepared successfully! ==="
