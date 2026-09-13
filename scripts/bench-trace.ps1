# A serial trace that CANNOT reset the board it is tracing.
#
# THE CONTRADICTION THIS RESOLVES. Verifying "the join path is unchanged" by
# comparing a join trace against the stock image needs a recorder. The recorder
# was resetting the board. Both cannot be true, and working around it by simply
# not attaching would have left the trace comparison silently unperformed while
# still being claimed.
#
# THE PROBLEM WAS NEVER "recorders reset boards". It is narrower, and naming it
# correctly is what makes a fix possible:
#
#   * DTR/RTS: bench-capture.ps1 asserts BOTH (DtrEnable/RtsEnable = $true). That
#     combination does not reset a RUNNING board -- verified repeatedly -- so it
#     is not the culprit either.
#   * THE REOPEN IS. bench-capture is a re-attaching recorder: on any read error
#     it closes and reopens. A board that reboots drops its native-USB CDC, which
#     throws, which triggers a reopen -- and an open() landing inside the boot
#     window resets the chip (rst:0x15, and no DTR/RTS combination avoids it).
#     The reset causes another reboot, and the loop sustains itself.
#
# So the fix is not a different handshake, it is A SINGLE OPEN THAT IS NEVER
# REPEATED. This script opens once, holds the handle for the whole run, and if
# the read fails it STOPS and says so rather than reattaching. A gap in the trace
# is a fact about the board; a reattach is an intervention in it.
#
# Attach BEFORE powering the board, so the open cannot land in a boot window:
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\bench-trace.ps1 `
#       -Port COM6 -Label positive-control -Minutes 6
#   ... then plug the board in.
param(
    [Parameter(Mandatory=$true)][string]$Port,
    [Parameter(Mandatory=$true)][string]$Label,
    [int]$Minutes = 6,
    [string]$OutDir = 'c:\Github\Blipscope\bench-logs'
)

$stamp = Get-Date -Format 'yyyy-MM-dd-HHmm'
$log = Join-Path $OutDir ("trace-{0}-{1}.log" -f $Label, $stamp)
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function W($t) {
    $l = "{0}Z {1}" -f (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss'), $t
    Add-Content -Path $log -Value $l; Write-Host $l
}

# dtr/rts FALSE: does not reset a running board (documented in bench-capture.ps1's
# header and relied on throughout this project's one-shot reads).
$sp = New-Object System.IO.Ports.SerialPort $Port,115200,'None',8,'One'
$sp.DtrEnable = $false
$sp.RtsEnable = $false

W "=== bench-trace  port=$Port  label=$Label  minutes=$Minutes ==="
W "single open, no reattach: a read error ENDS this run rather than reopening"

$deadline = (Get-Date).AddMinutes($Minutes)
$pending = ''
try {
    $sp.Open()
    W "attached (single open)"
    while ((Get-Date) -lt $deadline) {
        try { $pending += $sp.ReadExisting() }
        catch {
            W "READ ERROR -- STOPPING (not reopening): $($_.Exception.Message)"
            break
        }
        while ($pending.Contains("`n")) {
            $i = $pending.IndexOf("`n")
            $line = $pending.Substring(0, $i).TrimEnd("`r")
            $pending = $pending.Substring($i + 1)
            if ($line.Trim() -ne '') { W $line }
        }
        Start-Sleep -Milliseconds 150
    }
} catch {
    W "OPEN FAILED: $($_.Exception.Message)"
} finally {
    try { if ($sp.IsOpen) { $sp.Close() } } catch {}
    $sp.Dispose()
}
W "=== end ==="
Write-Host ""
Write-Host "log: $log"
