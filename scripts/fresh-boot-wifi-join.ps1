# fresh-boot-wifi-join.ps1 -- STEP 2 of the automated fresh-boot acceptance, RUN BY THE OWNER HIMSELF
# (docs/fresh-boot-automation.md; the reviewer, 2026-11-13 02:03:41Z).
#
# It is his command, not a session's, because it does two things no session does on his behalf: it saves his real
# bench Wi-Fi password into the board's setup form, and it adds (then removes) a Wi-Fi profile on this PC.
#
# What it does, on the adapter he names (the one NOT carrying the LAN):
#   1. reads ssid= and password= from %USERPROFILE%\.config\blipscope\bench-wifi (never printed);
#   2. finds exactly one open setup network named Blipscope-XXXXXX (waits up to 120 s; refuses if two are in range);
#   3. adds a temporary open profile for it and connects;
#   4. posts the save form to the board's portal (192.168.4.1/wifisave: s=, p=), in-process, never on a command line;
#   5. disconnects and removes the profile, ALWAYS (finally), so the PC is left as it was.
# It prints only what happened: never the setup network's name (it carries part of the board's MAC), his network's
# name or his password.
#
#   powershell -ExecutionPolicy Bypass -File scripts\fresh-boot-wifi-join.ps1 -Adapter "Wi-Fi 2"
#   ... -Control WrongPassword     K-wifi, rehearsal only: posts a fixed WRONG password (never his), so the board must
#                                  stay in its setup portal; then run it again without -Control
param([Parameter(Mandatory = $true)][string]$Adapter, [ValidateSet("", "WrongPassword")][string]$Control = "")
$ErrorActionPreference = "Stop"

function Say([string]$m) { Write-Host ("{0} {1}" -f (Get-Date).ToUniversalTime().ToString("HH:mm:ssZ"), $m) }

$file = Join-Path $env:USERPROFILE ".config\blipscope\bench-wifi"
if (-not (Test-Path $file)) { Say "bench-wifi: absent. Nothing done."; exit 2 }
$lines = Get-Content -LiteralPath $file
$s = @($lines | Where-Object { $_ -match '^ssid=.' }); $p = @($lines | Where-Object { $_ -match '^password=.' })
if ($s.Count -ne 1 -or $p.Count -ne 1) { Say "bench-wifi: malformed (needs exactly one ssid= and one password= line). Nothing done."; exit 2 }
$ssid = $s[0].Substring(5); $pw = $p[0].Substring(9)
Say "bench-wifi: present"
if ($Control -eq "WrongPassword") { $pw = "fba-control-not-the-password"; Say "K-wifi control: posting a fixed WRONG password" }

$ap = $null; $end = (Get-Date).AddSeconds(120)
while (-not $ap -and (Get-Date) -lt $end) {
  $found = @(netsh wlan show networks interface="$Adapter" | ForEach-Object { if ($_ -match '^SSID \d+ : (Blipscope-[0-9A-F]{6})\s*$') { $Matches[1] } } | Select-Object -Unique)
  if ($found.Count -gt 1) { Say "two or more Blipscope setup networks are in range: refusing to guess. Nothing done."; exit 2 }
  if ($found.Count -eq 1) { $ap = $found[0] } else { Start-Sleep -Seconds 5 }
}
if (-not $ap) { Say "no Blipscope setup network on adapter '$Adapter' within 120 s. Nothing done."; exit 2 }
Say "setup network found (1)"

$xml = Join-Path $env:TEMP ("fba-" + [guid]::NewGuid().ToString("N") + ".xml")
@"
<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
  <name>$ap</name>
  <SSIDConfig><SSID><name>$ap</name></SSID></SSIDConfig>
  <connectionType>ESS</connectionType>
  <connectionMode>manual</connectionMode>
  <MSM><security><authEncryption><authentication>open</authentication><encryption>none</encryption><useOneX>false</useOneX></authEncryption></security></MSM>
</WLANProfile>
"@ | Set-Content -LiteralPath $xml -Encoding UTF8

$rc = 3
try {
  netsh wlan add profile filename="$xml" interface="$Adapter" user=current | Out-Null
  netsh wlan connect name="$ap" ssid="$ap" interface="$Adapter" | Out-Null
  $joined = $false; $end = (Get-Date).AddSeconds(30)
  while (-not $joined -and (Get-Date) -lt $end) {
    $block = (netsh wlan show interfaces) -join "`n"
    $joined = $block -match ("(?s)Name\s*:\s*" + [regex]::Escape($Adapter) + ".*?State\s*:\s*connected.*?SSID\s*:\s*" + [regex]::Escape($ap))
    if (-not $joined) { Start-Sleep -Seconds 2 }
  }
  if (-not $joined) {
    Say "joined: no (30 s)"; $rc = 3   # no `return` here: it would end the script before `exit $rc`, exiting 0
  } else {
    Say "joined: yes"
    $r = Invoke-WebRequest -Uri "http://192.168.4.1/wifisave" -Method Post -Body @{ s = $ssid; p = $pw } -UseBasicParsing -TimeoutSec 20
    Say ("save: HTTP {0}" -f [int]$r.StatusCode)
    $rc = if ([int]$r.StatusCode -eq 200) { 0 } else { 3 }
  }
} catch {
  Say ("FAILED: {0}" -f $_.Exception.GetType().Name)   # the type only: a message could carry a value
  $rc = 3
} finally {
  netsh wlan disconnect interface="$Adapter" | Out-Null
  netsh wlan delete profile name="$ap" interface="$Adapter" | Out-Null
  Remove-Item -LiteralPath $xml -ErrorAction SilentlyContinue
  $left = @(netsh wlan show profiles interface="$Adapter" | Where-Object { $_ -match [regex]::Escape($ap) }).Count
  Say ("profile removed: {0}" -f ($(if ($left -eq 0) { "yes" } else { "NO -- remove it by hand" })))
  $ssid = $null; $pw = $null
}
exit $rc
