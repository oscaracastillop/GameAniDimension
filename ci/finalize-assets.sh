#!/usr/bin/env bash
set -e

ASSETS=src/main/assets/defaults
UPDATES=src/main/assets/update
ARCH=("armeabi-v7a" "arm64-v8a" "x86" "x86_64" )
OUTPUT=("libocgcorev7.so" "libocgcorev8.so" "libocgcorex86.so" "libocgcorex64.so")
mkdir -p "$ASSETS"
mkdir -p "$UPDATES"

for i in {0..3}; do
	CORE="libs/${ARCH[i]}/libocgcore.so"
	if [[ ! -f "$CORE" ]]; then
		CORE="deps/ocgcore/libs/${ARCH[i]}/libocgcore.so"
	fi
	if [[ -f "$CORE" ]]; then
		echo "Copying $CORE to $ASSETS/${OUTPUT[i]}..."
		cp "$CORE" "$ASSETS/${OUTPUT[i]}"
		cp "$CORE" "$UPDATES/${OUTPUT[i]}"
	fi
done

cd "$ASSETS"
touch .nomedia
find -L . ! -path . -type d | sed -E "s|^\./||" > ../index.txt
find -L . ! -path . | sed -E "s|^\./||" > ../filelist.txt

filelist_count=$(wc -l < ../filelist.txt)
echo "filelist.txt entry count: $filelist_count"
if [ "$filelist_count" -lt 10000 ]; then
    echo "ERROR: filelist.txt has only $filelist_count lines! Assets were not properly generated!"
    exit 1
fi

cd ../update
touch .nomedia
find -L . ! -path . -type d | sed -E "s|^\./||" > ../indexu.txt
find -L . ! -path . | sed -E "s|^\./||" > ../filelistu.txt

echo "=== Finalize assets complete! Verified $filelist_count core assets. ==="
