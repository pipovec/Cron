#!/usr/bin/env python3
"""
Kontrola hracov z players_all s clan_id IS NULL oproti WoT API (/account/info/).

  - API vrati pre ucet null           -> ucet neexistuje -> kandidat na zmazanie
  - API vrati objekt                  -> ucet existuje   -> ostava (vypise sa, ma sa mu dopocitat clan_id)
  - odpoved chyba / ucet bez odpovede -> nic sa nerobi (nikdy sa nemaze na zaklade chyby)

Predvolene je to SUCHY BEH (nic nemeni). Zmazanie az s APPLY=1:
    APPLY=1 scripts/check_null_clan_players.py

Dopocitanie clan_id a nickname pre existujuce ucty (API clan_id null -> 0) az s FILL=1:
    FILL=1 scripts/check_null_clan_players.py
Nick sa najprv uvolni u inych uctov (historia do players_nick, nickname = NULL), rovnako ako
lib/PlayerNick.cpp, aby sa nevytvorili duplicity. Meni sa len riadok, ktory ma stale clan_id IS NULL.

Zmazu sa len ucty, ktore API potvrdilo ako neexistujuce A zaroven maju v DB stale clan_id IS NULL.
Zmazanie ide cez DELETE FROM players_all (rovnako ako delete_dead_players.sh), po davkach.

Prostredie: PSQL_CONN (predvolene "host=localhost port=5432 dbname=clan user=deamon"),
            WOT_APP_ID (predvolene sa precita z lib/SendCurl.h).
"""
import json
import os
import re
import subprocess
import sys
import time
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONN = os.environ.get("PSQL_CONN", "host=localhost port=5432 dbname=clan user=deamon")
APPLY = os.environ.get("APPLY", "0") == "1"
FILL = os.environ.get("FILL", "0") == "1"
API = "https://api.worldoftanks.eu/wot/account/info/"


def app_id():
    if os.environ.get("WOT_APP_ID"):
        return os.environ["WOT_APP_ID"]
    # WG_APPLICATION_ID z .env projektu (rovnako ako C++ programy)
    try:
        with open(os.path.join(ROOT, ".env"), encoding="utf-8") as f:
            for line in f:
                m = re.match(r'\s*(?:export\s+)?WG_APPLICATION_ID\s*=\s*["\']?([^"\'\s]+)', line)
                if m:
                    return m.group(1)
    except OSError:
        pass
    sys.exit("Nenasiel som WG_APPLICATION_ID v .env (alebo nastav WOT_APP_ID)")


def psql(sql):
    r = subprocess.run(["psql", CONN, "-AtX", "-v", "ON_ERROR_STOP=1", "-c", sql],
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("psql chyba: " + r.stderr.strip())
    return r.stdout.strip()


def fetch(ids, key):
    q = urllib.parse.urlencode({"application_id": key, "fields": "nickname,clan_id",
                                "account_id": ",".join(map(str, ids))})
    for attempt in range(5):
        try:
            with urllib.request.urlopen(API + "?" + q, timeout=30) as resp:
                js = json.load(resp)
            if js.get("status") == "ok" and isinstance(js.get("data"), dict):
                return js["data"]
            print("  neplatna odpoved:", str(js)[:200], file=sys.stderr)
        except Exception as e:  # sietova chyba, 504 a pod.
            print("  API chyba:", e, file=sys.stderr)
        time.sleep(10 * (attempt + 1))
    return None


def lit(v):
    return "NULL" if v is None else "'" + str(v).replace("'", "''") + "'"


def fill(alive, info):
    todo = [a for a in alive if info[a][1]]  # bez nicku z API nemame co zapisat
    print(f"Dopocitanie clan_id/nickname: {len(todo)} uctov (bez nicku v API: {len(alive) - len(todo)})")
    for a in todo[:5]:
        print("  priklad:", a, info[a])
    done = 0
    for i in range(0, len(todo), 100):
        sql = ["BEGIN;"]
        for a in todo[i:i + 100]:
            clan, nick = info[a]
            sql.append(
                f"WITH freed AS (UPDATE players_all SET nickname = NULL WHERE nickname = {lit(nick)} "
                f"AND account_id <> {a} RETURNING account_id) "
                f"INSERT INTO players_nick (account_id, nick) SELECT account_id, {lit(nick)} FROM freed;")
            sql.append(f"UPDATE players_all SET clan_id = {int(clan)}, nickname = {lit(nick)} "
                       f"WHERE account_id = {a} AND clan_id IS NULL;")
        sql.append("COMMIT;" if APPLY else "ROLLBACK;")
        r = subprocess.run(["psql", CONN, "-AtXq", "-v", "ON_ERROR_STOP=1"], input="\n".join(sql),
                           capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit("psql chyba (davka vratena): " + r.stderr.strip())
        done += len(todo[i:i + 100])
    print(f"{'Zapisanych' if APPLY else 'Suchy beh - overenych (ROLLBACK)'}: {done}")


def main():
    key = app_id()
    rows = psql("SELECT account_id FROM players_all WHERE clan_id IS NULL ORDER BY account_id")
    ids = [int(x) for x in rows.splitlines() if x]
    print(f"Hracov s clan_id IS NULL: {len(ids)}  (rezim: {'APPLY' if APPLY else 'SUCHY BEH'})")

    dead, alive, unknown = [], [], []
    info = {}  # account_id -> (clan_id, nickname)
    for i in range(0, len(ids), 100):
        batch = ids[i:i + 100]
        data = fetch(batch, key)
        if data is None:
            unknown += batch
            continue
        for a in batch:
            if str(a) not in data:
                unknown.append(a)
            elif data[str(a)] is None:
                dead.append(a)
            else:
                alive.append(a)
                d = data[str(a)]
                info[a] = (d.get("clan_id") or 0, d.get("nickname"))
        time.sleep(0.3)

    print(f"Neexistuju (API null): {len(dead)}")
    print(f"Existuju:              {len(alive)}")
    print(f"Neoverene (chyba):     {len(unknown)}")
    if alive:
        print("  existujuce (prvych 10):", alive[:10])
    if unknown:
        print("  neoverene (prvych 10):", unknown[:10])

    if FILL:
        fill(alive, info)
    if not APPLY:
        print("Suchy beh - nic sa nezmenilo. Na zapis spusti s APPLY=1 (FILL=1 doplni clan_id/nickname, mazanie je bez FILL).")
        return
    deleted = 0
    for i in range(0, len(dead), 100):
        b = ",".join(map(str, dead[i:i + 100]))
        out = psql("WITH d AS (DELETE FROM players_all WHERE clan_id IS NULL "
                   f"AND account_id IN ({b}) RETURNING 1) SELECT count(*) FROM d")
        deleted += int(out or 0)
    print(f"Zmazanych z players_all: {deleted}")


if __name__ == "__main__":
    main()
