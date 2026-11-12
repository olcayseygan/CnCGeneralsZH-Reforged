<#
.SYNOPSIS
  Plays a fixed-seed skirmish, then plays its own replay back, and compares the two.

.DESCRIPTION
  P6 of PATHFINDING-PLAN.md. Every movement change in that plan edits GameLogic, which is the half
  of the engine that has to arrive at the same answer on every machine in a network game and on
  every playback of a replay. A change that plays beautifully and desyncs costs more than the
  smoothness it buys, and the failure is invisible in a single-machine test: the game plays fine,
  and then somebody cannot join.

  A replay is the cheap version of a second machine. It replays the command stream and nothing
  else, so if the logic is deterministic the playback walks the same frames as the recording. This
  runs a match, keeps its replay, plays that replay back, and compares the CRC of every object in
  the world at the frame both runs stopped on - the HEADLESS CRC line, which any build writes at
  the end of an unattended run.

  A mismatch names the frame it was taken at, not the frame it started at, so it says a divergence
  happened rather than where. That is still the whole difference between shipping a desync and
  not: -DebugCRCFromFrame, which Release parses too, is what narrows it down afterwards.

.EXAMPLE
  .\replay-check.ps1
  .\replay-check.ps1 -Seeds 0,1,2,3 -Seconds 200
    Four seeds instead of two, shorter matches. Each seed costs two runs.
#>
param(
	# the matches to check; each one is played twice, once live and once from its own replay
	[int[]] $Seeds = @(0, 1),
	# players in each match
	[int] $Players = 2,
	# easy | medium | brutal
	[string] $Difficulty = "brutal",
	# how long each match runs, in seconds of game time (400 is 6 min 40 s), turned into logic frames
	# at LOGICFRAMES_PER_SECOND from GameCommon.h. Long enough to build and fight; a divergence in
	# movement code needs units on the move, and the first half minute is two workers walking to a
	# supply dock
	[int] $Seconds = 400,
	# the same length in logic frames outright; 0 takes it from -Seconds
	[int] $MaxFrames = 0,
	# playable cells a side of the generated map; 0 lets the generator size it for the player count.
	# 128 used to be the default, below even the generator's small size for two players: seed 1 put
	# the two starts 124 units apart, neither AI could build, and the match stayed idle
	[int] $MapCells = 0,
	# a generator style (twilight), the last word of -randommap; empty is the plain generator
	[string] $Style = "",
	# where the game is
	[string] $RunDir = "$PSScriptRoot\GeneralsMD\Run",
	[string] $Exe = "generals.exe",
	# switches to add to both halves of every seed. A switch that reaches the match has to be on for
	# the recording and the playback alike: turning it on for one of them is a divergence the script
	# would report as a broken build. -ExtraArgs -unitlimit is the unit limit's determinism check
	[string[]] $ExtraArgs = @(),
	# minutes before a run is killed rather than waited on forever, as ai-batch.ps1 does. -headless
	# does not stop every dialog (a missing base game still puts up a message box), and no unattended
	# run may wait on a person; the killed run reports no result, which counts as a failure
	[int] $TimeoutMinutes = 60,
	# the games' user data folder (ZH_USER_DATA_DIR, which EarlyOptions.h honours on Windows too), so that
	# several of these can run side by side: each recording is written to Replays\00000000.rep there.
	# Empty: the player's own, in Documents, as always
	[string] $UserDataDir = ""
)

$ErrorActionPreference = "Stop"
# the logic rate, from the engine's own header so this script never carries a copy of it
$LogicFps = [int][regex]::Match((Get-Content -Raw "$PSScriptRoot\GeneralsMD\Code\GameEngine\Include\Common\GameCommon.h"),
	'(?m)^\s*LOGICFRAMES_PER_SECOND\s*=\s*(\d+)').Groups[1].Value
if ($LogicFps -le 0) { throw "no LOGICFRAMES_PER_SECOND in GameCommon.h" }
if ($MaxFrames -le 0) { $MaxFrames = $Seconds * $LogicFps }
$env:ZH_UNATTENDED = "1"	# every game this starts is unattended: no box may wait on a person (EarlyCommandLine.h)

$exePath = Join-Path $RunDir $Exe
if (-not (Test-Path $exePath)) { throw "no $Exe in $RunDir" }

# The game writes its replays here and always to the same name, so a run has to be moved aside
# before the next one overwrites it.
if ($UserDataDir -ne "") {
	New-Item -ItemType Directory -Force $UserDataDir | Out-Null
	$env:ZH_USER_DATA_DIR = (Resolve-Path $UserDataDir).Path		# every game started below inherits it
	$replayDir = Join-Path $env:ZH_USER_DATA_DIR "Replays"
} else {
	Remove-Item Env:\ZH_USER_DATA_DIR -ErrorAction SilentlyContinue
	$replayDir = Join-Path $env:USERPROFILE "Documents\Command and Conquer Generals Zero Hour Data\Replays"
}
$lastReplay = Join-Path $replayDir "00000000.rep"

function Invoke-Run([string[]] $extra, [string] $prefix)
{
	# -noFPSLimit for the same reason as in ai-batch.ps1: headless does not need it, and a run that
	# has its window back must not be paced by the monitor. It cannot touch the logic, so the CRC
	# this script compares is unaffected either way.
	$args = @("-headless", "-quickstart", "-noshellmap", "-multiInstance", "-noFPSLimit",
						"-maxframes", $MaxFrames, "-logPrefix", $prefix) + $extra + $ExtraArgs
	# the log's name says the build (Debug.cpp): DebugLogFile.txt Release, DebugLogFileD.txt Debug,
	# DebugLogFileI.txt Internal.  None may be left from an earlier run to be read as this one's.
	$logNames = @("DebugLogFile.txt", "DebugLogFileD.txt", "DebugLogFileI.txt") | ForEach-Object { Join-Path $RunDir "$prefix$_" }
	$logNames | Where-Object { Test-Path $_ } | ForEach-Object { Remove-Item $_ -Force }
	$proc = Start-Process -FilePath $exePath -ArgumentList $args -WorkingDirectory $RunDir -PassThru
	$proc.PriorityClass = 'AboveNormal'
	$null = $proc.Handle		# kept, so ExitCode is still there after the exit
	if (-not $proc.WaitForExit($TimeoutMinutes * 60 * 1000)) {
		$proc.Kill()
		$proc.WaitForExit()
		Write-Host ("KILLED ({0} wedged past {1} min) " -f $prefix, $TimeoutMinutes) -NoNewline
		return $null
	}
	# The codes an unattended run (ZH_UNATTENDED, -headless) leaves by name instead of waiting on a box;
	# the reason itself is on the game's stderr and in its log.
	switch ($proc.ExitCode) {
		2 { Write-Host ("exit 2: {0} stopped unattended (no base game, or a broken INI) " -f $prefix) -NoNewline }
	}
	$log = $logNames | Where-Object { Test-Path $_ } | Select-Object -First 1
	if ($null -eq $log) { return $null }
	$crcLine = Select-String -Path $log -Pattern "HEADLESS CRC: (0x[0-9A-F]+) at frame (\d+)" | Select-Object -Last 1
	$resLine = Select-String -Path $log -Pattern "HEADLESS RESULT: (.+)$" | Select-Object -Last 1
	if ($null -eq $crcLine) { return $null }
	return [pscustomobject]@{
		CRC    = $crcLine.Matches[0].Groups[1].Value
		Frame  = [int] $crcLine.Matches[0].Groups[2].Value
		Result = if ($resLine) { $resLine.Matches[0].Groups[1].Value } else { "" }
	}
}

$failures = 0
$diverged = 0		# of the failures, the seeds whose two runs finished and disagreed
foreach ($seed in $Seeds)
{
	Write-Host ("seed {0}: recording ... " -f $seed) -NoNewline

	if (Test-Path $lastReplay) { Remove-Item $lastReplay -Force }

	# -observer, so both sides are AI and the command stream is entirely the AI's own decisions
	$mapArgs = @("-randommap", $seed, $Players)
	if ($MapCells -gt 0) { $mapArgs += $MapCells }
	if ($Style) { $mapArgs += $Style }
	$live = Invoke-Run ($mapArgs + @("-autoskirmish", $Players, "-aidiff", $Difficulty,
											 "-seed", $seed, "-observer")) "det$($seed)_live"
	if ($null -eq $live) { Write-Host "no result from the live run"; $failures++; continue }
	if (-not (Test-Path $lastReplay)) { Write-Host "the live run wrote no replay"; $failures++; continue }

	# out of the way of the next recording, and under a name -replay can be given
	$kept = Join-Path $replayDir ("determinism{0}.rep" -f $seed)
	Move-Item $lastReplay $kept -Force

	Write-Host ("frame {0}, CRC {1}; playing back ... " -f $live.Frame, $live.CRC) -NoNewline

	$back = Invoke-Run @("-replay", ("determinism{0}" -f $seed)) "det$($seed)_back"
	if ($null -eq $back) { Write-Host "no result from the playback"; $failures++; continue }

	if ($back.CRC -eq $live.CRC -and $back.Frame -eq $live.Frame)
	{
		Write-Host ("same at frame {0}" -f $back.Frame)
	}
	else
	{
		Write-Host ("DIVERGED: live {0} at frame {1}, playback {2} at frame {3}" -f
								$live.CRC, $live.Frame, $back.CRC, $back.Frame)
		$failures++
		$diverged++
	}
}

Write-Host ""
if ($failures -eq 0)
{
	Write-Host ("{0} of {0} replays played back to the same world." -f $Seeds.Count)
}
elseif ($diverged -gt 0)
{
	Write-Host ("{0} of {1} did not. The logic is not deterministic; do not ship it." -f $failures, $Seeds.Count)
}
else
{
	# a run that crashed, wrote nothing or was killed at -TimeoutMinutes compared nothing: a failure, not a verdict
	Write-Host ("{0} of {1} gave no result to compare (crashed, killed or wrote no replay); nothing was compared." -f $failures, $Seeds.Count)
}
exit $failures
