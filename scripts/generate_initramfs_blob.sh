#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 INPUT_TAR OUTPUT_C" >&2
    exit 1
fi

input_tar=$1
output_c=$2
tmp_output="${output_c}.tmp"

od -An -v -tx1 "$input_tar" | awk '
BEGIN {
    print "#include \"initramfs_demo_blob.h\""
    print ""
    print "const unsigned char g_initramfs_demo_blob[] = {"
    total = 0
    col = 0
}
{
    for (i = 1; i <= NF; i++) {
        if (col == 0) {
            printf "  "
        } else {
            printf " "
        }
        printf "0x%s,", $i
        total++
        col++
        if (col == 12) {
            printf "\n"
            col = 0
        }
    }
}
END {
    if (col != 0) {
        printf "\n"
    }
    print "};"
    print ""
    printf "const unsigned int g_initramfs_demo_blob_len = %d;\n", total
}
' > "$tmp_output"

mv "$tmp_output" "$output_c"
