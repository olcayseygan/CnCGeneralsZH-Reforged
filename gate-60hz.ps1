<#
.SYNOPSIS
  The 60 Hz gate: eight AIs, 2000 objects, a drawn match, and a nonzero exit when p99.9 passes 16.7 ms.

.DESCRIPTION
  The bar is every frame under 16.7 ms however long a match runs, on Twilight Flame with eight Hard
  AIs, so a mean says nothing here and the number judged is p99.9, logic and picture each on its own
  line. Scenarios/gate60hz.txt holds the match at 2000 objects from minute ten on, which is the most
  it is built for; the plain match stops near 1560.

  The window draws at 1080p with 4x MSAA and the logic on its own thread (-logicthread 1), so the
  picture's tail and the logic's come apart: a tick pass that waits on logic is the logic's
  fault and shows in LOGICTIME, the render's own share in the PASSES lines. The whole thing is one
  seed, so the same exe draws the same crowd and a -Window run is a slice of the same match the hour
  plays: two exes are compared gate to gate, never against a plain match (-scenario fixes the start
  positions, so its CRC is its own).

  Failing is a result: this reports it and exits 1. Exit 2 is a run that gave no numbers (crashed,
  killed, or stopped short), and a run that never reached the object count is a failure too, because
  a green gate at 1200 objects would say nothing about 2000.

.EXAMPLE
  .\gate-60hz.ps1
    The hour: 216000 logic frames, all of them drawn. A real hour once it holds 60 Hz, about two
    today, since the run falls behind the clock and never drops a tick.

.EXAMPLE
  .\gate-60hz.ps1 -Minutes 17 -Window 2
    Undrawn up to minute 15, then two drawn minutes. About thirteen minutes of wall clock.
#>
param(
	# game minutes the match runs, 60 logic frames a second
	[int] $Minutes = 60,
	# draw only the last this many of them and reach the rest undrawn at full speed; 0 draws all of them
	[int] $Window = 0,
	# where the game is
	[string] $RunDir = "$PSScriptRoot\GeneralsMD\Run",
	[string] $Exe = "generals.exe",
	# switches to add to the run
	[string[]] $ExtraArgs = @(),
	# the game's user data folder (ZH_USER_DATA_DIR). Empty: one of this script's own under TEMP, so the
	# picture measured is never the Options.ini of whoever runs it
	[string] $UserDataDir = "",
	# copied to Options.ini there. Empty: a file the folder already has stays, and a folder with none
	# gets reforged-look.ini, the setup the gate measures, the Reforged picture at High
	[string] $Options = "",
	# minutes of wall clock before a run is killed and counted as no result
	[int] $TimeoutMinutes = 180
)

$ErrorActionPreference = "Stop"
$env:ZH_UNATTENDED = "1"	# no box may wait on a person (EarlyCommandLine.h)

$BudgetMS = 16.7
$ObjectTarget = 2000		# Scenarios/gate60hz.txt
$HistogramTopMS = 64		# the log's frame time histogram: past this a percentile reads as the worst frame

$exePath = Join-Path $RunDir $Exe
if (-not (Test-Path $exePath)) { throw "no $Exe in $RunDir" }
if ($UserDataDir -eq "") { $UserDataDir = Join-Path $env:TEMP "gate60hz_userdata" }
New-Item -ItemType Directory -Force $UserDataDir | Out-Null
$env:ZH_USER_DATA_DIR = (Resolve-Path $UserDataDir).Path
$optionsFile = Join-Path $env:ZH_USER_DATA_DIR "Options.ini"
if ($Options -ne "") { Copy-Item $Options $optionsFile }
elseif (-not (Test-Path $optionsFile)) { Copy-Item "$PSScriptRoot\reforged-look.ini" $optionsFile }

$lastFrame = $Minutes * 60 * 60
$prefix = "gate60hz_"
$log = Join-Path $RunDir "${prefix}DebugLogFile.txt"
if (Test-Path $log) { Remove-Item $log -Force }

# -noFPSLimit: a windowed device honours vsync and a run paced by the monitor measures the monitor.
# No -slowframe: a bar low enough to help writes and flushes a line on every frame over it, and those
# frames are the ones being measured. The camera sits where the road from every seat ends.
$runArgs = @("-win", "-borderless", "-xres", "1920", "-yres", "1080", "-msaa", "4", "-particlecap", "2500", "-nochroma",
	"-noFPSLimit", "-quickstart", "-noshellmap", "-observer", "-multiInstance",
	"-map", '"Maps\Twilight Flame\Twilight Flame.map"', "-autoskirmish", "8", "-teams", "2", "-aidiff", "hard", "-seed", "3",
	"-logicthread", "1", "-scenario", "gate60hz", "-camera", "2430", "2345",
	"-maxframes", $lastFrame, "-logPrefix", $prefix) + $ExtraArgs
if ($Window -gt 0) { $runArgs += @("-jumpToFrame", (($Minutes - $Window) * 60 * 60)) }

$proc = Start-Process -FilePath $exePath -ArgumentList $runArgs -WorkingDirectory $RunDir -PassThru
$null = $proc.Handle		# kept, so ExitCode is still there after the exit
try {
	$proc.PriorityClass = 'AboveNormal'
	if (-not $proc.WaitForExit($TimeoutMinutes * 60 * 1000)) {
		Write-Host ("KILLED: past {0} min" -f $TimeoutMinutes)
		exit 2
	}
} finally {
	if (-not $proc.HasExited) { $proc.Kill(); $proc.WaitForExit() }
}

function Get-LogMatch([string] $pattern)
{
	if (-not (Test-Path $log)) { return $null }
	$hit = Select-String -Path $log -Pattern $pattern | Select-Object -Last 1
	if ($null -eq $hit) { return $null }
	return $hit.Matches[0].Groups
}

$time = 'mean ([\d.]+) p50 [\d.]+ p95 [\d.]+ p99 ([\d.]+) p99\.9 ([\d.]+) worst ([\d.]+) ms \(frame (\d+)\)'
$frame = Get-LogMatch "HEADLESS FRAMETIME: (\d+) frames \| $time \| over 16\.7ms (\d+) \(([\d.]+)%\)"
$logic = Get-LogMatch "HEADLESS LOGICTIME: (\d+) ticks \| $time"
$objects = Get-LogMatch 'HEADLESS OBJECTS: peak (\d+) \(frame (\d+)\) \| mean (\d+)'
$crc = Get-LogMatch 'HEADLESS CRC: (0x[0-9A-F]+) at frame (\d+)'
$scenario = Get-LogMatch 'HEADLESS SCENARIO: (.+)$'
$part = '([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)'
$tick = Get-LogMatch "HEADLESS PASSES TICK: (\d+) \| pass $part \| sync $part \| render $part \| logic $part"
$noTick = Get-LogMatch "HEADLESS PASSES NOTICK: (\d+) \| pass $part \| sync $part \| render $part"

if ($null -eq $frame -or $null -eq $logic -or $null -eq $objects -or $null -eq $crc -or [int] $crc[2].Value -ne $lastFrame) {
	Write-Host ("NO RESULT: exit {0}, the log {1} holds no full run to {2} frames" -f $proc.ExitCode, $log, $lastFrame)
	exit 2
}

$frameTail = [double] $frame[4].Value
$logicTail = [double] $logic[4].Value
$objectPeak = [int] $objects[1].Value

Write-Host ("frame   p99.9 {0} ms | mean {1} p99 {2} worst {3} (frame {4}) | {5} of {6} over 16.7 ms ({7}%)" -f
	$frame[4].Value, $frame[2].Value, $frame[3].Value, $frame[5].Value, $frame[6].Value,
	$frame[7].Value, $frame[1].Value, $frame[8].Value)
if ($frameTail -gt $HistogramTopMS) {
	Write-Host ("        p99.9 is past the histogram's {0} ms, so it reads as the worst frame; the mean is the number to read" -f $HistogramTopMS)
}
Write-Host ("logic   p99.9 {0} ms | mean {1} p99 {2} worst {3} (frame {4})" -f
	$logic[4].Value, $logic[2].Value, $logic[3].Value, $logic[5].Value, $logic[6].Value)
if ($null -ne $tick -and $null -ne $noTick) {
	Write-Host ("render  p99.9 {0} ms on {1} passes with a tick, sync {2}; {3} ms on {4} passes without, sync {5}" -f
		$tick[13].Value, $tick[1].Value, $tick[9].Value, $noTick[13].Value, $noTick[1].Value, $noTick[9].Value)
}
Write-Host ("objects peak {0} (frame {1}) mean {2}, target {3}" -f $objectPeak, $objects[2].Value, $objects[3].Value, $ObjectTarget)
Write-Host ("crc     {0} at frame {1}" -f $crc[1].Value, $crc[2].Value)
if ($null -ne $scenario) { Write-Host ("scenario {0}" -f $scenario[1].Value) }

$failed = @()
if ($frameTail -gt $BudgetMS) { $failed += "frame p99.9" }
if ($logicTail -gt $BudgetMS) { $failed += "logic p99.9" }
if ($objectPeak -lt $ObjectTarget) { $failed += "object count" }
if ($failed.Count -gt 0) {
	Write-Host ("FAIL: {0}" -f ($failed -join ", "))
	exit 1
}
Write-Host "PASS"
exit 0
