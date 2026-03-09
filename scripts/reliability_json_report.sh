#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 2 ]; then
    echo "usage: $0 <mode> <log_file> [--expect-replay-hash profile=hash ...]" >&2
    exit 2
fi

mode="$1"
log_file="$2"
shift 2

if [ ! -f "$log_file" ]; then
    echo "[reliability_json_report] missing log file: $log_file" >&2
    exit 2
fi

expected_pairs=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --expect-replay-hash)
            if [ "$#" -lt 2 ]; then
                echo "[reliability_json_report] missing value for --expect-replay-hash" >&2
                exit 2
            fi
            expected_pairs="${expected_pairs}${2}\n"
            shift 2
            ;;
        *)
            echo "[reliability_json_report] unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

clean_log="$(mktemp)"
trap 'rm -f "$clean_log"' EXIT
tr -d '\r' < "$log_file" > "$clean_log"

awk -v mode="$mode" -v expected_pairs="$expected_pairs" '
function parse_int_field(line, key,    pat, tmp) {
    pat = "\"" key "\":";
    if (match(line, pat "-?[0-9]+")) {
        tmp = substr(line, RSTART + length(pat), RLENGTH - length(pat));
        return tmp + 0;
    }
    return -2147483648;
}

function parse_str_field(line, key,    pat, rest, pos) {
    pat = "\"" key "\":\"";
    if (match(line, pat)) {
        rest = substr(line, RSTART + RLENGTH);
        pos = index(rest, "\"");
        if (pos > 0) {
            return substr(rest, 1, pos - 1);
        }
    }
    return "";
}

BEGIN {
    run_id = 0;
    current_profile = "-";
    total_events = 0;
    total_cases = 0;
    total_summaries = 0;
    total_traces = 0;
}

/^\{"type":"meta","seed":[0-9]+,"script":"[^"]+"\}$/ {
    run_id++;
    current_profile = "-";
    run_script[run_id] = parse_str_field($0, "script");
}

/^\{"type":"meta_ext","replay":"[^"]+","fuzz_lite":[0-9]+\}$/ {
    if (run_id == 0) {
        run_id++;
    }
    current_profile = parse_str_field($0, "replay");
    run_profile[run_id] = current_profile;
    run_fuzz[run_id] = parse_int_field($0, "fuzz_lite");
}

/^\{"type":"event","seq":[0-9]+,"scenario":"[^"]+","action":"[^"]+","rc":-?[0-9]+\}$/ {
    total_events++;
    s = parse_str_field($0, "scenario");
    scenario_seen[s] = 1;
    scenario_events[s]++;
}

/^\{"type":"case","name":"[^"]+","ok":(true|false),"rc":-?[0-9]+\}$/ {
    total_cases++;
    c = parse_str_field($0, "name");
    case_seen[c] = 1;
    case_pass[c] += ($0 ~ /"ok":true/);
    case_fail[c] += ($0 ~ /"ok":false/);
}

/^\{"type":"summary","total":[0-9]+,"failures":[0-9]+,"ok":(true|false)\}$/ {
    total_summaries++;
}

/^\{"type":"trace_summary","seq":[0-9]+,"hash":[0-9]+\}$/ {
    total_traces++;
    seq = parse_int_field($0, "seq");
    h = parse_int_field($0, "hash");
    trace_seq[run_id] = seq;
    trace_hash[run_id] = h;
    if (run_profile[run_id] != "" && run_profile[run_id] != "-") {
        replay_hash[run_profile[run_id]] = h;
        replay_seq[run_profile[run_id]] = seq;
    }
}

END {
    print "report: mode=" mode " runs=" run_id " events=" total_events " cases=" total_cases " summaries=" total_summaries " traces=" total_traces;

    for (s in scenario_seen) {
        print "report: scenario " s " events=" scenario_events[s];
    }
    for (c in case_seen) {
        print "report: case " c " pass=" case_pass[c] " fail=" case_fail[c];
    }
    for (i = 1; i <= run_id; i++) {
        prof = run_profile[i];
        if (prof == "") {
            prof = "-";
        }
        print "report: run#" i " script=" run_script[i] " replay=" prof " fuzz_lite=" run_fuzz[i] " trace_seq=" trace_seq[i] " trace_hash=" trace_hash[i];
    }

    if (expected_pairs != "") {
        n = split(expected_pairs, lines, "\n");
        for (i = 1; i <= n; i++) {
            if (lines[i] == "") {
                continue;
            }
            eq = index(lines[i], "=");
            if (eq <= 1) {
                print "[reliability_json_report] bad expected pair: " lines[i] > "/dev/stderr";
                exit 1;
            }
            p = substr(lines[i], 1, eq - 1);
            h = substr(lines[i], eq + 1) + 0;
            if (!(p in replay_hash)) {
                print "[reliability_json_report] missing replay trace hash for profile: " p > "/dev/stderr";
                exit 1;
            }
            if (replay_hash[p] != h) {
                print "[reliability_json_report] replay trace hash mismatch for " p ": expected=" h " actual=" replay_hash[p] > "/dev/stderr";
                exit 1;
            }
            print "report: replay-hash " p " ok hash=" replay_hash[p] " seq=" replay_seq[p];
        }
    }
}
' "$clean_log"
