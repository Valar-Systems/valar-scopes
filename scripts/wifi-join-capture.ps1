# Capture a Wi-Fi JOIN FAILURE, on the plain shipping image, with the scan
# record that tells you which kind of failure it was.
#
# OBSERVATION ONLY. This changes nothing on the device and requires no special
# build: the disconnect reason is already printed by the shipping firmware's
# WiFi.onEvent handler (main.cpp:353). Run it against a stock v11 board.
#
# WHY BOTH HALVES. The reason code alone does not separate the two candidates
# behind a handshake timeout:
#
#   reason 204 with the AP advertising WPA3 or WPA2/WPA3 transition  -> PMF/WPA3
#   reason 204 with the AP advertising WPA2-PSK only                 -> weak-node
#                                                                       handshake loss
#
# The device cannot tell you which: the shipping image never logs
# WiFi.encryptionType() for a scanned entry, only the channel/RSSI of an AP it
# already joined. So the scan record comes from THIS LAPTOP's radio instead,
# which sees the same beacons and reports the advertised auth mode directly.
# Different instrument, same datum, and it keeps the device image stock.
#
# TIMING. WiFiManager is configured setConnectRetries(5) x setConnectTimeout(15),
# so one join phase is ~75 s, plus ~30 s of boot. The portal then holds for 180 s
# (setConfigPortalTimeout, armed only when credentials exist) before the device
# reboots and tries again. One full cycle is therefore ~4.8 minutes, and TWO
# cycles -- the minimum that distinguishes "failed" from "still trying" -- needs
# about ten. The default below is twelve.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\wifi-join-capture.ps1 `
#       -Port COM6 -Ssid "TheirNetworkName"
param(
    [Parameter(Mandatory=$true)][string]$Port,
    [Parameter(Mandatory=$true)][string]$Ssid,      # the network being joined
    [int]$Minutes = 12,
    [string]$OutDir = 'c:\Github\Blipscope\bench-logs'
)

$stamp = Get-Date -Format 'yyyy-MM-dd-HHmm'
$log   = Join-Path $OutDir ("wifi-join-{0}-{1}.log" -f ($Ssid -replace '[^\w-]','_'), $stamp)
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function W($t) { $line = "{0}Z {1}" -f (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss'), $t
                 $line | Tee-Object -FilePath $log -Append | Out-Null; Write-Host $line }

W "=== wifi-join-capture  port=$Port  ssid=$Ssid  minutes=$Minutes ==="

# ---- 1. THE SCAN RECORD, before the serial capture ------------------------
# Taken from this machine's radio. `mode=bssid` is what carries Authentication,
# Channel and the per-BSSID signal -- the plain `show networks` does not.
W "--- scan record for '$Ssid' (this laptop's radio) ---"
$raw = netsh wlan show networks mode=bssid 2>&1 | Out-String
$blocks = $raw -split '(?m)^SSID \d+ : '
$hit = $blocks | Where-Object { $_ -match "^\s*$([regex]::Escape($Ssid))\s*[\r\n]" }
if (-not $hit) {
    W "  NOT VISIBLE to this laptop. Note whether the laptop is on 5 GHz -- an SSID"
    W "  the laptop sees only on 5 GHz is one the board (2.4 GHz only) cannot see,"
    W "  which is outcome (a) NO_AP_FOUND before any auth question arises."
    W "  Full scan follows for the record:"
    ($raw -split "`n" | Where-Object { $_ -match '^SSID \d+|Authentication|Encryption|Channel|Signal|BSSID' }) |
        ForEach-Object { W ("    " + $_.Trim()) }
} else {
    foreach ($l in ($hit -split "`n")) {
        if ($l -match 'Authentication|Encryption|Channel|Signal|BSSID|Network type|Radio type' -or $l.Trim() -eq $Ssid) {
            W ("    " + $l.Trim())
        }
    }
}
W "--- end scan record ---"

# ---- 2. THE SERIAL CAPTURE ------------------------------------------------
# dtr/rts false: attaching must not reset a running board. If the board is mid
# boot the open can still reset it (rst:0x15) -- start this BEFORE powering the
# board, which is why the instructions say attach first, then plug in.
$sp = New-Object System.IO.Ports.SerialPort $Port,115200,'None',8,'One'
$sp.DtrEnable = $false; $sp.RtsEnable = $false; $sp.ReadTimeout = 2000
$deadline = (Get-Date).AddMinutes($Minutes)
$pending = ''
try {
    $sp.Open()
    W "serial attached; capturing until $($deadline.ToUniversalTime().ToString('HH:mm:ss'))Z"
    while ((Get-Date) -lt $deadline) {
        try { $pending += $sp.ReadExisting() } catch {}
        while ($pending -match "`n") {
            $i = $pending.IndexOf("`n")
            $line = $pending.Substring(0, $i).TrimEnd("`r")
            $pending = $pending.Substring($i + 1)
            if ($line.Trim() -ne '') { W $line }
        }
        Start-Sleep -Milliseconds 200
    }
} catch {
    W "CAPTURE ERROR: $($_.Exception.Message)"
} finally {
    try { $sp.Close() } catch {}
    $sp.Dispose()
}

W "=== capture complete ==="
W "--- disconnect reasons seen ---"
$reasons = Select-String -Path $log -Pattern 'DISCONNECTED\s+reason=(\d+)\s+\(([^)]+)\)' -AllMatches
if ($reasons) {
    $reasons.Matches | ForEach-Object { $_.Groups[1].Value + ' ' + $_.Groups[2].Value } |
        Group-Object | Sort-Object Count -Descending |
        ForEach-Object { W ("    x{0,-4} reason={1}" -f $_.Count, $_.Name) }
} else {
    W "    NONE -- if the board also never printed a build banner, the capture"
    W "    started too late or on the wrong port. That is a capture failure, not a result."
}
Write-Host ""
Write-Host "log: $log"
