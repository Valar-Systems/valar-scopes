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
#     is not the culprit either. THE INVERSE IS NOT SAFE, and this script used to
#     get that backwards; see the handshake note at the SerialPort below.
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
    [double]$Minutes = 6,
    [string]$OutDir = 'c:\Github\Blipscope\bench-logs',
    # THE SEAM THAT FORCES THE FAILING BRANCH. Once the handshake is correct the
    # self-reset detector becomes unreachable in normal use -- and a guard that
    # cannot be made to fire is a guard nobody can check. This switch restores the
    # old, wrong handshake so the detector can be rehearsed red on demand.
    # It is for proving the instrument. Never use it to take a measurement.
    [switch]$RehearseUnsafeHandshake
)

$stamp = Get-Date -Format 'yyyy-MM-dd-HHmm'
$log = Join-Path $OutDir ("trace-{0}-{1}.log" -f $Label, $stamp)
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function W($t) {
    $l = "{0}Z {1}" -f (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss'), $t
    Add-Content -Path $log -Value $l; Write-Host $l
}

# ---- HANDSHAKE: ASSERT BOTH, and read the two notes below before "fixing" it ---
#
# WHAT IS ESTABLISHED (documentary, re-checkable by reading the other file):
# this line used to read "dtr/rts FALSE: does not reset a running board (documented
# in bench-capture.ps1's header)". bench-capture.ps1's header documents the
# OPPOSITE, and always did -- a measurement on COM119, two combinations, twice
# each, set before Open() and after it:
#
#     DTR=true  RTS=true   -> no reset
#     DTR=false RTS=false  -> RESET (ESP-ROM:esp32s3-... every time)
#
# So this script cited a source as saying the reverse of what it says. That is the
# failure this repo calls "a justification is a claim about code you are not
# editing": the sentence asserted a property of ANOTHER file, nothing re-checked
# it, and it stayed false indefinitely because bench-capture's own behaviour keeps
# being correct while bench-trace is the thing that is wrong. We now assert both
# lines, matching the only combination anyone has actually measured as safe.
#
# WHAT IS **NOT** ESTABLISHED -- and this is here so the next person does not
# inherit a confident story the way this file just did:
# on 2026-09-13 three boards each logged rst:0x15 (USB_UART_CHIP_RESET) in the same
# second as a single open with DTR/RTS false. rst:0x15 is host-caused -- the
# firmware's own self-heal reboot is rst:0xc (RTC_SW_CPU_RST) and carries the line
# "restarting to retry saved credentials" -- so those resets were the host's, not
# the board's. BUT a deliberate rehearsal minutes later, same script, same bad
# handshake, on a board in this same cluster, did NOT reset it; nor did the good
# handshake on a cycling board. THE HANDSHAKE IS THEREFORE NOT PROVEN TO BE THE
# CAUSE of that event, and a sabotage that does not change the result did not apply.
#
# LEADING HYPOTHESIS, UNTESTED: the reset follows the FIRST open after the USB
# device enumerates, not the handshake state. It fits every observation so far
# (all three boards were freshly plugged in; every later open had been preceded by
# one). THE DISCRIMINATING TEST, when someone has a board to spare: unplug it,
# plug it back in, open ONCE, and see whether rst:0x15 appears. Until that is run,
# treat the first read after a replug as the dangerous one regardless of handshake.
$sp = New-Object System.IO.Ports.SerialPort $Port,115200,'None',8,'One'
if ($RehearseUnsafeHandshake) {
    Write-Host "REHEARSAL: using the known-bad handshake on purpose. Expect a reset."
    $sp.DtrEnable = $false
    $sp.RtsEnable = $false
} else {
    $sp.DtrEnable = $true
    $sp.RtsEnable = $true
}

W "=== bench-trace  port=$Port  label=$Label  minutes=$Minutes ==="
W "single open, no reattach: a read error ENDS this run rather than reopening"

$deadline = (Get-Date).AddMinutes($Minutes)
$pending = ''
$selfReset = $false
# A comment saying "assert both handshake lines" is the weak form of this fix: it
# asks the next person to have read it. THIS is the form that fires without them.
# A host-caused reset names itself in the boot header (rst:0x15 USB_UART_CHIP_RESET),
# so a reset landing within a few seconds of our own open is us, not the board --
# and a trace that begins by rebooting its subject is measuring the instrument.
$attachedAt = $null
$SELF_RESET_WINDOW_SEC = 5
try {
    $sp.Open()
    $attachedAt = Get-Date
    W "attached (single open, DTR/RTS asserted)"
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
            if (-not $selfReset -and $line -match 'rst:0x15|USB_UART_CHIP_RESET') {
                $since = ((Get-Date) - $attachedAt).TotalSeconds
                if ($since -le $SELF_RESET_WINDOW_SEC) {
                    $selfReset = $true
                    W ""
                    W "!!! SELF-INFLICTED RESET -- THIS TRACE IS CONTAMINATED !!!"
                    W "!!! rst:0x15 (host-caused) ${since}s after our own open."
                    W "!!! The board did not do this; we did. Anything measured from"
                    W "!!! here -- uptime, cycle count, scan results, retry timing --"
                    W "!!! is about a boot THIS SCRIPT caused. Discard and re-run."
                    W ""
                }
            }
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
if ($selfReset) {
    W "VERDICT: CONTAMINATED -- this run reset its own subject. Do not report it."
} else {
    W "VERDICT: clean -- no host-caused reset within ${SELF_RESET_WINDOW_SEC}s of attach."
}
Write-Host ""
Write-Host "log: $log"
# Non-zero so a caller that chains off this cannot quietly consume a bad trace.
# The clean path exits 0 EXPLICITLY: dot-sourced or called with &, a script that
# simply runs off the end leaves $LASTEXITCODE unset, and an unset exit code is
# indistinguishable from success to every caller that checks it.
if ($selfReset) { exit 3 } else { exit 0 }
