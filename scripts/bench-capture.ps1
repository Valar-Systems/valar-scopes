# Bench serial capture: read the board directly and append to a dated ledger.
# Run DETACHED (Start-Process -WindowStyle Hidden) so it survives the Claude
# session; kill via the PID in bench-logs\capture.pid.
#
#   Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','c:\Github\Blipscope\scripts\bench-capture.ps1'
#
# WHY THIS IS NOT `pio device monitor` ANY MORE (rewritten 2026-07-31).
# The old version piped `pio device monitor` through
# `ForEach-Object { Add-Content }`. It silently stopped writing three separate
# times in one day -- the loop process alive, the monitor attached and holding
# the port, the device provably still emitting (verified by reading the port
# directly), and not one line landing in the ledger. Every one of those misses
# cost a verification window. It also left orphaned python/platformio children
# holding COM118 after a kill, which then blocked flashing.
#
# Reading System.IO.Ports directly removes the whole chain: no python, no
# miniterm, no PowerShell pipeline, no child process to orphan. One held
# StreamWriter with AutoFlush replaces an open/append/close per line.
#
# ONE LEDGER PER RUN, dated. The old shared 11 MB file made every grep slow and
# made "is it still writing?" hard to answer at a glance.
#
# NOT committed to the repo on purpose (bench tooling, machine-specific port).

# WHAT RESETS THIS BOARD, PRECISELY (corrected 2026-08-05).
# `pio device monitor` RESETS IT ON EXIT -- miniterm drops DTR/RTS as it tears
# down, and on this board that is a reset. Cost a 3 h 44 m soak on 2026-08-04,
# when swapping a console for this recorder brought the board back up=00:00:00.
#
# THIS SCRIPT DOES NOT. Verified the same day on the second board: it attached
# to a device that had been up 17.1 h and the uptime and in-RAM counters were
# untouched. The DtrEnable/RtsEnable = $false below is what buys that, and it
# holds on attach as well as during the session.
#
# So the rule is narrower than it first looked, and it is about the OTHER
# client: never leave a pio monitor attached to anything whose uptime matters,
# because ending it ends the run. Attaching or detaching THIS recorder mid-run
# is safe. Prefer starting it first anyway -- not because attaching costs
# anything, but because whatever happened before it attached has no per-event
# detail, only whatever totals the firmware keeps in RAM.

# FLASHING WHILE THIS RUNS: kill it and WAIT for the port to actually open before
# invoking esptool. Killing the process returns immediately but Windows releases
# the COM handle a beat later, so an upload fired straight afterwards fails in
# ~10 s with a misleading "FAILED" that looks like a build error. Bit me twice.
#
#   Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" |
#     Where-Object { $_.CommandLine -like '*bench-capture*' } |
#     ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
#   for ($i=0; $i -lt 15; $i++) {
#     try { $sp = New-Object System.IO.Ports.SerialPort 'COM118',115200
#           $sp.Open(); $sp.Close(); $sp.Dispose(); break } catch { Start-Sleep 2 } }
#
# Two boards now run at once (soak on one, bench harness on the other), so the
# port and the ledger name are parameters and the pid file is per-label. The
# single hardcoded COM118 + single capture.pid could only ever record one board,
# and silently recorded the WRONG one the moment a second appeared.
param(
    [string]$Port  = 'COM118',
    [string]$Label = 's3-128'
)

$stamp    = Get-Date -Format 'yyyy-MM-dd-HHmm'
$log      = "c:\Github\Blipscope\bench-logs\$Label-$stamp.log"
$fallback = $Port
$baud     = 115200

Set-Content -Path "c:\Github\Blipscope\bench-logs\capture-$Label.pid" -Value $PID

# Auto-detect each attempt: an ESP32-S3's native USB re-enumerates after a
# power-cycle and can come back on a different COM number.
function Find-BoardPort {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames()
    if ($ports -contains $fallback) { return $fallback }
    # NO "just take the first port" ANY MORE. That fallback existed when one
    # board was on the bench and re-enumeration after a power-cycle was the only
    # way to lose it. With two boards attached it does something far worse than
    # fail: it silently attaches this recorder to the OTHER board and writes a
    # ledger that looks perfectly healthy under the wrong label. Waiting for the
    # named port to come back is always recoverable; recording the wrong device
    # is not, because nothing in the output says so.
    return $null
}

function Write-Mark($sw, $text) {
    $sw.WriteLine("=== [capture] $text $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') pid=$PID ===")
}

# REDACT SECRETS AT WRITE TIME -- the control that would have prevented all of this.
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
# did not consent to being in a public repo.
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
    return $line
}

$sw = New-Object System.IO.StreamWriter($log, $true)
$sw.AutoFlush = $true          # the whole point: never buffer the evidence
Write-Mark $sw "started"

$failStreak = 0
while ($true) {
    $port = Find-BoardPort
    if (-not $port) { $failStreak++; Start-Sleep -Seconds ([Math]::Min(5 * $failStreak, 30)); continue }

    $sp = New-Object System.IO.Ports.SerialPort $port, $baud
    # ASSERT BOTH handshake lines. This is the opposite of what this line used to do,
    # and the old comment ("asserting DTR/RTS resets it") was simply wrong -- it was
    # written from the classic CP2102 auto-reset circuit and never checked against this
    # board, which is native USB-Serial/JTAG.
    #
    # Measured on COM119 (s3-128), 9 s of listening per combination, twice each -- set
    # before Open() and set after it, same result both ways:
    #
    #     DTR=true  RTS=true   -> no reset
    #     DTR=false RTS=false  -> RESET (ESP-ROM:esp32s3-... every time)
    #
    # So the previous setting reset the board on EVERY reattach. Because the script
    # reattaches whenever USB re-enumerates -- i.e. after every power cycle, which is
    # exactly when someone is watching the boot -- it rebooted the device mid-boot on
    # roughly half of observed loads and presented as a firmware fault. It cost three
    # separate measurements in one day: a thumb test, a first-run acceptance run, and a
    # splash observation.
    #
    # A capture that reboots the thing it is measuring is worse than none. That part of
    # the old comment was right; the code under it was not.
    $sp.DtrEnable = $true
    $sp.RtsEnable = $true
    $sp.ReadTimeout = 60000     # a quiet board still reports [health] every 30 s
    $t0 = Get-Date
    try {
        $sp.Open()
        Write-Mark $sw "attached port=$port"
        $failStreak = 0
        while ($sp.IsOpen) { $sw.WriteLine((Protect-Line $sp.ReadLine())) }
    } catch {
        $ran = [int]((Get-Date) - $t0).TotalSeconds
        # A read timeout means the DEVICE went quiet -- that is a finding, not a
        # capture failure, so it goes in the ledger rather than being retried away.
        Write-Mark $sw "detached after ${ran}s: $($_.Exception.GetType().Name) -- $($_.Exception.Message)"
        if ($ran -lt 10) { $failStreak++ }
    } finally {
        try { $sp.Close() } catch {}
        $sp.Dispose()
    }
    Start-Sleep -Seconds ([Math]::Min(5 * [Math]::Max($failStreak, 1), 30))
}
