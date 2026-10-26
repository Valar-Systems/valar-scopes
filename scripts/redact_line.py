"""The Python twin of scripts/redact-line.ps1's Protect-Line, for recorders written in Python
(fresh-boot-capture.py). The rules are the same, in the same order; scripts/check_redaction_vectors.py runs
both over scripts/redact-vectors.txt and refuses any difference (docs/bssid-guard.md).

PowerShell's -replace is case-insensitive, so every rule here is re.IGNORECASE.
"""
import re

_I = re.IGNORECASE
RULES = [
    (re.compile(r"(Using Password:\s*).*", _I), r"\1<redacted by bench-capture>"),
    (re.compile(r"(password[\"':=\s]+)\S+", _I), r"\1<redacted>"),
    (re.compile(r"(\*wm:\[\d\]\s+AP:\s+-?\d+\s+).*", _I), r"\1<ssid redacted>"),
    (re.compile(r"(\*wm:\[\d\]\s+DUP AP:\s*).*", _I), r"\1<ssid redacted>"),
    (re.compile(r"(\*wm:\[\d\]\s+)<div>.*", _I), r"\1<scan entry redacted>"),
    (re.compile(r"\b(bssid|bss)([^0-9A-Fa-f]{0,20}?)([0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}", _I), r"\1\2<bssid redacted>"),
]


def protect_line(line: str) -> str:
    for rx, rep in RULES:
        line = rx.sub(rep, line)
    return line
