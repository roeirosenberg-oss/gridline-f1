#!/usr/bin/env python3
"""Generate reference data for the Israel time-zone tests from the IANA tz
database (Python zoneinfo). Output: CSV lines

  T,<year>,<dst_start_utc>,<dst_end_utc>      exact DST transitions
  S,<utc_epoch>,<offset_seconds>               samples

The C++ test compares gl::israelOffsetSeconds() with these values.
"""
import sys
from datetime import datetime, timedelta, timezone
from zoneinfo import ZoneInfo

TZ = ZoneInfo("Asia/Jerusalem")
FIRST, LAST = 2013, 2045


def offset(ts: int) -> int:
    return int(datetime.fromtimestamp(ts, tz=timezone.utc).astimezone(TZ).utcoffset().total_seconds())


def transitions(year: int):
    # scan the year in 1-hour steps, then refine to the second
    start = int(datetime(year, 1, 1, tzinfo=timezone.utc).timestamp())
    end = int(datetime(year + 1, 1, 1, tzinfo=timezone.utc).timestamp())
    found = []
    prev = offset(start)
    t = start
    while t < end:
        nt = t + 3600
        o = offset(nt)
        if o != prev:
            lo, hi = t, nt
            while hi - lo > 1:
                mid = (lo + hi) // 2
                if offset(mid) == prev:
                    lo = mid
                else:
                    hi = mid
            found.append((hi, prev, o))
            prev = o
        t = nt
    return found


def main(out):
    out.write(f"# tzdata via Python zoneinfo, Asia/Jerusalem, {FIRST}-{LAST}\n")
    for y in range(FIRST, LAST + 1):
        tr = transitions(y)
        if len(tr) != 2 or tr[0][2] != 10800 or tr[1][2] != 7200:
            raise SystemExit(f"unexpected transitions in {y}: {tr}")
        out.write(f"T,{y},{tr[0][0]},{tr[1][0]}\n")
        # dense samples around each transition (every 15 min, +/- 6 h)
        for (ts, _, _) in tr:
            for k in range(-24, 25):
                s = ts + k * 900
                out.write(f"S,{s},{offset(s)}\n")
            for k in (-1, 0, 1):
                out.write(f"S,{ts + k},{offset(ts + k)}\n")
    # hourly samples for a few complete years
    for y in (2025, 2026, 2027):
        t = int(datetime(y, 1, 1, tzinfo=timezone.utc).timestamp())
        end = int(datetime(y + 1, 1, 1, tzinfo=timezone.utc).timestamp())
        while t < end:
            out.write(f"S,{t},{offset(t)}\n")
            t += 3600


if __name__ == "__main__":
    with open(sys.argv[1], "w") as f:
        main(f)
