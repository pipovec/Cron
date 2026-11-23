#!/usr/bin/env bash
#
# Nocny reťazec: kazdy krok zacne az po skonceni predchadzajuceho.
#
#   1. PlayersStatAll        denne okrem pondelka (historia za vcera; trva 1,5 - 3 h)
#   2. PlayersVehicleDelta   denne prirastky po tankoch (cita historiu z kroku 1)
#   3. PlayersWN8Recent      WN8 za 7/14/28 dni (cita delty z kroku 2)
#   4. PlayersVehicleStat    stav hracov po tankoch (active = hraci s historiou za vcera)
#   5. PlayersWN8v2          WN8 vsetkych hracov z API (trva ~1 h; od historie nezavisi)
#
# Krok, ktory zlyha, reťazec nezastavi - dalsie kroky pracuju s tym, co je v DB. Zlyhania sa
# zapisu do NightlyChain.txt. Druhy sucasny beh sa odmietne (flock), aby sa kroky neprekryvali.
#
# Cron:
#   15 02 * * * /home/deamon/projects/crons/scripts/nightly_chain.sh
#
# CHAIN_DRY_RUN=1 iba vypise, co by sa spustilo.

set -u

ROOT=/home/deamon/projects/crons
OUT=/home/deamon/outputs/C++
LOG=$OUT/NightlyChain.txt
LOCK=/tmp/nightly_chain.lock

exec 9>"$LOCK"
if ! flock -n 9; then
    echo "$(date '+%F %T') PRESKOCENE: predchadzajuci beh retazca este bezi" >> "$LOG"
    exit 1
fi

cd "$ROOT" || exit 1

log() { echo "$(date '+%F %T') $*" >> "$LOG"; }

# run <program> [argumenty]: vystup programu ide do $OUT/<program>.txt (ako doteraz v crontabe)
run() {
    local prog=$1
    shift
    if [ "${CHAIN_DRY_RUN:-0}" = "1" ]; then
        log "DRY: ./bin/$prog $*"
        return 0
    fi
    log "START $prog $*"
    "./bin/$prog" "$@" > "$OUT/$prog.txt" 2>&1
    local rc=$?
    log "KONIEC $prog rc=$rc"
    return $rc
}

log "=== retazec start ==="

# PlayersStatAll sa spustal Utorok-Nedela (cron 2-7); date +%u: 1 = pondelok
if [ "$(date +%u)" != "1" ]; then
    run PlayersStatAll
else
    log "PRESKOCENE PlayersStatAll (pondelok)"
fi

run PlayersVehicleDelta
run PlayersWN8Recent
run PlayersVehicleStat active
run PlayersWN8v2

log "=== retazec koniec ==="
