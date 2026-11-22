#!/bin/sh
# Genera assets/icons/<N>x<N>.png da assets/ribalta.svg (richiede rsvg-convert) e
# assets/ribalta.ico per Windows (richiede ImageMagick: magick, oppure convert della versione 6).
# Per ogni dimensione la linea di taglio viene allineata a pixel interi,
# così resta netta anche a 16-32 px invece di diventare una sfumatura grigia.
set -eu
SRC=assets/ribalta.svg
OUT=assets/icons
SIZES="16 22 24 32 48 64 128 256 512"
mkdir -p "$OUT"
for n in $SIZES; do
    # linea originale: y=125, altezza=6 su 256 unità -> pixel interi a questa dimensione
    svg=$(awk -v n="$n" 'BEGIN {
        k = n / 256; h = int(6 * k + 0.5); if (h < 1) h = 1
        y = int(128 * k - h / 2 + 0.5)
        printf "y=\"%.4f\" width=\"240\" height=\"%.4f\"", y / k, h / k }')
    sed "s|y=\"125.0\" width=\"240\" height=\"6\"|$svg|" "$SRC" |
        rsvg-convert -w "$n" -h "$n" -o "$OUT/${n}x${n}.png"
done
echo "icone generate in $OUT: $SIZES"

# assets/ribalta.ico: le PNG già pronte (stessa linea di taglio nitida) in un unico file.
# ImageMagick salva ogni dimensione in BMP a 32 bit (anche la 256: ~270 KB in più, ma è il
# formato classico che windres e ogni altro strumento leggono senza problemi)
ICO=assets/ribalta.ico
ICO_SIZES="16 24 32 48 64 128 256"
if command -v magick >/dev/null 2>&1; then
    IM=magick
elif command -v convert >/dev/null 2>&1; then
    IM=convert
else
    echo "ImageMagick (magick o convert) non trovato: $ICO non rigenerata" >&2
    exit 1
fi
set --
for n in $ICO_SIZES; do
    set -- "$@" "$OUT/${n}x${n}.png"
done
"$IM" "$@" "$ICO"
echo "icona Windows generata: $ICO ($ICO_SIZES)"
