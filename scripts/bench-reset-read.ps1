# Reset a RUNNING bench board and capture its boot output in one port session.
#
# WHY THIS EXISTS. bench-capture.ps1 deliberately does NOT reset a running board
# (it holds DTR and RTS both asserted, which on this auto-reset circuit is a
# no-op), and that is the right default -- attaching a recorder must not end a
# soak. But a config change is only visible in the `[quiet] armed:` line, which
# prints ONCE at boot, and a config save does not reboot ("No reboot: flag the
# change and let loop() re-read settings on the main task").
#
# So reading back a config change needs a deliberate reset, and it must be a
# NORMAL-BOOT reset rather than a download-mode one:
#
#     DTR = false   -> GPIO0 HIGH -> boot the application, NOT the ROM loader
#     RTS = true    -> EN LOW     -> hold in reset
#     RTS = false   -> EN HIGH    -> run
#
# Getting DTR the wrong way round drops the board into the bootloader, where it
# emits nothing and looks exactly like the wedge this bench has hit twice.
#
# The read happens in the SAME open session as the pulse, because the boot print
# lands within ~2 s and re-attaching a separate recorder would miss it.
param(
    [Parameter(Mandatory=$true)][string]$Port,
    [int]$Seconds = 25,
    [string]$SaveTo = ''
)

# Free the port: our recorders only, scoped to this port (see bench-flash.ps1).
$self   = $PID
$parent = (Get-CimInstance Win32_Process -Filter "ProcessId=$PID").ParentProcessId
Get-CimInstance Win32_Process -Filter "Name='powershell.exe' OR Name='pwsh.exe' OR Name='python.exe'" |
    Where-Object {
        $_.CommandLine -and $_.ProcessId -ne $self -and $_.ProcessId -ne $parent -and
        $_.CommandLine -match '(bench-capture\.ps1|ota_watch\.py)' -and
        $_.CommandLine -match ("(^|[\s'`"])" + [regex]::Escape($Port) + "([\s'`"]|$)")
    } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }

$free = $false
# 45 x 800ms = 36s. Was 20 x 500ms = 10s, and that refused a legitimate reset on
# 2026-09-10 because Windows had not released the COM handle yet. bench-flash.ps1
# had the same bug and was widened that morning; this copy was not, which is the
# two-guards-on-one-rule shape -- the second copy is always the stale one.
for ($i = 0; $i -lt 45; $i++) {
    try { $t = New-Object System.IO.Ports.SerialPort $Port,115200; $t.Open(); $t.Close(); $t.Dispose(); $free = $true; break }
    catch { Start-Sleep -Milliseconds 800 }
}
if (-not $free) { Write-Host "[reset-read] port $Port never freed"; exit 3 }

$sp = New-Object System.IO.Ports.SerialPort $Port,115200,'None',8,'One'
$sp.DtrEnable = $false     # GPIO0 high -> application boot
$sp.RtsEnable = $true      # EN low     -> in reset
$sp.Open()
Start-Sleep -Milliseconds 200
$sp.RtsEnable = $false     # EN high    -> run
Write-Host "[reset-read] reset pulsed on $Port; reading ${Seconds}s"

$sb = New-Object System.Text.StringBuilder
$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
    try { $null = $sb.Append($sp.ReadExisting()) } catch {}
    Start-Sleep -Milliseconds 250
}
$sp.Close(); $sp.Dispose()

$txt = $sb.ToString()
Write-Host "[reset-read] $($txt.Length) bytes"
if ($SaveTo -ne '') { Set-Content -Path $SaveTo -Value $txt -Encoding UTF8; Write-Host "[reset-read] saved: $SaveTo" }
# MATCH THE WHOLE TAG, NOT THE EXPECTED SENTENCE. This filter used to name
# '[quiet] armed' specifically, and on 2026-09-09 it silently swallowed
# '[quiet] migrated ...' -- the one line the run existed to see. A filter written
# against the output you expect is blindest exactly when something new appears,
# which is the case you are usually running for.
$txt -split "`n" | Where-Object { $_ -match '\[build\] env=|\[boot\]|\[quiet\]|\[netwd\]|\[cfg-migrate\]|IP=|rst:0x' }
