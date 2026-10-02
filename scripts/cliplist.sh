#!/bin/sh
# Clipboard history for the Quickshell panel: "id<TAB>txt<TAB>text" or "id<TAB>img<TAB>/path/to/preview"
# Image entries get decoded once into ~/.cache/rice/clip (the 40 newest).
cache="$HOME/.cache/rice/clip"
mkdir -p "$cache"
tab=$(printf '\t')
cliphist list | head -n 300 | {
    n=0
    while IFS="$tab" read -r id rest; do
        case "$rest" in
            "[[ binary data"*)
                case "$rest" in
                    *jpeg*|*jpg*) ext=jpg ;; *webp*) ext=webp ;; *gif*) ext=gif ;; *bmp*) ext=bmp ;; *) ext=png ;;
                esac
                f="$cache/$id.$ext"
                if [ ! -s "$f" ] && [ "$n" -lt 40 ]; then
                    cliphist decode "$id" > "$f" 2>/dev/null
                fi
                n=$((n + 1))
                printf '%s\timg\t%s\n' "$id" "$f" ;;
            *)
                printf '%s\ttxt\t%s\n' "$id" "$rest" ;;
        esac
    done
}
# forget previews of entries that are gone
ids=$(cliphist list | cut -f1)
ls "$cache" | while read -r f; do
    printf '%s\n' "$ids" | grep -qx "${f%%.*}" || rm -f "$cache/$f"
done 2>/dev/null
