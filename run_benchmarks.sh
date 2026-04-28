#!/usr/bin/env zsh
# =============================================================================
# run_benchmarks.sh  —  Mini-VCS real benchmark data generator
# No Python required. Requires: zsh, awk, dd, perl, du
#
# Usage:  zsh run_benchmarks.sh
# Run from the Mini_VCS folder (the one containing the mini_vcs binary)
# =============================================================================

MINI_VCS="./mini_vcs"
WORK_DIR="$(pwd)/bench_work"
OUT_DIR="$(pwd)/results"
RUNS=5

mkdir -p "$OUT_DIR"
REPORT="$OUT_DIR/benchmark_results.txt"
CSV="$OUT_DIR/raw_values.csv"
: > "$REPORT"
echo "strategy,corpus,metric,value" > "$CSV"

log()  { echo "$*" | tee -a "$REPORT"; }
sep()  { log ""; log "======================================================================"; log "$*"; log "======================================================================"; }
line() { log "----------------------------------------------------------------------"; }

if [ ! -f "$MINI_VCS" ]; then
    echo "ERROR: mini_vcs binary not found at $MINI_VCS"
    echo "Run this script from the Mini_VCS folder."
    exit 1
fi

# keep absolute path to binary so it works after cd
MINI_VCS_ABS="$(pwd)/mini_vcs"

# ---------- create a random text file of SIZE_KB kilobytes -------------------
make_file() {
    local path="$1"
    local size_kb="$2"
    local needed=$(( size_kb * 1024 ))
    # generate 4x data via urandom, strip non-printable, trim to exact size
    dd if=/dev/urandom bs=1024 count=$(( size_kb * 4 )) 2>/dev/null \
        | tr -dc 'a-zA-Z0-9 ' \
        | head -c "$needed" > "$path"
    # pad with 'a' if tr stripped too many bytes
    local actual
    actual=$(wc -c < "$path" | tr -d ' ')
    if [ "$actual" -lt "$needed" ]; then
        printf 'a%.0s' {1..$(( needed - actual ))} >> "$path" 2>/dev/null || true
        # fallback pad using dd
        dd if=/dev/zero bs=1 count=$(( needed - actual )) 2>/dev/null \
            | tr '\0' 'a' >> "$path"
    fi
}

# ---------- rewrite PCT% of files with new same-size content -----------------
modify_files() {
    local dir="$1"
    local pct="$2"
    [ "$pct" -eq 0 ] && return
    local all_files
    all_files=( "$dir"/file_*.txt )
    local total=${#all_files[@]}
    local n=$(( (total * pct + 99) / 100 ))
    # shuffle with awk
    local shuffled
    shuffled=( $(printf '%s\n' "${all_files[@]}" \
        | awk 'BEGIN{srand()} {print rand(), $0}' \
        | sort -n | awk '{print $2}') )
    local i=0
    while [ $i -lt $n ]; do
        local f="${shuffled[$((i+1))]}"
        if [ -f "$f" ]; then
            local sz
            sz=$(wc -c < "$f" | tr -d ' ')
            dd if=/dev/urandom bs=1024 count=$(( sz / 256 + 4 )) 2>/dev/null \
                | tr -dc 'a-zA-Z0-9 ' \
                | head -c "$sz" > "$f"
            # pad if needed
            local actual
            actual=$(wc -c < "$f" | tr -d ' ')
            if [ "$actual" -lt "$sz" ]; then
                dd if=/dev/zero bs=1 count=$(( sz - actual )) 2>/dev/null \
                    | tr '\0' 'b' >> "$f"
            fi
        fi
        i=$(( i + 1 ))
    done
}

# ---------- millisecond timestamp via perl (always on macOS) -----------------
now_ms() {
    perl -MTime::HiRes=time -e 'printf "%d\n", time()*1000'
}

# ---------- integer median via awk -------------------------------------------
median_of() {
    printf '%s\n' "$@" | sort -n | awk '
    BEGIN{n=0}
    {a[n++]=$1}
    END{
        if(n==0){print 0; exit}
        if(n%2==1) print a[int(n/2)]
        else print int((a[n/2-1]+a[n/2])/2)
    }'
}

# ---------- read one value from the CSV --------------------------------------
csv_get() {
    grep "^${1},${2},${3}," "$CSV" | tail -1 | awk -F',' '{print $4}'
}

# =============================================================================
# run_corpus TAG N_FILES AVG_KB N_COMMITS CHANGE_PCT STRAT
#
# NOTE: this function cd's into the repo dir and runs mini_vcs there.
#       Storage is measured with: du -sk .git/objects
#       which works because we ARE inside the repo when we call it.
# =============================================================================
run_corpus() {
    local tag=$1 nf=$2 kb=$3 nc=$4 pct=$5 strat=$6
    local repo="$WORK_DIR/${tag}_s${strat}"
    rm -rf "$repo"
    mkdir -p "$repo"

    # create initial files
    local i=1
    while [ $i -le $nf ]; do
        make_file "$repo/file_$(printf '%03d' $i).txt" "$kb"
        i=$(( i + 1 ))
    done

    # switch into repo and init
    cd "$repo"
    "$MINI_VCS_ABS" init --strategy "$strat" > /dev/null 2>&1

    STORAGE_EACH=()
    COMMIT_TIMES=()

    local c=1
    while [ $c -le $nc ]; do
        [ $c -gt 1 ] && modify_files "$repo" "$pct"

        "$MINI_VCS_ABS" add . > /dev/null 2>&1

        local t0 t1
        t0=$(now_ms)
        "$MINI_VCS_ABS" commit -m "c${c}" > /dev/null 2>&1
        t1=$(now_ms)

        COMMIT_TIMES+=( $(( t1 - t0 )) )

        # measure storage from INSIDE the repo — .git/objects is right here
        local kb_used
        kb_used=$(du -sk .git/objects 2>/dev/null | awk '{print $1}')
        [ -z "$kb_used" ] && kb_used=0
        STORAGE_EACH+=( "$kb_used" )

        c=$(( c + 1 ))
    done

    # checkout timing
    "$MINI_VCS_ABS" checkout -b bench_b > /dev/null 2>&1
    local t0 t1
    t0=$(now_ms)
    "$MINI_VCS_ABS" checkout master > /dev/null 2>&1
    t1=$(now_ms)
    CHECKOUT_MS=$(( t1 - t0 ))

    cd "$WORK_DIR/.." > /dev/null 2>&1
}

# =============================================================================
# MAIN
# =============================================================================
sep "Mini-VCS Benchmark"
log "Binary : $MINI_VCS_ABS  ($(du -sh $MINI_VCS_ABS 2>/dev/null | awk '{print $1}'))"
log "Date   : $(date)"
log "Host   : $(uname -n) / $(uname -m)"

for strat in 1 2 3; do
    case $strat in
        1) sname="FULL_COPY"      ;;
        2) sname="DELTA_METADATA" ;;
        3) sname="DELTA_HASH"     ;;
    esac

    sep "STRATEGY $strat — $sname"

    # ── PRIMARY: 50 files, 4 KB, 10 commits, 20% change ─────────────────────
    log ">>> Primary benchmark: 50 files x 4 KB, 10 commits, 20% change"
    log "    $RUNS timing passes for stable median..."

    local_c1_times=()
    local_c10_times=()
    local_co_times=()

    local r=1
    while [ $r -le $RUNS ]; do
        run_corpus "sm_r${r}" 50 4 10 20 $strat
        local_c1_times+=( ${COMMIT_TIMES[1]} )
        local_c10_times+=( ${COMMIT_TIMES[10]} )
        local_co_times+=( $CHECKOUT_MS )
        r=$(( r + 1 ))
    done

    med_c1=$(median_of "${local_c1_times[@]}")
    med_c10=$(median_of "${local_c10_times[@]}")
    med_co=$(median_of "${local_co_times[@]}")

    # clean run for storage numbers
    run_corpus "sm_final" 50 4 10 20 $strat
    stor_c1=${STORAGE_EACH[1]}
    stor_c5=${STORAGE_EACH[5]}
    stor_c10=${STORAGE_EACH[10]}

    log "  Storage @ C1  : ${stor_c1} KB"
    log "  Storage @ C5  : ${stor_c5} KB"
    log "  Storage @ C10 : ${stor_c10} KB"
    log "  Median commit time (C1)  : ${med_c1} ms"
    log "  Median commit time (C10) : ${med_c10} ms"
    log "  Median checkout time     : ${med_co} ms"

    echo "${strat},primary,stor_c1,${stor_c1}"    >> "$CSV"
    echo "${strat},primary,stor_c5,${stor_c5}"    >> "$CSV"
    echo "${strat},primary,stor_c10,${stor_c10}"  >> "$CSV"
    echo "${strat},primary,commit_ms,${med_c10}"  >> "$CSV"
    echo "${strat},primary,checkout_ms,${med_co}" >> "$CSV"

    # ── ZERO-CHANGE ──────────────────────────────────────────────────────────
    log ""
    log ">>> Zero-change: 50 files, 10 commits, 0% change (best case)"
    run_corpus "zero" 50 4 10 0 $strat
    log "  Storage @ C10 : ${STORAGE_EACH[10]} KB"
    echo "${strat},zero,stor_c10,${STORAGE_EACH[10]}" >> "$CSV"

    # ── ALL-CHANGE ───────────────────────────────────────────────────────────
    log ""
    log ">>> All-change: 50 files, 10 commits, 100% change (worst case)"
    run_corpus "allch" 50 4 10 100 $strat
    log "  Storage @ C10 : ${STORAGE_EACH[10]} KB"
    echo "${strat},allch,stor_c10,${STORAGE_EACH[10]}" >> "$CSV"

    # ── MEDIUM ───────────────────────────────────────────────────────────────
    log ""
    log ">>> Medium: 200 files x 8 KB, 15 commits, 10% change"
    run_corpus "med" 200 8 15 10 $strat
    log "  Storage @ C15 : ${STORAGE_EACH[15]} KB"
    echo "${strat},medium,stor_c15,${STORAGE_EACH[15]}" >> "$CSV"

    # ── TINY ─────────────────────────────────────────────────────────────────
    log ""
    log ">>> Tiny: 10 files x 2 KB, 5 commits, 40% change"
    run_corpus "tiny" 10 2 5 40 $strat
    log "  Storage @ C5  : ${STORAGE_EACH[5]} KB"
    echo "${strat},tiny,stor_c5,${STORAGE_EACH[5]}" >> "$CSV"
done

rm -rf "$WORK_DIR"

# =============================================================================
# FINAL SUMMARY
# =============================================================================
sep "FINAL SUMMARY — paste these numbers into your LaTeX report"

S1_C1=$(csv_get 1 primary stor_c1);   S1_C5=$(csv_get 1 primary stor_c5);   S1_C10=$(csv_get 1 primary stor_c10)
S1_MS=$(csv_get 1 primary commit_ms); S1_CO=$(csv_get 1 primary checkout_ms)
S1_Z10=$(csv_get 1 zero stor_c10);    S1_A10=$(csv_get 1 allch stor_c10);    S1_M15=$(csv_get 1 medium stor_c15)

S2_C1=$(csv_get 2 primary stor_c1);   S2_C5=$(csv_get 2 primary stor_c5);   S2_C10=$(csv_get 2 primary stor_c10)
S2_MS=$(csv_get 2 primary commit_ms); S2_CO=$(csv_get 2 primary checkout_ms)
S2_Z10=$(csv_get 2 zero stor_c10);    S2_A10=$(csv_get 2 allch stor_c10);    S2_M15=$(csv_get 2 medium stor_c15)

S3_C1=$(csv_get 3 primary stor_c1);   S3_C5=$(csv_get 3 primary stor_c5);   S3_C10=$(csv_get 3 primary stor_c10)
S3_MS=$(csv_get 3 primary commit_ms); S3_CO=$(csv_get 3 primary checkout_ms)
S3_Z10=$(csv_get 3 zero stor_c10);    S3_A10=$(csv_get 3 allch stor_c10);    S3_M15=$(csv_get 3 medium stor_c15)

RED2=$(awk "BEGIN{if($S1_C10>0) printf \"%d\",(1-$S2_C10/$S1_C10)*100; else print \"n/a\"}")
RED3=$(awk "BEGIN{if($S1_C10>0) printf \"%d\",(1-$S3_C10/$S1_C10)*100; else print \"n/a\"}")

DM_REL=$(awk "BEGIN{
    if($S1_MS>0){r=$S2_MS/$S1_MS; d=(1-r)*100
    if(d>=0) printf \"%.2fx (%d%% faster)\",r,d
    else printf \"%.2fx (%d%% slower)\",r,-d}
    else print \"n/a\"}")

DH_REL=$(awk "BEGIN{
    if($S1_MS>0){r=$S3_MS/$S1_MS; d=(1-r)*100
    if(d>=0) printf \"%.2fx (%d%% faster)\",r,d
    else printf \"%.2fx (%d%% slower)\",r,-d}
    else print \"n/a\"}")

BINSIZE=$(du -sh "$MINI_VCS_ABS" 2>/dev/null | awk '{print $1}')

log ""
log "-------- Table: tab:storage (50-file, 10-commit, 20% change) ---------"
log ""
printf "%-18s  %8s  %8s  %8s  %s\n" "Strategy" "@C1(KB)" "@C5(KB)" "@C10(KB)" "Reduction" | tee -a "$REPORT"
line
printf "%-18s  %8s  %8s  %8s  %s\n" "FULL_COPY"      "$S1_C1" "$S1_C5" "$S1_C10" "--- (baseline)"  | tee -a "$REPORT"
printf "%-18s  %8s  %8s  %8s  %s\n" "DELTA_METADATA" "$S2_C1" "$S2_C5" "$S2_C10" "~${RED2}%"        | tee -a "$REPORT"
printf "%-18s  %8s  %8s  %8s  %s\n" "DELTA_HASH"     "$S3_C1" "$S3_C5" "$S3_C10" "~${RED3}%"        | tee -a "$REPORT"

log ""
log "-------- Table: tab:perf (performance benchmarks) --------------------"
log ""
printf "%-42s  %-16s  %-16s  %-16s\n" "Metric" "FULL_COPY" "DELTA_META" "DELTA_HASH" | tee -a "$REPORT"
line
printf "%-42s  %-16s  %-16s  %-16s\n" "Avg commit time (50 files, 20% change)" "${S1_MS} ms"   "${S2_MS} ms"   "${S3_MS} ms"   | tee -a "$REPORT"
printf "%-42s  %-16s  %-16s  %-16s\n" "Relative to FULL_COPY"                  "1.0x baseline" "$DM_REL"       "$DH_REL"       | tee -a "$REPORT"
printf "%-42s  %-16s  %-16s  %-16s\n" "Storage @ C10 (50-file, 20% change)"    "${S1_C10} KB"  "${S2_C10} KB"  "${S3_C10} KB"  | tee -a "$REPORT"
printf "%-42s  %-16s  %-16s  %-16s\n" "Storage @ C10 (zero-change best case)"  "${S1_Z10} KB"  "${S2_Z10} KB"  "${S3_Z10} KB"  | tee -a "$REPORT"
printf "%-42s  %-16s  %-16s  %-16s\n" "Storage @ C10 (all-change worst case)"  "${S1_A10} KB"  "${S2_A10} KB"  "${S3_A10} KB"  | tee -a "$REPORT"
printf "%-42s  %-16s  %-16s  %-16s\n" "Storage @ C15 (200-file medium corpus)" "${S1_M15} KB"  "${S2_M15} KB"  "${S3_M15} KB"  | tee -a "$REPORT"
printf "%-42s  %-16s  %-16s  %-16s\n" "Checkout time (50 files)"               "${S1_CO} ms"   "${S2_CO} ms"   "${S3_CO} ms"   | tee -a "$REPORT"
printf "%-42s  %-16s  %-16s  %-16s\n" "Binary size"                            "$BINSIZE"      "same"          "same"          | tee -a "$REPORT"

log ""
log "Raw CSV: $CSV"
sep "Done. Full results saved to: $REPORT"
