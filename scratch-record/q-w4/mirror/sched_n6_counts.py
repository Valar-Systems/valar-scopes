# SCRATCH, read-only: one mirror round's counts for sched_n6.sh. Prints "m f r o e":
#   m  ftp2 records in retry-fetched.json now;  f / r / o  the round's "fetched" / "not fetched: " / other attempt lines
#   (the closing "retry: ..." line is not an attempt);  e  the round's stderr bytes.
#   python -X utf8 -I mirror/sched_n6_counts.py <cache> <round .out> <round .err>
import json, os, sys

cache, out, err = sys.argv[1:4]
rec = json.load(open(os.path.join(cache, "retry-fetched.json"), encoding="utf-8"))
m = sum(1 for k in rec if k.startswith("https://ftp2.census.gov/"))
lines = [l.rstrip("\n") for l in open(out, encoding="utf-8") if l.strip() and not l.startswith("retry: ")]
f = sum(1 for l in lines if l.endswith(": fetched"))
r = sum(1 for l in lines if ": not fetched: " in l)
print(m, f, r, len(lines) - f - r, os.path.getsize(err))
