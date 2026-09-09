# Flash a bench board and re-attach the serial recorder -- IN THAT ORDER, AND
# ONLY IF THE FLASH SUCCEEDED.
#
# WHY THIS EXISTS (2026-09-09). The sequence was a hand-run procedure: kill the
# capture, upload, start the capture again. The third step was unconditional, and
# that is the whole defect. On 2026-09-08 an upload failed with the port still
# held; the watcher was launched anyway, took the port, and the SECOND attempt
# failed for a reason the first had created. The board ended up wedged for the
# night, and the recovery was an accidental one -- an overnight computer restart
# power-cycling USB.
#
# So the failure is not "a flash can fail". It is that a failed flash was
# followed by an action that made the next attempt worse, because nothing was
# reading the exit status. bench-capture.ps1 has documented the kill-and-wait
# half of this since 2026-08 ("Bit me twice"); the half after the upload was
# never written down, and prose would not have run anyway.
#
# THE RULES THIS ENCODES, all of them from CLAUDE.md and all of them earned:
#
#   * READ THE EXIT CODE, NOT THE OUTPUT. pio prints plenty of cheerful text
#     above an error, and "a failed deploy and an ineffective change produce the
#     same observation".
#   * DO NOT FILTER A COMMAND YOU ARE TESTING FOR FAILURE. The upload runs bare
#     and is TEE'd, never piped into a grep.
#   * POLL FOR THE HANDLE, DO NOT SLEEP. Killing the recorder returns before
#     Windows releases the COM handle.
#   * A FAILED FLASH LEAVES THE PORT FREE. No watcher, non-zero exit, and the
#     board is left in the state most likely to accept a retry.
#
# USAGE
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\bench-flash.ps1 `
#       -PioEnv blipscope-s3-128-quiethour -Port COM119 -Label b3-com119
#
#   -NoCapture   flash only; do not re-attach the recorder
#
# THE QUIET HOUR IS NOT A PARAMETER HERE, deliberately. It lives in the env's
# build_flags, where a `pio run` and this script agree by construction. A
# --project-option would have to RESTATE the whole build_flags list (it replaces
# rather than appends), and a mis-stated one silently drops the backend URL --
# the -U/-D trap in platformio.ini's [cloud] note. An untested second path in a
# script written to close a second-path defect is not a trade worth making.
param(
    [Parameter(Mandatory=$true)][string]$PioEnv,
    [Parameter(Mandatory=$true)][string]$Port,
    [Parameter(Mandatory=$true)][string]$Label,
    [switch]$NoCapture
)

$ErrorActionPreference = 'Continue'
$repo = 'c:\Github\Blipscope'
$stamp = Get-Date -Format 'yyyy-MM-dd-HHmm'
$uploadLog = "$repo\bench-logs\flash-$Label-$stamp.txt"

function Say($t) { Write-Host "[bench-flash] $t" }

# ---- 1. stop OUR recorders that name THIS port ------------------------------
# TWO CONDITIONS, both required, and the second is the safety one.
#
#   * it must be one of OUR bench recorders by name. bench-capture.ps1 is not
#     the only one -- an ota_watch.py from an earlier run is what actually held
#     COM119 on 2026-09-09, and a matcher that knew only about bench-capture
#     would have refused the flash without being able to clear it.
#   * its command line must name THE TARGET PORT. COM5 belongs to a different
#     project and must never be touched; scoping the kill to $Port means this
#     cannot reach it however the process list is spelled, rather than relying
#     on an exclusion someone has to remember to maintain.
#
# Anything else holding the port is left alone and reported by the poll below,
# because killing a process we did not start is not this script's business.
Say "stopping our recorders that name $Port"
$killed = 0
# EXCLUDE SELF AND PARENT, and match the SCRIPT FILENAME rather than a bare word.
# A dry run of this matcher killed itself: the test command's own command line
# quoted the strings "ota_watch" and "COM119", so the process doing the matching
# matched. That is not a curiosity -- any shell command that MENTIONS this
# script's targets becomes a target, which includes the automation running it.
#
# So: require the filename WITH ITS EXTENSION (an invocation, not a mention),
# require $Port as a standalone argument rather than a substring, and never
# consider this process or the one that launched it. The residual hazard is a
# command line that genuinely spells out `bench-capture.ps1 ... COM119` without
# being one -- rare, and it now has to try.
$self   = $PID
$parent = (Get-CimInstance Win32_Process -Filter "ProcessId=$PID").ParentProcessId
Get-CimInstance Win32_Process -Filter "Name='powershell.exe' OR Name='pwsh.exe' OR Name='python.exe'" |
    Where-Object {
        $_.CommandLine -and
        $_.ProcessId -ne $self -and
        $_.ProcessId -ne $parent -and
        $_.CommandLine -match '(bench-capture\.ps1|ota_watch\.py)' -and
        $_.CommandLine -match ("(^|[\s'`"])" + [regex]::Escape($Port) + "([\s'`"]|$)")
    } |
    ForEach-Object {
        Say "  killing pid $($_.ProcessId)  ($($_.Name))"
        Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
        $killed++
    }
if ($killed -eq 0) { Say "  none found" }

# ---- 2. WAIT FOR THE HANDLE, by opening it ---------------------------------
# A poll, not a sleep: the only proof the handle is free is that we can take it.
$free = $false
for ($i = 0; $i -lt 20; $i++) {
    try {
        $sp = New-Object System.IO.Ports.SerialPort $Port, 115200
        $sp.Open(); $sp.Close(); $sp.Dispose()
        $free = $true
        Say "port $Port is free (attempt $($i+1))"
        break
    } catch {
        Start-Sleep -Milliseconds 800
    }
}
if (-not $free) {
    Say "REFUSING TO FLASH: $Port never became openable."
    Say "  A flash into a held port fails in ~10 s with a message that reads"
    Say "  like a build error. Something we do not recognise is holding it:"
    Say "    Get-CimInstance Win32_Process | ? { \$_.CommandLine -match '$Port' }"
    exit 3
}

# ---- 3. the upload, BARE, tee'd ---------------------------------------------
Say "uploading env=$PioEnv -> $Port  (log: $uploadLog)"
Push-Location $repo
& pio run -e $PioEnv -t upload --upload-port $Port 2>&1 | Tee-Object -FilePath $uploadLog
$rc = $LASTEXITCODE
Pop-Location

# ---- 4. THE GATE -------------------------------------------------------------
if ($rc -ne 0) {
    Say ""
    Say "UPLOAD FAILED, exit=$rc.  NOT starting the recorder."
    Say "  This is the gate. An unconditional watcher after a failed flash is"
    Say "  what took the port on 2026-09-08 and made the retry worse than the"
    Say "  first attempt. The port is left FREE for a retry."
    Say "  Full output: $uploadLog"
    exit $rc
}

Say "upload OK (exit=0)"

if ($NoCapture) {
    Say "-NoCapture given; not attaching the recorder."
    exit 0
}

# ---- 5. only now, the recorder ----------------------------------------------
Say "attaching recorder label=$Label port=$Port"
Start-Process powershell -WindowStyle Hidden -ArgumentList @(
    '-NoProfile','-ExecutionPolicy','Bypass',
    '-File', "$repo\scripts\bench-capture.ps1",
    '-Port', $Port, '-Label', $Label
)
Start-Sleep -Milliseconds 1200
$pidFile = "$repo\bench-logs\capture-$Label.pid"
if (Test-Path $pidFile) {
    Say "recorder pid $(Get-Content $pidFile)"
} else {
    Say "WARNING: no pid file at $pidFile -- the recorder may not have started."
}
exit 0
