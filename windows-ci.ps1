#	Copyright 2026 İlyas Akın
#	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#
#	This program is free software: you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation, either version 3 of the License, or
#	(at your option) any later version.
#
#	This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
#	You should have received a copy of the GNU General Public License
#	along with this program.  If not, see <http://www.gnu.org/licenses/>.
<#
.SYNOPSIS
  The Windows check for a merge, in one command: build, the ctest suite, the GPU tests, and E1's replay CRCs.

.DESCRIPTION
  The Windows counterpart of the port's post-merge checks on macOS and Linux (W2). In order:
    1. build.bat -Config (skip with -SkipBuild), then ZH_GAME_DATA set on the build tree when -DataDir is given;
    2. ctest, minus the audio and video tests (no sound on a CI machine) and, in session 0, minus the GPU tests;
    3. the desktop part: the GPU tests, then replay-check.ps1 for every E1 run at once, each on its own farm of
       the data with its own user data folder (ZH_USER_DATA_DIR, which Windows honours too). The runs
       are -Seeds at -MaxFrames, or -Runs "seed@frames" pairs when the seeds need different lengths.

  Session 0 (an ssh or service session) has no display, so no Direct3D device, and Windows -headless still
  makes one. Started there, the script runs its desktop part again in the logged-on user's interactive
  session through a one-off scheduled task (schtasks /it), waits for it, reads its result and deletes the
  task. Started on the desktop, it runs that part itself. Either way it needs a user logged on to a desktop.
  The task runs its wrapper in the classic console (conhost.exe), never the default terminal: Windows 11
  hands a console started in the desktop session to Windows Terminal, and one left running from earlier
  swallowed every such console, so the desktop part never started (a gate waited 90 minutes on it).

  RULE 9: the game never runs in the data folder. Each run runs in -WorkDir\farm<n>: a symbolic link to every file
  of -DataDir\zerohour, with the build's GeneralsMD\Run over them (the exe and DLLs copied), as Windows'
  one-folder layout has the fork's files over the install. Every file of the data folder is hashed before
  the farm is made and again after the runs, and a difference fails the check. Symbolic links need an
  elevated session or Developer Mode.

  -ExpectCrc "0@1200:0x81C18E15","1@12000:0x7F5B73D4" fails the check when a run's recorded CRC is another
  one: that is the cross-platform comparison ("seed:crc" means that seed at -MaxFrames). Without it the
  check only asks that each replay plays back to the same world. Pin -ExpectCrc to the commit the numbers
  were made on: upstream gameplay data moves them.

  -Bundle <file> -Ref <branch> first fetches that branch from a git bundle into this worktree and checks it
  out (detached), so a merge can be checked from another machine: make the bundle there, copy it here, run
  this. -Ref alone checks out a ref the worktree already has. PowerShell parsed this script before it ran,
  so the process that checked out keeps running the OLD version: it hands over at once to the checked-out
  one, with the same arguments, and exits with its status.

.EXAMPLE
  .\windows-ci.ps1 -DataDir D:\ZeroHourData -Runs "0@1200","0@12000","1@12000" -ExpectCrc "0@1200:0x81C18E15","0@12000:0x4452FB85","1@12000:0x7F5B73D4"
    (the E1 pins PORTING.md gives; upstream gameplay data changes move them. These frames and CRCs were
    measured at a 30 Hz logic tick and GAME_SPEED_PERCENT 150, 40 s and 400 s of game time: re-measure
    them whenever LOGICFRAMES_PER_SECOND or GAME_SPEED_PERCENT in GameCommon.h changes. At speed 100 the
    pins are the older 0x87792035, 0xAF2D1FDD and 0x084F009C)
  .\windows-ci.ps1 -DataDir D:\ZeroHourData -SkipBuild -Seeds 0 -MaxFrames 1200
  .\windows-ci.ps1 -Bundle D:\bundles\branch.bundle -Ref my-branch -DataDir D:\ZeroHourData

  Exit status: 0 when everything passed, 1 otherwise. The summary says which part failed.
#>
param(
	# a folder holding zerohour\ (a Zero Hour install) and generals\ (the base game); without it the data
	# tests skip and E1 does not run
	[string] $DataDir = "",
	[string] $Config = "Release",
	[switch] $SkipBuild,
	[string[]] $Seeds = @("0", "1"),
	# logic frames each E1 run plays; 0 is 400 s of game time at LOGICFRAMES_PER_SECOND from GameCommon.h
	[int] $MaxFrames = 0,
	# "seed@frames" pairs, in place of -Seeds at -MaxFrames
	[string[]] $Runs = @(),
	# "seed:0xCRC" pairs the recorded CRCs must equal
	[string[]] $ExpectCrc = @(),
	# check out this ref first: from -Bundle when one is given, otherwise one this worktree already has
	[string] $Bundle = "",
	[string] $Ref = "",
	[string] $WorkDir = (Join-Path $env:TEMP "zh-windows-ci"),
	# the game's platform, x64 or ARM64; empty is this machine's own.  An ARM64 machine given x64 cross-builds
	# the x64 game (build.bat's ZH_PLATFORM) and runs its tests and E1 under Windows' x64 emulation
	[ValidateSet("", "x64", "ARM64")] [string] $Platform = "",
	# a folder put in front of PATH for everything this runs (the tests, the desktop part, E1): the x64 lane's
	# d3dx9_43.dll on an ARM64 machine, which has none of its own, so nothing is installed system-wide
	[string] $DllPath = "",
	# a program that prints the full path of the DLL it loads by the same search; its answer and that file's
	# SHA-256 go in the summary, before the tests and again from the desktop part, so the DLL used is on record
	[string] $DllProbe = "",
	# internal: "desktop" when the script is running its desktop part, and where that part writes its result
	[string] $Phase = "all",
	[string] $CheckedOut = "",	# internal: what the -Bundle/-Ref hand-over checked out, for the summary
	[string] $ResultFile = ""
)

# Continue, not Stop: Windows PowerShell 5.1 turns a native command's stderr, under 2>&1, into a terminating
# error, and ctest writes "Errors while running CTest" there. Failures are counted by exit status instead.
$ErrorActionPreference = "Continue"
$env:ZH_UNATTENDED = "1"	# every game this starts is unattended: no box may wait on a person (EarlyCommandLine.h)
if ($MaxFrames -le 0) {
	# the logic rate, from the engine's own header so this script never carries a copy of it
	$logicFps = [int][regex]::Match((Get-Content -Raw "$PSScriptRoot\GeneralsMD\Code\GameEngine\Include\Common\GameCommon.h"),
		'(?m)^\s*LOGICFRAMES_PER_SECOND\s*=\s*(\d+)').Groups[1].Value
	if ($logicFps -le 0) { throw "no LOGICFRAMES_PER_SECOND in GameCommon.h" }
	$MaxFrames = 400 * $logicFps
}
# through powershell -File an array arrives as one comma-joined string: -Seeds, -Runs and -ExpectCrc alike
$Seeds = @($Seeds | ForEach-Object { $_.Split(',') } | Where-Object { $_ -ne "" } | ForEach-Object { [int]$_ })
# the E1 runs as "seed@frames"
$Runs = @($Runs | ForEach-Object { $_.Split(',') } | Where-Object { $_ -ne "" })
if ($Runs.Count -eq 0) { $Runs = @($Seeds | ForEach-Object { "$_@$MaxFrames" }) }
$ExpectCrc = @($ExpectCrc | ForEach-Object { $_.Split(',') } | Where-Object { $_ -ne "" } |
	ForEach-Object { $k, $v = $_.Split(':', 2); if ($k -notmatch '@') { $k = "$k@$MaxFrames" }; "${k}:$v" })
$Root = $PSScriptRoot
# build.bat's folder: ARM64 builds into build-arm64 and x64 into build64; the platform is -Platform's, which
# build.bat reads from ZH_PLATFORM, or else the machine's own
if ($Platform -ne "") { $env:ZH_PLATFORM = $Platform }
if ($DllPath -ne "") { $env:PATH = "$DllPath;$env:PATH" }
$arm64 = if ($Platform -ne "") { $Platform -eq "ARM64" } else { $env:PROCESSOR_ARCHITECTURE -eq "ARM64" }
$Build = Join-Path $Root $(if ($arm64) { "build-arm64" } else { "build64" })
$RunDir = Join-Path $Root "GeneralsMD\Run"
$NoSound = "test_milesaudiomanager|miles_smoke|test_miles_miniaudio|test_binkvideo|bink_smoke"
$GpuTests = "^(dx9_smoke|dx9_smoke_msaa|test_dx11device)$"

function Get-CtestExe {
	$cache = Join-Path $Build "CMakeCache.txt"
	$line = Select-String -Path $cache -Pattern '^CMAKE_COMMAND:INTERNAL=(.+)$' | Select-Object -First 1
	if (-not $line) { throw "no CMAKE_COMMAND in ${cache}: build first" }
	$cmake = $line.Matches[0].Groups[1].Value
	return @{ CMake = $cmake; CTest = (Join-Path (Split-Path $cmake) "ctest.exe") }
}

# Build outputs dated more than five minutes ahead of the clock, deleted, and how many.  A VM whose clock
# ran fast (7 h, until RealTimeIsUniversal) stamped its outputs in the future; once the clock was put right,
# MSBuild took them for newer than any edited source and rebuilt nothing, and a Debug round tested a stale
# generals.exe.  Deleting them makes the build redo exactly those.
# Files git tracks are never touched: a checkout made while the clock ran fast dates them in the future too,
# and Run\ holds some (BrowserEngine.dll, which dx8webbrowser.cpp #imports; a sweep of Run\ on a Windows
# ARM64 VM deleted it and the next build failed).  If git cannot list them, nothing is deleted and -1 comes
# back.
function Remove-FutureOutputs([string] $root, [string[]] $dirs) {
	$limit = (Get-Date).AddMinutes(5)
	$present = @($dirs | Where-Object { Test-Path -LiteralPath $_ })
	if ($present.Count -eq 0) { return 0 }
	# git refuses a path outside the work tree, and nothing outside it is tracked
	$top = [System.IO.Path]::GetFullPath($root).TrimEnd('\') + '\'
	$inside = @($present | Where-Object { [System.IO.Path]::GetFullPath($_).StartsWith($top, [StringComparison]::OrdinalIgnoreCase) })
	$listed = @()
	if ($inside.Count -gt 0) {
		$listed = @(git -C $root -c core.quotepath=off ls-files -- $inside)
		if ($LASTEXITCODE -ne 0) { return -1 }
	}
	$tracked = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
	$listed | ForEach-Object { [void]$tracked.Add([System.IO.Path]::GetFullPath((Join-Path $root $_))) }
	$future = @($present | ForEach-Object {
		Get-ChildItem -LiteralPath $_ -Recurse -File -Force -ErrorAction SilentlyContinue } |
		Where-Object { $_.LastWriteTime -gt $limit -and -not $tracked.Contains($_.FullName) })
	$future | ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue }
	return $future.Count
}

# every file of a folder: its relative path, size and SHA-256, sorted - equal listings mean an equal folder
function Get-TreeListing([string] $dir) {
	Get-ChildItem -LiteralPath $dir -Recurse -File -Force | Sort-Object FullName | ForEach-Object {
		"{0} {1} {2}" -f $_.FullName.Substring($dir.Length + 1), $_.Length, (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
	}
}

# A symbolic link where the token may make one, else a hard link: the desktop part runs with the signed-in
# user's token, which under UAC cannot create symbolic links (the ARM64 VM), and a farm without its links
# only shows the game's missing-install dialog.  A hard link needs no privilege and keeps what the farm is
# for: deleting it (GameEngine::init deletes INIZH.big) leaves the data.  Neither is a failure, loudly.
function New-FarmLink([string] $path, [string] $target) {
	try { New-Item -ItemType SymbolicLink -Path $path -Target $target -ErrorAction Stop | Out-Null }
	catch { New-Item -ItemType HardLink -Path $path -Target $target -ErrorAction Stop | Out-Null }
}

function New-Farm([string] $data, [string] $farm) {
	if (Test-Path $farm) { cmd /c "rmdir /s /q `"$farm`"" | Out-Null }		# links go, never their targets
	New-Item -ItemType Directory $farm | Out-Null
	Get-ChildItem -LiteralPath $data -Recurse -File | Where-Object { $_.Name -notlike '._*' } | ForEach-Object {
		$dst = Join-Path $farm $_.FullName.Substring($data.Length + 1)
		New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
		New-FarmLink $dst $_.FullName
	}
	Get-ChildItem -LiteralPath $RunDir -Recurse -File | Where-Object { $_.Extension -ne '.pdb' } | ForEach-Object {
		$dst = Join-Path $farm $_.FullName.Substring($RunDir.Length + 1)
		New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
		if (Test-Path -LiteralPath $dst) { Remove-Item -LiteralPath $dst -Force }	# the link, never its target
		if ($_.Extension -in '.exe', '.dll') { Copy-Item -LiteralPath $_.FullName $dst }
		else { New-FarmLink $dst $_.FullName }
	}
}

# ---- the desktop part: GPU tests and E1, returning a result object -------------------------------------
# what -DllProbe says loads, and its SHA-256
function Get-DllProbe {
	$where = "$(& $DllProbe 2>&1 | Select-Object -First 1)".Trim()
	if (Test-Path -LiteralPath $where -PathType Leaf) { return "$where, sha256 $((Get-FileHash -Algorithm SHA256 -LiteralPath $where).Hash)" }
	return "none loaded ($where)"
}

function Invoke-DesktopPart {
	$r = [ordered]@{ Gpu = "not run"; E1 = "not run"; Crcs = @{}; DataUnchanged = $null; Log = @(); Dll = "" }
	if ($DllProbe -ne "") { $r.Dll = Get-DllProbe }
	$tools = Get-CtestExe
	Push-Location $Build
	$gpu = & $tools.CTest -C $Config -R $GpuTests --output-on-failure 2>&1
	Pop-Location
	$r.Log += $gpu
	$r.Gpu = if ($LASTEXITCODE -eq 0) { "passed" } else { "FAILED" }

	if ($DataDir -ne "") {
		$zh = Join-Path $DataDir "zerohour"
		New-Item -ItemType Directory -Force $WorkDir | Out-Null
		$before = @(Get-TreeListing $zh)
		# E1's runs side by side: each its own farm and its own user data folder (ZH_USER_DATA_DIR, through
		# replay-check.ps1 -UserDataDir), since every recording is written to Replays\00000000.rep and the
		# game deletes Data\INI\INIZH.big from its root.  Each is its own powershell, hidden; the results
		# are read back in the runs' order, so the log reads as when they ran one after another.
		$started = @()
		$single = Join-Path $WorkDir "farm"		# the one farm the runs shared before; its links go, never their targets
		if (Test-Path $single) { cmd /c "rmdir /s /q `"$single`"" | Out-Null }
		for ($i = 0; $i -lt $Runs.Count; $i++) {
			$farm = Join-Path $WorkDir "farm$i"
			try { New-Farm $zh $farm }
			catch { $r.E1 = "FAILED (could not make the farm: $($_.Exception.Message))"; return $r }
			$user = Join-Path $WorkDir "user$i"
			if (Test-Path $user) { cmd /c "rmdir /s /q `"$user`"" | Out-Null }
			$seed, $frames = $Runs[$i].Split('@')
			$outFile = Join-Path $WorkDir "e1-run$i.log"
			$argList = "-NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $Root 'replay-check.ps1')`" -RunDir `"$farm`" " +
				"-UserDataDir `"$user`" -Seeds $([int]$seed) -MaxFrames $([int]$frames)"
			$p = Start-Process powershell.exe -ArgumentList $argList -WindowStyle Hidden -PassThru `
				-RedirectStandardOutput $outFile -RedirectStandardError "$outFile.err"
			$null = $p.Handle		# kept, so ExitCode is still there after the exit
			$started += [pscustomobject]@{ Run = $Runs[$i]; Process = $p; Out = $outFile }
		}
		$failures = 0
		foreach ($s in $started) {
			$s.Process.WaitForExit()
			$failures += $s.Process.ExitCode
			$out = @(Get-Content $s.Out, "$($s.Out).err" -ErrorAction SilentlyContinue | ForEach-Object { "$_" })
			$r.Log += $out
			foreach ($line in $out) {
				if ($line -match 'frame (\d+), CRC (0x[0-9A-Fa-f]+)') { $r.Crcs[$s.Run] = $Matches[2]; break }
			}
		}
		$r.Log | Out-File -Encoding utf8 (Join-Path $WorkDir "e1.log")
		$r.E1 = if ($failures -eq 0 -and $r.Crcs.Count -eq $Runs.Count) { "played back the same" } else { "FAILED (the output: $(Join-Path $WorkDir 'e1.log'))" }
		$after = @(Get-TreeListing $zh)
		$r.DataUnchanged = ($before.Count -gt 0) -and (($before -join "`n") -eq ($after -join "`n"))
	}
	return $r
}

if ($Phase -eq "desktop") {
	# written whole under another name, then renamed: the main part waits for this name, and Out-File would
	# create it empty at the start of the pipeline
	$result = Invoke-DesktopPart
	# what started this part, for the record: through the task, conhost then cmd, never the default terminal
	$chain = @(); $id = $PID
	for ($i = 0; $i -lt 5 -and $id; $i++) {
		$p = Get-CimInstance Win32_Process -Filter "ProcessId = $id" -ErrorAction SilentlyContinue
		if (-not $p) { break }
		$chain += $p.Name; $id = $p.ParentProcessId
	}
	$result.Parents = $chain -join ' < '
	$json = $result | ConvertTo-Json -Depth 4
	$json | Out-File -Encoding utf8 "$ResultFile.partial"
	Move-Item -Force "$ResultFile.partial" $ResultFile
	exit 0
}

# ---- the main part --------------------------------------------------------------------------------------
$summary = @()
$failed = $false

if ($Bundle -ne "" -or $Ref -ne "") {
	if ($Ref -eq "") { Write-Host "-Bundle needs -Ref, the branch to take from it"; exit 1 }
	if ($Bundle -ne "") {
		git -C $Root fetch -q -f $Bundle "${Ref}:refs/remotes/ci/${Ref}"
		if ($LASTEXITCODE -ne 0) { Write-Host "could not fetch $Ref from $Bundle"; exit 1 }
		$target = "refs/remotes/ci/${Ref}"
	} else { $target = $Ref }
	git -C $Root checkout -q --detach $target
	if ($LASTEXITCODE -ne 0) { Write-Host "could not check out $target (uncommitted changes in $Root?)"; exit 1 }
	# hand over to the version just checked out (this process runs the one it was started as)
	$again = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath)
	foreach ($k in $PSBoundParameters.Keys) {
		if ($k -in 'Bundle', 'Ref') { continue }
		$v = $PSBoundParameters[$k]
		if ($v -is [System.Management.Automation.SwitchParameter]) { if ($v.IsPresent) { $again += "-$k" } }
		else { $again += "-$k"; $again += (@($v) -join ',') }
	}
	$again += '-CheckedOut'; $again += ("`"" + (git -C $Root log --oneline -1) + "`"")
	& powershell.exe @again
	exit $LASTEXITCODE
}
if ($CheckedOut -ne "") { $summary += "checked out: $CheckedOut" }
if ($Platform -ne "") { $summary += "platform: $Platform, on an $($env:PROCESSOR_ARCHITECTURE) machine" }

if (-not $SkipBuild) {
	$futureOutputs = Remove-FutureOutputs $Root @($Build, $RunDir)
	if ($futureOutputs -lt 0) { $summary += "build: git could not list the tracked files, so no output dated in the future was deleted" }
	if ($futureOutputs -gt 0) { $summary += "build: deleted $futureOutputs output(s) dated in the future (a clock that ran fast); they are rebuilt" }
	# The last build's exe goes first: a build that fails must leave nothing the desktop part could run.
	Remove-Item (Join-Path $RunDir "generals.exe") -ErrorAction SilentlyContinue
	Push-Location $Root
	# not $phase: PowerShell names ignore case, and that would be this script's -Phase, a [string].  A
	# stopwatch, not Get-Date: the VM's wall clock was once stepped back 7 h in the middle of a gate.
	$phaseClock = [Diagnostics.Stopwatch]::StartNew()
	cmd /c "build.bat $Config < NUL" | Tee-Object -Variable buildOut | Out-Host
	$built = $LASTEXITCODE
	Pop-Location
	# how much it rebuilt: MSBuild names each source it compiles on a line of its own
	$compiled = @($buildOut | Where-Object { "$_" -match '^\s+[\w\.\-]+\.(cpp|c|cc|cxx)$' }).Count
	$took = "$([int]$phaseClock.Elapsed.TotalSeconds) s, $compiled source(s) compiled"
	$summary += "build: " + $(if ($built -eq 0) { "ok ($took)" } else { $failed = $true; "FAILED (exit $built; $took)" })
	if ($built -ne 0) {
		$summary += "ctest, GPU tests: not run (build failed)"
		if ($DataDir -ne "") { $summary += "E1: not run (build failed)" }
		Write-Host ""
		$summary | ForEach-Object { Write-Host $_ }
		Write-Host "WINDOWS CHECK FAILED"
		exit 1
	}
}
$tools = Get-CtestExe
if ($DllProbe -ne "") { $summary += "dll probe (tests): $(Get-DllProbe)" }
if ($DataDir -ne "") { & $tools.CMake -S (Join-Path $Root "GeneralsMD\Code") -B $Build "-DZH_GAME_DATA=$DataDir" | Out-Null }

$session0 = (Get-Process -Id $PID).SessionId -eq 0
Push-Location $Build
$exclude = if ($session0) { "$NoSound|dx9_smoke|dx9_smoke_msaa|test_dx11device" } else { $NoSound }
$phaseClock = [Diagnostics.Stopwatch]::StartNew()
$ctestOut = & $tools.CTest -C $Config -j4 --timeout 900 --output-on-failure -E $exclude 2>&1
$ctestTook = "$([int]$phaseClock.Elapsed.TotalSeconds) s"
$ctestExit = $LASTEXITCODE
Pop-Location
New-Item -ItemType Directory -Force $WorkDir | Out-Null
$ctestOut | ForEach-Object { "$_" } | Out-File -Encoding utf8 (Join-Path $WorkDir "ctest.log")		# the failing tests' own output
$summary += "ctest: " + $(if ($ctestExit -eq 0) { "passed ($ctestTook)" } else { $failed = $true; "FAILED ($ctestTook; the output: $(Join-Path $WorkDir 'ctest.log'))" })
$ctestOut | Select-String -Pattern 'tests passed|\*\*\*' | ForEach-Object { $summary += "  " + $_.Line.Trim() }

# the desktop part: here, or in the interactive session through a one-off task
$phaseClock = [Diagnostics.Stopwatch]::StartNew()
New-Item -ItemType Directory -Force $WorkDir | Out-Null
$resultFile = Join-Path $WorkDir "desktop-result.json"
Remove-Item $resultFile, "$resultFile.partial" -ErrorAction SilentlyContinue
$argList = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Phase desktop -ResultFile `"$resultFile`" -Config $Config -MaxFrames $MaxFrames -WorkDir `"$WorkDir`" -Runs $($Runs -join ',')"
if ($DataDir -ne "") { $argList += " -DataDir `"$DataDir`"" }
if ($Platform -ne "") { $argList += " -Platform $Platform" }
if ($DllPath -ne "") { $argList += " -DllPath `"$DllPath`"" }
if ($DllProbe -ne "") { $argList += " -DllProbe `"$DllProbe`"" }
if ($session0) {
	$user = (Get-CimInstance Win32_ComputerSystem).UserName
	if (-not $user) { Write-Host "no user is logged on to a desktop: the GPU tests and E1 cannot run"; exit 1 }
	$task = "zh-windows-ci-" + [guid]::NewGuid().ToString("N").Substring(0, 8)
	# schtasks /tr takes at most 261 characters, so the task runs a one-line wrapper, in the classic console named
	# outright: left to the default, Windows 11 hands it to Windows Terminal (see the description above)
	$wrapper = Join-Path $WorkDir "desktop-part.cmd"
	"@powershell.exe $argList" | Out-File -Encoding ascii $wrapper
	# No quotes inside it: Windows PowerShell passes a program an argument with spaces in quotes of its own, without
	# escaping any inside, so the paths must have no spaces (the work folder's is checked)
	if ("$env:SystemRoot$wrapper" -match ' ') { Write-Host "the desktop part's task cannot run $wrapper (a space in the path): a -WorkDir without one"; exit 1 }
	$taskRun = "$env:SystemRoot\System32\conhost.exe $env:SystemRoot\System32\cmd.exe /c $wrapper"
	if ($taskRun.Length -gt 261) { Write-Host "the desktop part's task line is $($taskRun.Length) characters, over schtasks' 261: a shorter -WorkDir"; exit 1 }
	schtasks /create /tn $task /tr $taskRun /sc once /st 23:59 /it /ru $user /f | Out-Null
	schtasks /run /tn $task | Out-Null
	$waitClock = [Diagnostics.Stopwatch]::StartNew()		# not the wall clock: a VM's can step by hours
	while (-not (Test-Path $resultFile) -and $waitClock.Elapsed.TotalMinutes -lt 90) { Start-Sleep -Seconds 10 }
	Start-Sleep -Seconds 2
	schtasks /delete /tn $task /f | Out-Null
} else {
	# each only when given: Windows PowerShell drops an empty argument to a program, which would leave its name bare
	$platformArgs = @(if ($Platform -ne "") { "-Platform", $Platform }) + @(if ($DllPath -ne "") { "-DllPath", $DllPath }) +
		@(if ($DllProbe -ne "") { "-DllProbe", $DllProbe })
	& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Phase desktop -ResultFile $resultFile -Config $Config -MaxFrames $MaxFrames -WorkDir $WorkDir -Runs ($Runs -join ',') -DataDir $DataDir @platformArgs
}
$desktopTook = "$([int]$phaseClock.Elapsed.TotalSeconds) s"
if (-not (Test-Path $resultFile)) {
	$summary += "desktop part: NO RESULT (it did not finish, $desktopTook)"; $failed = $true
} else {
	$summary += "desktop part (GPU tests and E1): $desktopTook"
	$d = Get-Content -Raw $resultFile | ConvertFrom-Json
	if ($d.Parents) { $summary += "desktop part started by: $($d.Parents)" }
	if ($d.Dll) { $summary += "dll probe (desktop part): $($d.Dll)" }
	$summary += "GPU tests (desktop session): $($d.Gpu)"; if ($d.Gpu -ne "passed") { $failed = $true }
	if ($DataDir -ne "") {
		$summary += "E1: $($d.E1)"; if ($d.E1 -ne "played back the same") { $failed = $true }
		foreach ($run in $Runs) { $s, $f = $run.Split('@'); $summary += "  seed $s at frame ${f}: $($d.Crcs.$run)" }
		foreach ($pair in $ExpectCrc) {
			$run, $want = $pair.Split(':', 2)
			$got = $d.Crcs."$run"
			if ($got -ne $want) { $summary += "  ${run}: EXPECTED $want, got $got"; $failed = $true }
			else { $summary += "  ${run}: as expected" }
		}
		$summary += "data folder: " + $(if ($d.DataUnchanged) { "unchanged" } else { $failed = $true; "CHANGED OR NOT CHECKED" })
	}
}
Write-Host ""
$summary | ForEach-Object { Write-Host $_ }
Write-Host $(if ($failed) { "WINDOWS CHECK FAILED" } else { "WINDOWS CHECK PASSED" })
exit $(if ($failed) { 1 } else { 0 })
