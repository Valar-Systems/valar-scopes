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

$stamp    = Get-Date -Format 'yyyy-MM-dd-HHmm'
$log      = "c:\Github\Blipscope\bench-logs\s3-128-$stamp.log"
$fallback = 'COM118'
$baud     = 115200

Set-Content -Path 'c:\Github\Blipscope\bench-logs\capture.pid' -Value $PID

# Auto-detect each attempt: an ESP32-S3's native USB re-enumerates after a
# power-cycle and can come back on a different COM number.
function Find-BoardPort {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames()
    if ($ports -contains $fallback) { return $fallback }
    if ($ports.Count -gt 0) { return $ports[0] }
    return $null
}

function Write-Mark($sw, $text) {
    $sw.WriteLine("=== [capture] $text $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') pid=$PID ===")
}

$sw = New-Object System.IO.StreamWriter($log, $true)
$sw.AutoFlush = $true          # the whole point: never buffer the evidence
Write-Mark $sw "started"

$failStreak = 0
while ($true) {
    $port = Find-BoardPort
    if (-not $port) { $failStreak++; Start-Sleep -Seconds ([Math]::Min(5 * $failStreak, 30)); continue }

    $sp = New-Object System.IO.Ports.SerialPort $port, $baud
    # Leave the handshake lines alone: on this board asserting DTR/RTS resets it,
    # and a capture that reboots the thing it is measuring is worse than none.
    $sp.DtrEnable = $false
    $sp.RtsEnable = $false
    $sp.ReadTimeout = 60000     # a quiet board still reports [health] every 30 s
    $t0 = Get-Date
    try {
        $sp.Open()
        Write-Mark $sw "attached port=$port"
        $failStreak = 0
        while ($sp.IsOpen) { $sw.WriteLine($sp.ReadLine()) }
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
