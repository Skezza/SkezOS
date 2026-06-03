#!/usr/bin/env bash
set -eu

if [ "$#" -lt 3 ]; then
    echo "usage: $0 <iso> <output_ppm> <qemu_log> [boot_wait_secs] [qmp_port] [artifact_label] [qmp_key_sequence] [serial_input_sequence]" >&2
    exit 2
fi

ISO_PATH="$1"
OUTPUT_PPM="$2"
QEMU_LOG="$3"
BOOT_WAIT_SECS="${4:-16}"
QMP_PORT="${5:-45557}"
QEMU_BIN="${QEMU:-qemu-system-i386}"
META_PATH="${OUTPUT_PPM%.ppm}.meta"
ARTIFACT_DIR="$(dirname "$OUTPUT_PPM")"
ARTIFACT_LABEL="${6:-failure}"
QMP_KEY_SEQUENCE="${7:-}"
SERIAL_INPUT_SEQUENCE="${8:-}"
LATEST_PPM_PATH="${ARTIFACT_DIR}/gui-fb-${ARTIFACT_LABEL}-latest.ppm"
LATEST_META_PATH="${ARTIFACT_DIR}/gui-fb-${ARTIFACT_LABEL}-latest.meta"
LATEST_QEMU_LOG_PATH="${ARTIFACT_DIR}/gui-fb-${ARTIFACT_LABEL}-latest.qemu.log"
SERIAL_FIFO=""
SERIAL_WRITER_PID=""

if [ ! -f "$ISO_PATH" ]; then
    echo "[capture-qemu-fb-dump] missing iso: $ISO_PATH" >&2
    exit 1
fi

mkdir -p "$ARTIFACT_DIR"
mkdir -p "$(dirname "$QEMU_LOG")"
rm -f "$OUTPUT_PPM"
rm -f "$META_PATH"

if [ -n "$SERIAL_INPUT_SEQUENCE" ]; then
    SERIAL_FIFO="$(mktemp -u "${ARTIFACT_DIR}/gui-fb-serial-${ARTIFACT_LABEL}-XXXXXX.fifo")"
    mkfifo "$SERIAL_FIFO"
    {
        OLD_IFS="$IFS"
        IFS=','
        set -- $SERIAL_INPUT_SEQUENCE
        IFS="$OLD_IFS"
        sleep "$BOOT_WAIT_SECS"
        for token in "$@"; do
            token="$(printf '%s' "$token" | tr -d '[:space:]')"
            if [ -z "$token" ]; then
                continue
            fi
            case "$token" in
                up) printf '\033[A' ;;
                down) printf '\033[B' ;;
                right) printf '\033[C' ;;
                left) printf '\033[D' ;;
                enter) printf '\n' ;;
                backspace) printf '\b' ;;
                text:*) printf '%s' "${token#text:}" ;;
                *) ;;
            esac
            sleep 0.2
        done
    } > "$SERIAL_FIFO" &
    SERIAL_WRITER_PID=$!
    "$QEMU_BIN" \
        -cdrom "$ISO_PATH" \
        -display none \
        -serial stdio \
        -monitor none \
        -qmp "tcp:127.0.0.1:${QMP_PORT},server=on,wait=off" \
        -no-reboot \
        -no-shutdown < "$SERIAL_FIFO" > "$QEMU_LOG" 2>&1 &
else
        "$QEMU_BIN" \
        -cdrom "$ISO_PATH" \
        -display none \
        -serial stdio \
        -monitor none \
        -qmp "tcp:127.0.0.1:${QMP_PORT},server=on,wait=off" \
        -no-reboot \
        -no-shutdown > "$QEMU_LOG" 2>&1 &
fi
QEMU_PID=$!

cleanup() {
    if [ -n "$SERIAL_WRITER_PID" ] && ps -p "$SERIAL_WRITER_PID" >/dev/null 2>&1; then
        kill "$SERIAL_WRITER_PID" >/dev/null 2>&1 || true
    fi
    if ps -p "$QEMU_PID" >/dev/null 2>&1; then
        kill "$QEMU_PID" >/dev/null 2>&1 || true
    fi
    if [ -n "$SERIAL_FIFO" ]; then
        rm -f "$SERIAL_FIFO"
    fi
    if [ -n "$SERIAL_WRITER_PID" ]; then
        wait "$SERIAL_WRITER_PID" >/dev/null 2>&1 || true
    fi
    wait "$QEMU_PID" >/dev/null 2>&1 || true
}
trap cleanup EXIT

connected=0
for _ in $(seq 1 100); do
    if bash -lc "exec 3<>/dev/tcp/127.0.0.1/${QMP_PORT}" 2>/dev/null; then
        connected=1
        break
    fi
    sleep 0.1
done

if [ "$connected" -ne 1 ]; then
    echo "[capture-qemu-fb-dump] qmp connection failed on port ${QMP_PORT}" >&2
    exit 1
fi

sleep "$BOOT_WAIT_SECS"
if [ -n "$SERIAL_INPUT_SEQUENCE" ]; then
    sleep 1.5
fi

if ! exec 3<>/dev/tcp/127.0.0.1/"${QMP_PORT}"; then
    echo "[capture-qemu-fb-dump] qmp command connection failed on port ${QMP_PORT}" >&2
    exit 1
fi

if ! printf '{"execute":"qmp_capabilities"}\n' >&3; then
    echo "[capture-qemu-fb-dump] qmp capabilities command failed" >&2
    exec 3>&-
    exit 1
fi
sleep 0.2

if [ -n "$QMP_KEY_SEQUENCE" ]; then
    OLD_IFS="$IFS"
    IFS=','
    set -- $QMP_KEY_SEQUENCE
    IFS="$OLD_IFS"
    for key in "$@"; do
        key="$(printf '%s' "$key" | tr -d '[:space:]')"
        if [ -z "$key" ]; then
            continue
        fi
        if ! printf '{"execute":"send-key","arguments":{"keys":[{"type":"qcode","data":"%s"}]}}\n' "$key" >&3; then
            echo "[capture-qemu-fb-dump] qmp send-key failed for ${key}" >&2
            exec 3>&-
            exit 1
        fi
        sleep 0.2
    done
fi

if ! printf '{"execute":"screendump","arguments":{"filename":"%s"}}\n' "$OUTPUT_PPM" >&3; then
    echo "[capture-qemu-fb-dump] qmp screendump command failed" >&2
    exec 3>&-
    exit 1
fi
sleep 0.2
printf '{"execute":"quit"}\n' >&3 || true
exec 3>&-

for _ in $(seq 1 100); do
    if [ -s "$OUTPUT_PPM" ]; then
        size_bytes="$(wc -c < "$OUTPUT_PPM" | tr -d '[:space:]')"
        sha256="unavailable"
        header_tokens="$(head -c 48 "$OUTPUT_PPM" | tr -d '\000' | tr '\n\r\t' ' ')"
        timestamp_utc="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        magic=""
        width=""
        height=""
        maxval=""

        if command -v sha256sum >/dev/null 2>&1; then
            sha256="$(sha256sum "$OUTPUT_PPM" | awk '{print $1}')"
        fi

        set -- $header_tokens
        magic="${1:-}"
        width="$(printf '%s' "${2:-}" | tr -cd '0-9')"
        height="$(printf '%s' "${3:-}" | tr -cd '0-9')"
        maxval="$(printf '%s' "${4:-}" | tr -cd '0-9')"

        if [ "$magic" != "P6" ] || [ -z "$width" ] || [ -z "$height" ] || [ -z "$maxval" ]; then
            echo "[capture-qemu-fb-dump] invalid PPM header in $OUTPUT_PPM" >&2
            exit 1
        fi
        if [ "$width" -le 0 ] || [ "$height" -le 0 ] || [ "$maxval" -le 0 ]; then
            echo "[capture-qemu-fb-dump] non-positive PPM geometry/maxval in $OUTPUT_PPM" >&2
            exit 1
        fi

        cat > "$META_PATH" <<EOF
timestamp_utc=$timestamp_utc
ppm_path=$OUTPUT_PPM
qemu_log_path=$QEMU_LOG
latest_ppm_path=$LATEST_PPM_PATH
latest_meta_path=$LATEST_META_PATH
latest_qemu_log_path=$LATEST_QEMU_LOG_PATH
artifact_label=$ARTIFACT_LABEL
qmp_port=$QMP_PORT
boot_wait_secs=$BOOT_WAIT_SECS
qmp_key_sequence=$QMP_KEY_SEQUENCE
serial_input_sequence=$SERIAL_INPUT_SEQUENCE
size_bytes=$size_bytes
sha256=$sha256
magic=$magic
width=$width
height=$height
maxval=$maxval
EOF

        ln -sfn "$(basename "$OUTPUT_PPM")" "$LATEST_PPM_PATH"
        ln -sfn "$(basename "$META_PATH")" "$LATEST_META_PATH"
        ln -sfn "$(basename "$QEMU_LOG")" "$LATEST_QEMU_LOG_PATH"

        echo "[capture-qemu-fb-dump] wrote $OUTPUT_PPM"
        echo "[capture-qemu-fb-dump] wrote $META_PATH"
        echo "[capture-qemu-fb-dump] updated latest pointers in $ARTIFACT_DIR"
        echo "GUI_FB_DUMP_META path=$META_PATH sha256=$sha256 width=$width height=$height size_bytes=$size_bytes"
        exit 0
    fi
    sleep 0.1
done

echo "[capture-qemu-fb-dump] screendump artifact missing at $OUTPUT_PPM" >&2
exit 1
