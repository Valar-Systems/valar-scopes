# REDACT SECRETS AT WRITE TIME -- the control that would have prevented all of this.
#
# Dot-sourced by EVERY PowerShell recorder that writes a board's serial output to a file
# (bench-capture.ps1, bench-trace.ps1). Its Python twin is scripts/redact_line.py, used by
# fresh-boot-capture.py; scripts/check_redaction_vectors.py runs both over the same lines
# (scripts/redact-vectors.txt) and refuses any difference, so the two cannot drift.
# (docs/bssid-guard.md: bench-trace and fresh-boot-capture wrote raw lines until 2026-10-23.)
#
# Shipping firmware ran WiFiManager at DEV level, which printed the Wi-Fi password
# in the clear ("*wm:[4] Using Password: ..."). This recorder faithfully wrote it to
# a ledger, and two of those ledgers were committed to a PUBLIC repo, where they sat
# for weeks. The firmware no longer emits it -- but a capture script must not depend
# on the device being well-behaved, because the whole job of this file is to record
# whatever the device says, including from OLD images and sibling-edition builds that
# still carry the old level.
#
# Redacts rather than drops, so the ledger still shows the line occurred. Also covers
# the scan list, which is level 3 and geolocating: a set of co-visible SSIDs is what
# Wi-Fi positioning databases index, and those SSIDs belong to the neighbours, who
# did not consent to being in a public repo. And the BSSID (2026-10-23 ruling): a full
# access-point MAC is exactly what those databases key on, so "[WiFi] fast join: ...
# bssid=<MAC>" and the join-diag "bss <MAC>" lines are redacted the way the SSID is.
function Protect-Line([string]$line) {
    $line = $line -replace '(Using Password:\s*).*',        '${1}<redacted by bench-capture>'
    $line = $line -replace '(password["'':=\s]+)\S+',       '${1}<redacted>'
    # Three forms carry the neighbour list, and missing any one of them leaks the set:
    #   *wm:[3] AP:  -51 <ssid>
    #   *wm:[3] DUP AP: <ssid>
    #   *wm:[4] <div>...data-ssid='<ssid>'><ssid></a>...   (the rendered picker row)
    $line = $line -replace '(\*wm:\[\d\]\s+AP:\s+-?\d+\s+).*',  '${1}<ssid redacted>'
    $line = $line -replace '(\*wm:\[\d\]\s+DUP AP:\s*).*',      '${1}<ssid redacted>'
    $line = $line -replace '(\*wm:\[\d\]\s+)<div>.*',           '${1}<scan entry redacted>'
    # A full MAC after "bssid" or "bss" (any case, ":" or "-"). The firmware's own two-byte form,
    # bssid=..:..:..:..:AB:CD, is not a full MAC and is left alone.
    $line = $line -replace '\b(bssid|bss)([^0-9A-Fa-f]{0,20}?)([0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}', '${1}${2}<bssid redacted>'
    return $line
}
