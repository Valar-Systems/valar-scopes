import sys, re
a, b = open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read()
print(f"sizes {len(a)} / {len(b)}")
if len(a) != len(b):
    print("SIZE DIFFERS -> not a stamp-only difference"); sys.exit(1)
diff = [i for i in range(len(a)) if a[i] != b[i]]
runs, cur = [], None
for i in diff:
    if cur and i <= cur[1] + 8: cur[1] = i
    else:
        cur = [i, i]; runs.append(cur)
n = len(a); unexplained = 0; tally = {}
for s, e in runs:
    cnt = sum(1 for i in diff if s <= i <= e)
    if 0xB0 <= s and e < 0xB0 + 32: kind = "app_elf_sha256"
    elif s >= n - 33:   # imgcheck.py proved n-33 is the checksum byte and n-32.. the trailing SHA in these images
        kind = "image checksum + trailing SHA-256" if s == n - 33 else "trailing SHA-256"
    else:
        ctx_a = a[max(0, s - 12):e + 13]; ctx_b = b[max(0, s - 12):e + 13]
        txt = lambda c: re.sub(rb"[^ -~]", b".", c).decode()
        if re.search(rb"\d\d:\d\d:\d\d", ctx_a) and re.search(rb"\d\d:\d\d:\d\d", ctx_b): kind = f"compile-time string '{txt(ctx_a)}' -> '{txt(ctx_b)}'"
        else:
            kind = f"UNEXPLAINED '{txt(ctx_a)}' -> '{txt(ctx_b)}'"; unexplained += cnt
    tally[kind.split(" '")[0]] = tally.get(kind.split(" '")[0], 0) + cnt
    print(f"  0x{s:06x}-0x{e:06x}: {cnt} bytes, {kind}")
print(f"{len(diff)} differing bytes: " + ", ".join(f"{k} {v}" for k, v in tally.items()))
print("VERDICT:", "stamps only" if unexplained == 0 else f"{unexplained} UNEXPLAINED bytes -> STOP")
sys.exit(1 if unexplained else 0)
