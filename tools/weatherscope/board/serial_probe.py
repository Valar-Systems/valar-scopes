"""Item 2's M2 read (docs/weatherscope-stage3.md A9.42): the records of a board whose serial port nothing has opened
since the flash, fetched over Wi-Fi from /diag/wx. This script NEVER opens a serial port.

    python -I tools/weatherscope/board/serial_probe.py <board host> <empty out dir> <flash end, unix s> [<at, s>]

Waits until flash end + <at> (default 150) s, then GETs http://<host>/diag/wx. A connection error or a 503 is
recorded and retried at most twice, 5 s apart (every attempt is in fetch.json). Writes body.txt (the records) and
fetch.json. Prints no address: the host is the board's mDNS name.
"""
import json, os, sys, time, urllib.error, urllib.request


def main(argv):
    if len(argv) not in (4, 5):
        print(__doc__)
        return 2
    host, out, flash_end = argv[1], argv[2], float(argv[3])
    at = float(argv[4]) if len(argv) == 5 else 150.0
    if os.path.exists(out) and os.listdir(out):
        sys.exit(f"REFUSED: {out} is not empty")
    os.makedirs(out, exist_ok=True)
    wait = flash_end + at - time.time()
    print(f"waiting {max(0, wait):.0f} s (until flash end + {at:.0f} s); the serial port stays closed", flush=True)
    if wait > 0:
        time.sleep(wait)
    attempts = []
    body = None
    for k in range(3):
        t0 = time.time()
        try:
            resp = urllib.request.urlopen(f"http://{host}/diag/wx", timeout=10)
            body = resp.read()
            attempts.append({"k": k, "t": t0, "status": resp.status, "bytes": len(body),
                             "wait_ms": resp.headers.get("X-Weatherscope-Wait-Ms"),
                             "since_flash_s": round(t0 - flash_end, 1)})
            break
        except urllib.error.HTTPError as e:
            attempts.append({"k": k, "t": t0, "status": e.code, "error": e.read().decode(errors="replace")[:200]})
        except Exception as e:
            attempts.append({"k": k, "t": t0, "error": f"{type(e).__name__}"})
        time.sleep(5)
    json.dump(attempts, open(os.path.join(out, "fetch.json"), "w"), indent=1)
    for a in attempts:
        print(a, flush=True)
    if body is None:
        print("no records fetched: BLIND")
        return 1
    open(os.path.join(out, "body.txt"), "wb").write(body)
    print(f"{len(body)} bytes saved")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
