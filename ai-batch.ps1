<#
.SYNOPSIS
  Runs a batch of headless skirmishes and reports the win rate.

.DESCRIPTION
  Section 0 of AI-ROADMAP.md: no AI change can be claimed without a number behind it, and one
  match proves nothing - the same seed on the same map is repeatable, but a different seed is a
  different game. This runs the same configuration over a spread of seeds and maps and prints
  the win rate per slot, so a change can be reported as "40% -> 65%" instead of "it felt better".

  Each run is one process, headless (no picture, no sound) with the logic tick running flat out,
  bounded by -maxframes so a match the AI cannot close out still ends. The result comes back in
  that run's own log, which -logPrefix names, so runs never overwrite each other's answers.

  A run's map comes from -randommap, so nothing has to be installed to compare two builds - the
  seed is the map.

.EXAMPLE
  .\ai-batch.ps1 -Runs 20 -Tag base
  .\ai-batch.ps1 -Runs 20 -Tag counter-comp -Difficulty brutal
    ... then compare the two win rates.

.EXAMPLE
  .\ai-batch.ps1 -Runs 8 -Map "Twilight Flame" -Difficulty brutal -Exe generals_perf.exe
    A fixed map instead of a generated one, so the terrain and the unit load are the same in every
    batch, and the PERF_TIMERS exe, so the run also prints where the frame went. That is the pair
    of switches THREADING-ROADMAP.md section 0 is measured with.
#>
param(
	# how many matches to play
	[int] $Runs = 20,
	# players in each match; slot 0..N-1 all AI under -autoskirmish
	[int] $Players = 2,
	# easy | medium | brutal (anything else is read as brutal by the game)
	[string] $Difficulty = "brutal",
	# rung for the odd-numbered slots; empty means the same as -Difficulty, so a rung plays itself
	[string] $Difficulty2 = "",
	# AI personality for every slot by name (Normal, Attacker, Turtle, ... Random); empty is Normal
	[string] $Persona = "",
	# personality for the odd-numbered slots; empty means the same as -Persona, so a personality
	# plays itself
	[string] $Persona2 = "",
	# split the slots into this many allied teams instead of a free-for-all: 2 with -Players 8 is
	# 4v4. Free-for-all spreads the fighting over the whole map; teams put every unit on one of two
	# fronts, which is the load a player actually complains about
	[int] $Teams = 0,
	# a match that has not been decided by here is a result in itself: the AI cannot finish.
	# Logic frames, 60 a second: 60000 is 16.7 minutes of game time
	[int] $MaxFrames = 60000,
	# names this batch's logs, so two batches can be compared afterwards
	[string] $Tag = "ai",
	# map sizes to spread the batch over, in playable cells a side. Ignored when -Map is given.
	[int[]] $MapCells = @(96, 128, 160),
	# a shipped map to play instead of a generated one, e.g. "Twilight Flame". A generated map is
	# a different map per seed, which is what a change to the AI wants; one hand-made map instead
	# holds the terrain still, which is what a change to the *renderer* wants - the shadow, particle
	# and skinning load has to be the same on both sides of the comparison or the number means
	# nothing. The seed still varies the match.
	[string] $Map = "",
	# generate the maps in a shipped map's style, e.g. "twilight" (-randommap's last argument)
	[string] $Style = "",
	# first seed of the batch. A change tuned against seeds 0..23 has to be confirmed on seeds it
	# was not tuned on, or the number is a fit to twenty-four maps rather than a result
	[int] $SeedStart = 0,
	# where the game is; the default is this repo's own Run directory
	[string] $RunDir = "$PSScriptRoot\GeneralsMD\Run",
	# which executable in that directory to play. Two builds can sit side by side under different
	# names, which is the only way to measure a change against its own baseline without rebuilding
	# between batches - and the only way to run a batch at all while a copy of the game is open.
	[string] $Exe = "generals.exe",
	# extra arguments handed to every run, for sweeping a knob the game reads from the command line
	# (e.g. -ExtraArgs "-unitlimit") without a rebuild between batches
	[string[]] $ExtraArgs = @(),
	# a scenario file from Run/Scenarios played in every match, e.g. overlordrush. With -Takeover the
	# scenario plays that seat by hand and the AI plays the others
	[string] $Scenario = "",
	# the one seat left without an AI (-takeover <slot>); -1 leaves every seat to the AI
	[int] $Takeover = -1,
	# faction per seat, in seat order, e.g. FactionChina,FactionGLA; empty lets the seed draw
	[string[]] $Sides = @(),
	# minutes before a wedged run is killed rather than waited on forever
	[int] $TimeoutMinutes = 20,
	# matches played at once. A headless match is one logic thread, so one a core runs the batch
	# that many times faster and the results are the same: the logic does not read the clock. The
	# frame-time and stutter lines are the exception, every copy slows the others, so a batch that is
	# asked about frame time passes -Parallel 1
	[int] $Parallel = [Environment]::ProcessorCount
)

$ErrorActionPreference = "Stop"
# LOGICFRAMES_PER_SECOND: every frame number the log writes counts these
$LogicFps = 60
$env:ZH_UNATTENDED = "1"	# every game this starts is unattended: no box may wait on a person (EarlyCommandLine.h)

# PowerShell variable names are case-insensitive, so this must not be called $exe: it would be the
# same variable as the -Exe parameter and the summary line would print the whole path back.
$exePath = Join-Path $RunDir $Exe
if (-not (Test-Path $exePath)) {
	throw "no $Exe in $RunDir - build first (build.bat Release copies it there)"
}

# One row per match. Slot results are kept as a hashtable per row so the summary can pivot on them.
$rows = @()

# Every match is started first, $Parallel at a time, and read back afterwards in seed order.
$started = @()

for ($i = 0; $i -lt $Runs; $i++) {

	$seed = $SeedStart + $i
	$cells = $MapCells[$i % $MapCells.Count]
	$prefix = "{0}_{1:d3}" -f $Tag, $seed
	$log = Join-Path $RunDir "$($prefix)DebugLogFile.txt"

	# a stale log from an earlier batch would read as this run's result if the process died early
	Remove-Item $log -ErrorAction SilentlyContinue

	# -observer costs no slot and makes every slot an AI, so the batch measures AI against AI
	# rather than an AI against an idle human seat (see the observer note in setupAutoSkirmish).
	# -multiInstance: the single-instance guard counts a headless run as "Generals is already
	# running" and bails at once, so a batch cannot be measured while a copy of the game is open
	# -noFPSLimit uncaps the renderer. A headless run has no renderer and already ticks its logic
	# every pass, so it changes nothing here; it is passed anyway so that dropping -headless (which
	# is how a renderer or stutter question is measured) does not quietly put the run back behind
	# the monitor's refresh. Game speed is untouched - that is -fps, and moving it would make this
	# batch incomparable with every earlier one.
	$args = @("-headless", "-quickstart", "-noshellmap", "-observer", "-multiInstance", "-noFPSLimit")
	if ($Map) {
		# -map wants the path the map cache holds, and WinMain's tokenizer honours double quotes -
		# which every shipped map name needs, because they all have a space in them.
		$args += "-map"
		$args += ('"Maps\{0}\{0}.map"' -f $Map)
	} else {
		# a generated map is built in memory and written nowhere, so there is nothing to tidy up
		$args += @("-randommap", $seed, $Players, $cells)
		if ($Style) { $args += $Style }
	}
	$args += @(
		"-autoskirmish", $Players,
		"-aidiff", $Difficulty,
		"-seed", $seed,
		"-maxframes", $MaxFrames,
		"-logPrefix", $prefix
	)
	# a second rung for the odd slots, so one rung can be played against another
	if ($Difficulty2) { $args += "-aidiff2"; $args += $Difficulty2 }
	# and personalities the same way, so one can be played against another
	if ($Persona) { $args += "-aipersonaall"; $args += $Persona }
	if ($Persona2) { $args += "-aipersona2"; $args += $Persona2 }
	if ($Teams -gt 1) { $args += "-teams"; $args += $Teams }
	if ($Scenario) { $args += "-scenario"; $args += $Scenario }
	if ($Takeover -ge 0) { $args += "-takeover"; $args += $Takeover }
	for ($s = 0; $s -lt $Sides.Count; $s++) { if ($Sides[$s]) { $args += @("-side", $s, $Sides[$s]) } }
	if ($ExtraArgs.Count) { $args += $ExtraArgs }

	while (@($started | Where-Object { -not $_.Proc.HasExited }).Count -ge $Parallel) {
		# a run that never reaches its match (a bad -side, a missing map) sits in the shell for good, and
		# the wait for a free slot would wait with it: the timeout applies here as well as below
		foreach ($r in $started) {
			if (-not $r.Proc.HasExited -and $r.Clock.Elapsed.TotalMinutes -gt $TimeoutMinutes) { $r.Proc.Kill() }
		}
		Start-Sleep -Milliseconds 200
	}
	$proc = Start-Process -FilePath $exePath -ArgumentList $args -WorkingDirectory $RunDir -PassThru
	$proc.PriorityClass = 'AboveNormal'
	$started += [pscustomobject]@{
		Index = $i; Seed = $seed; Cells = $cells; Log = $log; Proc = $proc
		Clock = [Diagnostics.Stopwatch]::StartNew()
	}
}

foreach ($run in $started) {

	$seed = $run.Seed
	$cells = $run.Cells
	$log = $run.Log
	$proc = $run.Proc
	$sw = $run.Clock

	$where = if ($Map) { $Map } else { "$cells cells" }
	Write-Host ("[{0,3}/{1}] seed {2} {3} ... " -f ($run.Index + 1), $Runs, $seed, $where) -NoNewline
	$timeLeft = [Math]::Max(0, $TimeoutMinutes * 60 * 1000 - $sw.ElapsedMilliseconds)
	if (-not $proc.WaitForExit([int]$timeLeft)) {
		$proc.Kill()
		Write-Host "KILLED (wedged past $TimeoutMinutes min)"
		$rows += [pscustomobject]@{ Seed = $seed; Cells = $cells; Why = "wedged"; Frames = 0; Wall = $sw.Elapsed.TotalSeconds; Slots = @{}; Pf = $null }
		continue
	}
	# the wall time is the process's own; this loop may reach a run long after it finished
	$sw.Stop()
	$wall = ($proc.ExitTime - $proc.StartTime).TotalSeconds

	if (-not (Test-Path $log)) {
		Write-Host "NO LOG (exit $($proc.ExitCode))"
		$rows += [pscustomobject]@{ Seed = $seed; Cells = $cells; Why = "no log"; Frames = 0; Wall = $wall; Slots = @{}; Pf = $null }
		continue
	}

	$why = "no result"
	$frames = 0
	$slots = @{}
	# what the pathfinder did over the whole match, for comparing a pathing change against its
	# baseline: search time and count say what it cost, nopath/outofcells say whether it broke,
	# blocked/stuck say whether the traffic jams it was aimed at actually got shorter
	$pf = [pscustomobject]@{ FindMs = 0.0; Finds = 0; Expands = 0; NoPath = 0; OutOfCells = 0; Blocked = 0; Stuck = 0
													 Wedge = 0; WedgeWorst = 0; Escapes = 0; Repaths = 0
													 Dithers = 0; Orders = 0; Arrived = 0; Stalled = 0
													 Queued = 0; QWait = 0.0; QLongest = 0; QLate = 0; QPeak = 0 }
	# THREADING-ROADMAP.md section 0: the per-scope frame cost, present only in a PERF_TIMERS
	# build (-Exe generals_perf.exe), and the job pool's own report, present in every build.
	$perf = @{}
	$jobs = $null
	# Stability, which is a different question from speed: the tail of the frame-time distribution
	# and the count of frames that missed a 60Hz budget. Present in every build.
	$ft = $null
	$spikes = 0
	# the AI's own account of its fights, per player index: what it lost and killed in money, its team
	# deaths by how alone each one died, what it sent out and why, and the economy lines that say the
	# match was played at all
	$value = @{}
	$loss = @{}
	$engage = @{}
	$econ = [pscustomobject]@{ Reattach = 0; Waves = 0; Counter = 0 }
	# production buildings each player put up, by kind (an arms dealer is GLA's war factory); a lost
	# one put back counts again, so this is at least the most that ever stood at once
	$factories = @{}

	foreach ($line in Get-Content $log) {
		if ($line -match "^AI BUILT frame \d+ player (\d+) '.*?(WarFactory|ArmsDealer|Barracks|Airfield)'") {
			$p = [int]$Matches[1]
			if (-not $factories.ContainsKey($p)) { $factories[$p] = @{ WarFactory = 0; Barracks = 0; Airfield = 0 } }
			$kind = if ($Matches[2] -eq "ArmsDealer") { "WarFactory" } else { $Matches[2] }
			$factories[$p][$kind]++
			continue
		}
		if ($line -match "^AI LOSS frame \d+ player (\d+) .* (straggling|grouped|last), ") {
			$p = [int]$Matches[1]
			if (-not $loss.ContainsKey($p)) { $loss[$p] = @{ straggling = 0; grouped = 0; last = 0 } }
			$loss[$p][$Matches[2]]++
			continue
		}
		if ($line -match "^AI ENGAGE frame \d+ player (\d+) (\w+): (\d+) units, ([\d.]+) power against ([\d.]+) remembered, cost ([\d.]+) matchup ([\d.]+)") {
			$p = [int]$Matches[1]
			if (-not $engage.ContainsKey($p)) { $engage[$p] = @() }
			$engage[$p] += [pscustomobject]@{ Cause = $Matches[2]; Units = [int]$Matches[3]; Power = [double]$Matches[4]
				Theirs = [double]$Matches[5]; Cost = [double]$Matches[6]; Matchup = [double]$Matches[7] }
			continue
		}
		if ($line -match "Re-attaching supply truck") { $econ.Reattach++; continue }
		if ($line -match "^AI WAVE .* sends \d+ teams") { $econ.Waves++; continue }
		if ($line -match "^AI COUNTER") { $econ.Counter++; continue }
		if ($line -match "HEADLESS VALUE (\d+): units worth (\d+) lost, (\d+) killed") {
			$value[[int]$Matches[1]] = [pscustomobject]@{ Lost = [int]$Matches[2]; Killed = [int]$Matches[3] }
			continue
		}
		if ($line -match "HEADLESS PERF (\S+): ([\d.]+) \(([\d.]+)\) x([\d.]+) worst ([\d.]+)") {
			$perf[$Matches[1]] = [pscustomobject]@{
				Net = [double]$Matches[2]; Gross = [double]$Matches[3]; Calls = [double]$Matches[4]
				Worst = [double]$Matches[5]
			}
		}
		elseif ($line -match "HEADLESS FRAMETIME: (\d+) frames \| mean ([\d.]+) p50 ([\d.]+) p95 ([\d.]+) p99 ([\d.]+) p99\.9 ([\d.]+) worst ([\d.]+) ms \(frame (\d+)\) \| over 16\.7ms (\d+) .* \| over 33\.3ms (\d+) ") {
			$ft = [pscustomobject]@{
				Frames = [int]$Matches[1]; Mean = [double]$Matches[2]; P50 = [double]$Matches[3]
				P95 = [double]$Matches[4]; P99 = [double]$Matches[5]; P999 = [double]$Matches[6]
				Worst = [double]$Matches[7]; WorstAt = [int]$Matches[8]
				Over16 = [int]$Matches[9]; Over33 = [int]$Matches[10]
			}
		}
		elseif ($line -match "HEADLESS SPIKE:") { $spikes++ }
		elseif ($line -match "HEADLESS JOBS: (\d+) worker threads, (\d+) allocations from a job") {
			$jobs = [pscustomobject]@{ Workers = [int]$Matches[1]; Allocs = [int]$Matches[2] }
		}
		elseif ($line -match "HEADLESS RESULT: (.+?) on frame (\d+)") {
			$why = $Matches[1]
			$frames = [int]$Matches[2]
		}
		elseif ($line -match "HEADLESS PATHFIND: (.*)") {
			$report = $Matches[1]
			# "queue 6000x/47 find 2313x/0 ... expand 11414x | nopath 0 outofcells 0 blocked 17 stuck 0"
			# Only the outermost scope is timed, so a findPath called from the queue is counted but
			# charged nothing - the match's pathfinder time is the sum over every timed slot, and
			# the search count comes from the entry points that actually run a search.
			foreach ($m in [regex]::Matches($report, "(\d+)x/([\d.]+)")) {
				$pf.FindMs += [double]$m.Groups[2].Value
			}
			foreach ($slot in @("find", "closest", "patch", "attack")) {
				if ($report -match "(?<![\w.])$slot (\d+)x") { $pf.Finds += [int]$Matches[1] }
			}
			if ($report -match "(?<![\w.])expand (\d+)x") { $pf.Expands = [int]$Matches[1] }
			if ($report -match "nopath (\d+)")            { $pf.NoPath = [int]$Matches[1] }
			if ($report -match "outofcells (\d+)")        { $pf.OutOfCells = [int]$Matches[1] }
			if ($report -match "blocked (\d+)")           { $pf.Blocked = [int]$Matches[1] }
			if ($report -match "stuck (\d+)")             { $pf.Stuck = [int]$Matches[1] }
			# the rescue ladder's own counts: a unit stopped with nothing to blame it on, the backing-out
			# manoeuvres that answers, and the routes given up on entirely
			if ($report -match "wedge (\d+)")             { $pf.Wedge = [int]$Matches[1] }
			if ($report -match "worst (\d+)")             { $pf.WedgeWorst = [int]$Matches[1] }
			if ($report -match "escape (\d+)")            { $pf.Escapes = [int]$Matches[1] }
			if ($report -match "repath (\d+)")            { $pf.Repaths = [int]$Matches[1] }
			if ($report -match "dither (\d+)")            { $pf.Dithers = [int]$Matches[1] }
			# how long a request stood in the path queue: the late-game wait players see as a unit
			# that takes seconds to set off
			if ($report -match "queued (\d+) wait ([\d.]+) longest (\d+) late (\d+) peak (\d+)") {
				$pf.Queued = [int]$Matches[1]; $pf.QWait = [double]$Matches[2]; $pf.QLongest = [int]$Matches[3]
				$pf.QLate = [int]$Matches[4]; $pf.QPeak = [int]$Matches[5]
			}
		}
		elseif ($line -match "HEADLESS PLAYER (\d+) '(.*?)': (\w+) \| score (\d+) \| money (\d+) earned (\d+) spent \| units (\d+) built (\d+) lost (\d+) killed peak (\d+)(?:.*\| slot (-?\d+) team (-?\d+))?") {
			# the lobby team is -1 for a free-for-all seat, and absent from a log an older build wrote
			$team = if ($Matches[12]) { [int]$Matches[12] } else { -1 }
			$slots[[int]$Matches[1]] = [pscustomobject]@{
				Name = $Matches[2]; Status = $Matches[3]; Score = [int]$Matches[4]
				Earned = [int]$Matches[5]; Spent = [int]$Matches[6]
				Built = [int]$Matches[7]; Lost = [int]$Matches[8]; Killed = [int]$Matches[9]
				Peak = [int]$Matches[10]; Team = $team
			}
		}
	}

	$winner = ($slots.Keys | Where-Object { $slots[$_].Status -eq "WON" } | Select-Object -First 1)
	$winnerText = if ($null -ne $winner) { "player $winner" } else { "no winner" }
	Write-Host ("{0}, {1}, frame {2}, {3:n0}s" -f $why, $winnerText, $frames, $wall)

	$rows += [pscustomobject]@{ Seed = $seed; Cells = $cells; Why = $why; Frames = $frames; Wall = $wall; Slots = $slots; Pf = $pf; Perf = $perf; Jobs = $jobs; Ft = $ft; Spikes = $spikes
		Value = $value; Loss = $loss; Engage = $engage; Econ = $econ; Factories = $factories; Winner = $winner }
}

# ---------------------------------------------------------------------------------------------
Write-Host ""
Write-Host "=== $Tag : $Runs matches, $Players players$(if ($Teams -gt 1) { " in $Teams teams" }), $Difficulty$(if ($Difficulty2) { " vs " + $Difficulty2 }), $(if ($Map) { "on $Map" } else { "generated maps" }), $Exe, cap $MaxFrames frames$(if ($ExtraArgs.Count) { ", " + ($ExtraArgs -join ' ') }) ==="

$decided = @($rows | Where-Object { $_.Why -eq "decided" })
Write-Host ("decided {0}/{1}   frame-limited {2}   failed {3}" -f
	$decided.Count, $rows.Count,
	@($rows | Where-Object { $_.Why -eq "frame limit reached" }).Count,
	@($rows | Where-Object { $_.Why -in @("wedged", "no log", "no result") }).Count)

if ($decided.Count -gt 0) {
	Write-Host ("average length {0:n0} frames ({1:n1} min of game time)" -f
		($decided | Measure-Object Frames -Average).Average,
		(($decided | Measure-Object Frames -Average).Average / $LogicFps / 60))
}

# The player list carries the civilian and neutral sides before the playable ones, so the indices
# in the log are not 0..N-1. Pivot on whichever ones actually turned up.
$slotIds = $rows | ForEach-Object { $_.Slots.Keys } | Sort-Object -Unique

$summary = foreach ($p in $slotIds) {
	$mine = @($rows | Where-Object { $_.Slots.ContainsKey($p) } | ForEach-Object { $_.Slots[$p] })
	if ($mine.Count -eq 0) { continue }
	$wins = @($mine | Where-Object { $_.Status -eq "WON" }).Count
	[pscustomobject]@{
		Slot     = $p
		Wins     = $wins
		"Win%"   = [math]::Round(100.0 * $wins / $rows.Count, 1)
		AvgPeak  = [math]::Round(($mine | Measure-Object Peak -Average).Average, 1)
		AvgKills = [math]::Round(($mine | Measure-Object Killed -Average).Average, 1)
		AvgLost  = [math]::Round(($mine | Measure-Object Lost -Average).Average, 1)
		AvgSpent = [math]::Round(($mine | Measure-Object Spent -Average).Average, 0)
	}
}
if ($summary) { $summary | Format-Table -AutoSize | Out-String | Write-Host }

# How many of each production building a player put up, the most in any one match of the batch:
# a computer that stands one of each all match is short of factories (AIPlayer::addProductionForQueues).
$factoryIds = $rows | ForEach-Object { if ($_.Factories) { $_.Factories.Keys } } | Sort-Object -Unique
$factoryRows = foreach ($p in $factoryIds) {
	$mine = @($rows | Where-Object { $_.Factories -and $_.Factories.ContainsKey($p) } | ForEach-Object { $_.Factories[$p] })
	[pscustomobject]@{
		Player        = $p
		MaxWarFactory = ($mine | ForEach-Object { $_.WarFactory } | Measure-Object -Maximum).Maximum
		MaxBarracks   = ($mine | ForEach-Object { $_.Barracks } | Measure-Object -Maximum).Maximum
		MaxAirfield   = ($mine | ForEach-Object { $_.Airfield } | Measure-Object -Maximum).Maximum
	}
}
if ($factoryRows) {
	Write-Host "production buildings put up (AI BUILT), most in one match:"
	$factoryRows | Format-Table -AutoSize | Out-String | Write-Host
}

# The exchange each side made, in money: lost over killed below 1 is a side that trades well. Team
# deaths by how alone each one died, straggling being the one the AI could have prevented. What it
# sent out and why, with the two odds it set off on.
$played = @($rows | Where-Object { $_.Value -and $_.Value.Count -gt 0 })
$fightIds = $played | ForEach-Object { $_.Value.Keys } | Sort-Object -Unique
$fights = foreach ($p in $fightIds) {
	$vals = @($played | Where-Object { $_.Value.ContainsKey($p) } | ForEach-Object { $_.Value[$p] })
	$lost = ($vals | Measure-Object Lost -Sum).Sum
	$killed = ($vals | Measure-Object Killed -Sum).Sum
	$deaths = @($played | Where-Object { $_.Loss.ContainsKey($p) } | ForEach-Object { $_.Loss[$p] })
	$strag = ($deaths | ForEach-Object { $_.straggling } | Measure-Object -Sum).Sum
	$all = ($deaths | ForEach-Object { $_.straggling + $_.grouped + $_.last } | Measure-Object -Sum).Sum
	$sent = @($played | Where-Object { $_.Engage.ContainsKey($p) } | ForEach-Object { $_.Engage[$p] })
	[pscustomobject]@{
		Player      = $p
		AvgValLost  = [math]::Round($lost / $vals.Count, 0)
		AvgValKill  = [math]::Round($killed / $vals.Count, 0)
		"Lost/Kill" = if ($killed -gt 0) { [math]::Round($lost / $killed, 2) } else { "-" }
		"Strag%"    = if ($all -gt 0) { [math]::Round(100.0 * $strag / $all, 1) } else { "-" }
		TeamDeaths  = $all
		Sent        = $sent.Count
		Causes      = (($sent | Group-Object Cause | Sort-Object Count -Descending | ForEach-Object { "$($_.Name) $($_.Count)" }) -join ", ")
		"Matchup<1" = if ($sent.Count) { [math]::Round(100.0 * @($sent | Where-Object { $_.Matchup -lt 1 -and $_.Theirs -gt 0 }).Count / $sent.Count, 1) } else { "-" }
	}
}
if ($fights) {
	Write-Host "fights (money; Strag% = team deaths with 2+ teammates alive and at most one within 300):"
	$fights | Format-Table -AutoSize | Out-String -Width 400 | Write-Host
	# "Re-attaching supply truck" is written only for a gatherer whose dock is gone or was never set:
	# GLA workers back from building, a free Chinook, trucks of a lost center. A China against China
	# match that loses no center writes none while it earns 80,000 a player, so the income is the
	# number that says the trucks worked; the re-attaches say how many were orphaned.
	Write-Host ("economy per match: {0:n0} earned a player, {1:n1} orphaned trucks re-docked, {2:n1} AI WAVE sends, {3:n1} AI COUNTER picks" -f
		(($played | ForEach-Object { $_.Slots.Values } | Measure-Object Earned -Average).Average),
		(($played | ForEach-Object { $_.Econ.Reattach } | Measure-Object -Average).Average),
		(($played | ForEach-Object { $_.Econ.Waves } | Measure-Object -Average).Average),
		(($played | ForEach-Object { $_.Econ.Counter } | Measure-Object -Average).Average))
}

# With -Teams the question is which block won, not which seat: allies share a victory, so a team
# is counted once per match it won, and its kills and losses are the sum over its members.
$teamIds = $rows | ForEach-Object { $_.Slots.Values } | Where-Object { $_.Team -ge 0 } | ForEach-Object { $_.Team } | Sort-Object -Unique
$teamSummary = foreach ($t in $teamIds) {
	# not $matches: PowerShell names are case-insensitive and that is the -match result every parse above reads
	$teamRows = @($rows | ForEach-Object { ,@($_.Slots.Values | Where-Object { $_.Team -eq $t }) } | Where-Object { $_.Count -gt 0 })
	$wins = @($teamRows | Where-Object { @($_ | Where-Object { $_.Status -eq "WON" }).Count -gt 0 }).Count
	$seats = $rows | ForEach-Object { $slots = $_.Slots; $slots.Keys | Where-Object { $slots[$_].Team -eq $t } } | Sort-Object -Unique
	[pscustomobject]@{
		Team     = $t
		Players  = ($seats -join ",")
		Wins     = $wins
		"Win%"   = [math]::Round(100.0 * $wins / $rows.Count, 1)
		AvgKills = [math]::Round(($teamRows | ForEach-Object { ($_ | Measure-Object Killed -Sum).Sum } | Measure-Object -Average).Average, 1)
		AvgLost  = [math]::Round(($teamRows | ForEach-Object { ($_ | Measure-Object Lost -Sum).Sum } | Measure-Object -Average).Average, 1)
	}
}
if ($teamSummary) {
	Write-Host "per team:"
	$teamSummary | Format-Table -AutoSize | Out-String | Write-Host
}

# Pathfinder cost and traffic, averaged per match. A match that produced no log has nothing to
# say here, so it is left out rather than counted as a zero.
$pfRows = @($rows | Where-Object { $null -ne $_.Pf -and $_.Frames -gt 0 })
if ($pfRows.Count -gt 0) {
	$avg = { param($f) [math]::Round((($pfRows | ForEach-Object { $f.Invoke($_.Pf) } | Measure-Object -Average).Average), 1) }
	Write-Host ("pathfind per match: {0:n0} searches / {1:n0}ms, {2:n0} cell expansions | nopath {3} outofcells {4} | blocked unit-frames {5} (stuck {6})" -f
		(& $avg { param($p) $p.Finds }),
		(& $avg { param($p) $p.FindMs }),
		(& $avg { param($p) $p.Expands }),
		(& $avg { param($p) $p.NoPath }),
		(& $avg { param($p) $p.OutOfCells }),
		(& $avg { param($p) $p.Blocked }),
		(& $avg { param($p) $p.Stuck }))
	# blocked unit-frames scale with match length and army size, so the rate is the comparable number
	$totFrames = ($pfRows | Measure-Object Frames -Sum).Sum
	$totBlocked = ($pfRows | ForEach-Object { $_.Pf.Blocked } | Measure-Object -Sum).Sum
	Write-Host ("blocked unit-frames per 1000 logic frames: {0:n1}" -f (1000.0 * $totBlocked / $totFrames))

	# and the half of it a collision never sees: stopped with nothing to blame, and what was done
	# about it.
	$totWedge = ($pfRows | ForEach-Object { $_.Pf.Wedge } | Measure-Object -Sum).Sum
	# the worst is the one that matters: the rescue ladder promises nothing is left wanting to move
	# and not moving for longer than its own cycle, and this is the number that holds it to it
	$worstWedge = ($pfRows | ForEach-Object { $_.Pf.WedgeWorst } | Measure-Object -Maximum).Maximum
	Write-Host ("wedged unit-frames per 1000 logic frames: {0:n1} | worst single wedge {1} frames ({2:n1}s) | escapes {3} repaths {4} dithers {5} per match" -f
		(1000.0 * $totWedge / $totFrames),
		$worstWedge, ($worstWedge / $LogicFps),
		(& $avg { param($p) $p.Escapes }),
		(& $avg { param($p) $p.Repaths }),
		(& $avg { param($p) $p.Dithers }))

	# the path queue: three requests a frame, so a late 4v4 backs it up and a unit stands waiting
	$totQueued = ($pfRows | ForEach-Object { $_.Pf.Queued } | Measure-Object -Sum).Sum
	if ($totQueued -gt 0) {
		$totLate = ($pfRows | ForEach-Object { $_.Pf.QLate } | Measure-Object -Sum).Sum
		Write-Host ("path queue per match: {0:n0} requests, mean wait {1:n1} frames, {2:n1}% waited over 1s | longest {3} frames ({4:n1}s), deepest {5}" -f
			(& $avg { param($p) $p.Queued }),
			(& $avg { param($p) $p.QWait }),
			(100.0 * $totLate / $totQueued),
			(($pfRows | ForEach-Object { $_.Pf.QLongest } | Measure-Object -Maximum).Maximum),
			((($pfRows | ForEach-Object { $_.Pf.QLongest } | Measure-Object -Maximum).Maximum) / $LogicFps),
			(($pfRows | ForEach-Object { $_.Pf.QPeak } | Measure-Object -Maximum).Maximum))
	}
}

# Stability. A mean frame time is the statistic that hides a stutter, so this reports the tail:
# how bad the worst frames were and how many of them missed a 60Hz budget. Per 1000 frames, because
# a longer batch would otherwise look worse than a short one.
$ftRows = @($rows | Where-Object { $null -ne $_.Ft -and $_.Ft.Frames -gt 0 })
if ($ftRows.Count -gt 0) {
	$totFt = ($ftRows | ForEach-Object { $_.Ft.Frames } | Measure-Object -Sum).Sum
	$totOver16 = ($ftRows | ForEach-Object { $_.Ft.Over16 } | Measure-Object -Sum).Sum
	$totOver33 = ($ftRows | ForEach-Object { $_.Ft.Over33 } | Measure-Object -Sum).Sum
	$worst = $ftRows | Sort-Object -Property { $_.Ft.Worst } -Descending | Select-Object -First 1
	Write-Host ("frame time: mean {0:n2} p50 {1:n2} p99 {2:n2} p99.9 {3:n2} ms | worst {4:n1} ms (seed {5}, logic frame {6})" -f
		(($ftRows | ForEach-Object { $_.Ft.Mean } | Measure-Object -Average).Average),
		(($ftRows | ForEach-Object { $_.Ft.P50 } | Measure-Object -Average).Average),
		(($ftRows | ForEach-Object { $_.Ft.P99 } | Measure-Object -Average).Average),
		(($ftRows | ForEach-Object { $_.Ft.P999 } | Measure-Object -Average).Average),
		$worst.Ft.Worst, $worst.Seed, $worst.Ft.WorstAt)
	Write-Host ("stutters per 1000 frames: {0:n2} over 16.7ms, {1:n2} over 33.3ms  ({2} and {3} in {4:n0} frames)" -f
		(1000.0 * $totOver16 / $totFt), (1000.0 * $totOver33 / $totFt), $totOver16, $totOver33, $totFt)
}

# THREADING-ROADMAP.md 3.1's safety net. Present in every build, so a run that quietly started
# allocating inside a job says so here rather than six weeks later as a perf regression.
$jobRows = @($rows | Where-Object { $null -ne $_.Jobs })
if ($jobRows.Count -gt 0) {
	$workers = ($jobRows[0].Jobs.Workers)
	$allocs = ($jobRows | ForEach-Object { $_.Jobs.Allocs } | Measure-Object -Sum).Sum
	Write-Host ("job pool: {0} worker threads, {1} allocations from a job{2}" -f
		$workers, $allocs, $(if ($allocs -gt 0) { "  <== must be 0, see THREADING-ROADMAP.md 1.1" } else { "" }))
}

# THREADING-ROADMAP.md section 0. Only a PERF_TIMERS build fills these in; the table is what
# decides whether anything below section 3 is worth threading at all.
$perfRows = @($rows | Where-Object { $null -ne $_.Perf -and $_.Perf.Count -gt 0 })
if ($perfRows.Count -gt 0) {
	$names = $perfRows | ForEach-Object { $_.Perf.Keys } | Sort-Object -Unique
	$table = foreach ($n in $names) {
		$mine = @($perfRows | Where-Object { $_.Perf.ContainsKey($n) } | ForEach-Object { $_.Perf[$n] })
		[pscustomobject]@{
			Scope        = $n
			"us/frame"   = [math]::Round(($mine | Measure-Object Net -Average).Average, 1)
			"gross"      = [math]::Round(($mine | Measure-Object Gross -Average).Average, 1)
			"calls/frame"= [math]::Round(($mine | Measure-Object Calls -Average).Average, 2)
			# the stability column: a scope averaging nothing whose worst frame is 20ms is a stutter
			"worst us"   = [math]::Round(($mine | Measure-Object Worst -Maximum).Maximum, 0)
		}
	}
	Write-Host "per-frame cost by scope (net microseconds, averaged over the batch):"
	$table | Sort-Object -Property "us/frame" -Descending | Format-Table -AutoSize | Out-String | Write-Host
}

# the per-match detail, for whoever wants to look at one game rather than the average
$csv = Join-Path $RunDir "$Tag-batch.csv"
$rows | Select-Object Seed, Cells, Why, Frames, Wall,
	@{n="Winner";e={ $_.Winner }},
	@{n="ValueLostKilled";e={ $v = $_.Value; if ($v) { ($v.Keys | Sort-Object | ForEach-Object { "{0}:{1}/{2}" -f $_, $v[$_].Lost, $v[$_].Killed }) -join " " } }},
	@{n="Searches";e={ if ($_.Pf) { $_.Pf.Finds } }},
	@{n="SearchMs";e={ if ($_.Pf) { [math]::Round($_.Pf.FindMs, 1) } }},
	@{n="Expands";e={ if ($_.Pf) { $_.Pf.Expands } }},
	@{n="NoPath";e={ if ($_.Pf) { $_.Pf.NoPath } }},
	@{n="OutOfCells";e={ if ($_.Pf) { $_.Pf.OutOfCells } }},
	@{n="Blocked";e={ if ($_.Pf) { $_.Pf.Blocked } }},
	@{n="Stuck";e={ if ($_.Pf) { $_.Pf.Stuck } }},
	@{n="Wedge";e={ if ($_.Pf) { $_.Pf.Wedge } }},
	@{n="WedgeWorst";e={ if ($_.Pf) { $_.Pf.WedgeWorst } }},
	@{n="Escapes";e={ if ($_.Pf) { $_.Pf.Escapes } }},
	@{n="Repaths";e={ if ($_.Pf) { $_.Pf.Repaths } }},
	@{n="Dithers";e={ if ($_.Pf) { $_.Pf.Dithers } }} | Export-Csv -Path $csv -NoTypeInformation
Write-Host "per-match rows: $csv"
