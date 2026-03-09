#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <mode:base|replay|fuzz> <log_file>" >&2
    exit 2
fi

mode="$1"
log_file="$2"

if [ ! -f "$log_file" ]; then
    echo "[validate_reliability_json] missing log file: $log_file" >&2
    exit 2
fi

clean_log="$(mktemp)"
trap 'rm -f "$clean_log"' EXIT
tr -d '\r' < "$log_file" > "$clean_log"

awk -v mode="$mode" '
BEGIN {
    event_count = 0;
    trace_count = 0;
    meta_base = 0;
    case_proc = 0;
    case_cwd = 0;
    case_pipe = 0;
    summary_3 = 0;
    summary_1 = 0;
    replay_all = 0;
    replay_quick = 0;
    fuzz_1 = 0;
    fuzz_2 = 0;
    fuzz_3 = 0;
}
{
    if ($0 ~ /^\{"type":"event","seq":[0-9]+,"scenario":"[^"]+","action":"[^"]+","rc":-?[0-9]+\}$/) {
        event_count++;
    }
    if ($0 ~ /^\{"type":"trace_summary","seq":[0-9]+,"hash":[0-9]+\}$/) {
        trace_count++;
    }
    if ($0 == "{\"type\":\"meta\",\"seed\":1337,\"script\":\"all\"}") {
        meta_base++;
    }
    if ($0 == "{\"type\":\"case\",\"name\":\"proc_redirect_reap\",\"ok\":true,\"rc\":0}") {
        case_proc++;
    }
    if ($0 == "{\"type\":\"case\",\"name\":\"cwd_path_drift\",\"ok\":true,\"rc\":0}") {
        case_cwd++;
    }
    if ($0 == "{\"type\":\"case\",\"name\":\"pipe_close_order_parent_child\",\"ok\":true,\"rc\":0}") {
        case_pipe++;
    }
    if ($0 == "{\"type\":\"summary\",\"total\":3,\"failures\":0,\"ok\":true}") {
        summary_3++;
    }
    if ($0 == "{\"type\":\"summary\",\"total\":1,\"failures\":0,\"ok\":true}") {
        summary_1++;
    }
    if ($0 == "{\"type\":\"meta_ext\",\"replay\":\"all_seed1337\",\"fuzz_lite\":0}") {
        replay_all++;
    }
    if ($0 == "{\"type\":\"meta_ext\",\"replay\":\"quick_seed1337\",\"fuzz_lite\":0}") {
        replay_quick++;
    }
    if ($0 == "{\"type\":\"meta_ext\",\"replay\":\"-\",\"fuzz_lite\":1}") {
        fuzz_1++;
    }
    if ($0 == "{\"type\":\"meta_ext\",\"replay\":\"-\",\"fuzz_lite\":2}") {
        fuzz_2++;
    }
    if ($0 == "{\"type\":\"meta_ext\",\"replay\":\"-\",\"fuzz_lite\":3}") {
        fuzz_3++;
    }
}
END {
    if (mode == "base") {
        if (meta_base < 1 || case_proc < 1 || case_cwd < 1 || case_pipe < 1 || summary_3 < 1 || event_count < 1 || trace_count < 1) {
            exit 1;
        }
        exit 0;
    }
    if (mode == "replay") {
        if (replay_all < 1 || replay_quick < 1 || summary_3 < 1 || summary_1 < 1 || trace_count < 2 || event_count < 1) {
            exit 1;
        }
        exit 0;
    }
    if (mode == "fuzz") {
        if (fuzz_1 < 1 || fuzz_2 < 1 || fuzz_3 < 1 || summary_3 < 3 || trace_count < 3 || event_count < 3) {
            exit 1;
        }
        exit 0;
    }
    exit 2;
}
' "$clean_log" || {
    echo "[validate_reliability_json] validation failed (mode=$mode)" >&2
    exit 1
}
