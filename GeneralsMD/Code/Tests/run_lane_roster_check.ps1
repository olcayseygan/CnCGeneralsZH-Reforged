# What each of the twelve armies can buy in Direct Strike (LaneWar_roster, LaneWarDirector.cpp). Two
# headless 3v3 lane matches seat all twelve with -side, and every seat's "LANEWAR: player N <side> roster
# of K: ..." line, written once at match start, must read as below, in this order. The lists are the
# generals' own factory command sets on the shipped data: a Paladin for the Laser General or a Hijacker
# for the Toxin General fails it, and so does a general's unit falling back to the base side's template.
#
#   run_lane_roster_check.ps1 -Exe <generals.exe>        exit 0 pass, 1 a roster differs, 77 no game data
#
# The exe is copied into Run/ under a name of its own, so the check never holds the file a build copies
# over, and the two logs are deleted after they are read.
param(
	[Parameter(Mandatory = $true)] [string] $Exe,
	[string] $RunDir = ""
)
$ErrorActionPreference = "Stop"
# Windows PowerShell 5.1 has no $PSScriptRoot yet while it binds the parameters
if (-not $RunDir) { $RunDir = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "..\..\Run" }

$expected = [ordered]@{
	"America"                   = "AmericaInfantryRanger AmericaInfantryMissileDefender AmericaInfantryPathfinder AmericaVehicleHumvee AmericaTankCrusader AmericaVehicleSentryDrone AmericaTankMicrowave AmericaVehicleTomahawk AmericaTankPaladin AmericaVehicleComanche AmericaJetRaptor AmericaJetStealthFighter AmericaJetAurora AmericaTankAvenger AmericaInfantryColonelBurton"
	"AmericaAirForceGeneral"    = "AirF_AmericaInfantryRanger AirF_AmericaInfantryMissileDefender AirF_AmericaInfantryPathfinder AirF_AmericaVehicleHumvee AirF_AmericaVehicleSentryDrone AirF_AmericaTankMicrowave AirF_AmericaVehicleTomahawk AirF_AmericaVehicleComanche AirF_AmericaJetRaptor AirF_AmericaJetStealthFighter AirF_AmericaJetAurora AirF_AmericaTankAvenger AirF_AmericaInfantryColonelBurton"
	"AmericaLaserGeneral"       = "Lazr_AmericaInfantryRanger Lazr_AmericaInfantryMissileDefender Lazr_AmericaInfantryPathfinder Lazr_AmericaVehicleHumvee Lazr_AmericaTankCrusader Lazr_AmericaVehicleSentryDrone Lazr_AmericaTankMicrowave Lazr_AmericaVehicleComanche Lazr_AmericaJetRaptor Lazr_AmericaJetStealthFighter Lazr_AmericaJetAurora Lazr_AmericaTankAvenger Lazr_AmericaInfantryColonelBurton"
	"AmericaSuperWeaponGeneral" = "SupW_AmericaInfantryRanger SupW_AmericaInfantryMissileDefender SupW_AmericaInfantryPathfinder SupW_AmericaVehicleHumvee SupW_AmericaVehicleSentryDrone SupW_AmericaTankMicrowave SupW_AmericaVehicleTomahawk SupW_AmericaVehicleComanche SupW_AmericaJetRaptor SupW_AmericaJetStealthFighter SupW_AmericaJetAurora SupW_AmericaTankAvenger SupW_AmericaInfantryColonelBurton"
	"China"                     = "ChinaInfantryRedguard ChinaInfantryTankHunter ChinaTankBattleMaster ChinaTankGattling ChinaTankDragon ChinaVehicleTroopCrawler ChinaTankECM ChinaVehicleInfernoCannon ChinaVehicleListeningOutpost ChinaVehicleHelix ChinaInfantryHacker ChinaJetMIG ChinaTankOverlord ChinaVehicleNukeLauncher ChinaInfantryBlackLotus"
	"ChinaTankGeneral"          = "Tank_ChinaInfantryRedguard Tank_ChinaInfantryTankHunter Tank_ChinaTankBattleMaster Tank_ChinaTankGattling Tank_ChinaTankDragon Tank_ChinaVehicleTroopCrawler Tank_ChinaTankECM Tank_ChinaVehicleListeningOutpost Tank_ChinaVehicleHelix Tank_ChinaInfantryHacker Tank_ChinaJetMIG Tank_ChinaTankEmperor Tank_ChinaInfantryBlackLotus"
	"ChinaInfantryGeneral"      = "Infa_ChinaInfantryMiniGunner Infa_ChinaInfantryTankHunter Infa_ChinaTankDragon Infa_ChinaVehicleTroopCrawler Infa_ChinaTankECM Infa_ChinaVehicleInfernoCannon Infa_ChinaVehicleListeningOutpost Infa_ChinaVehicleHelix Infa_ChinaInfantryHacker Infa_ChinaJetMIG Infa_ChinaVehicleNukeLauncher Infa_ChinaInfantryBlackLotus"
	"ChinaNukeGeneral"          = "Nuke_ChinaInfantryRedguard Nuke_ChinaInfantryTankHunter Nuke_ChinaTankBattleMaster Nuke_ChinaTankGattling Nuke_ChinaTankDragon Nuke_ChinaVehicleTroopCrawler Nuke_ChinaTankECM Nuke_ChinaVehicleInfernoCannon Nuke_ChinaVehicleListeningOutpost Nuke_ChinaVehicleHelix Nuke_ChinaInfantryHacker Nuke_ChinaJetMIG Nuke_ChinaTankOverlord Nuke_ChinaVehicleNukeLauncher Nuke_ChinaInfantryBlackLotus"
	"GLA"                       = "GLAInfantryRebel GLAInfantryTerrorist GLAInfantryTunnelDefender GLAVehicleTechnical GLAVehicleCombatBike GLATankScorpion GLAVehicleQuadCannon GLAVehicleToxinTruck GLAInfantryAngryMobNexus GLAInfantryHijacker GLATankMarauder GLAVehicleRocketBuggy GLAVehicleBombTruck GLAVehicleBattleBus GLAVehicleScudLauncher GLAInfantryJarmenKell"
	"GLAToxinGeneral"           = "Chem_GLAInfantryRebel Chem_GLAInfantryTerrorist Chem_GLAInfantryTunnelDefender Chem_GLAVehicleTechnical Chem_GLAVehicleCombatBike Chem_GLATankScorpion Chem_GLAVehicleQuadCannon Chem_GLAVehicleToxinTruck Chem_GLAInfantryAngryMobNexus Chem_GLATankMarauder Chem_GLAVehicleRocketBuggy Chem_GLAVehicleBombTruck Chem_GLAVehicleBattleBus Chem_GLAVehicleScudLauncher Chem_GLAInfantryJarmenKell"
	"GLAStealthGeneral"         = "Slth_GLAInfantryRebel Slth_GLAInfantryTerrorist Slth_GLAInfantryTunnelDefender Slth_GLAVehicleTechnical Slth_GLAVehicleCombatBike GLAVehicleCombatBikeTerrorist Slth_GLAVehicleQuadCannon Slth_GLAVehicleToxinTruck Slth_GLAInfantryAngryMobNexus Slth_GLAInfantryHijacker Slth_GLAVehicleRocketBuggy Slth_GLAVehicleBombTruck Slth_GLAVehicleBattleBus Slth_GLAInfantryJarmenKell"
	"GLADemolitionGeneral"      = "Demo_GLAInfantryRebel Demo_GLAInfantryTerrorist Demo_GLAInfantryTunnelDefender Demo_GLAVehicleTechnical Demo_GLAVehicleCombatBike Demo_GLATankScorpion Demo_GLAVehicleQuadCannon Demo_GLAVehicleToxinTruck Demo_GLAInfantryAngryMobNexus Demo_GLATankMarauder Demo_GLAVehicleRocketBuggy Demo_GLAVehicleBombTruck Demo_GLAVehicleBattleBus Demo_GLAVehicleScudLauncher Demo_GLAInfantryJarmenKell"
}

if (-not (Test-Path (Join-Path $RunDir "INIZH.big"))) { Write-Host "skip: no Zero Hour data in $RunDir"; exit 77 }
$RunDir = (Resolve-Path $RunDir).Path
$env:ZH_UNATTENDED = "1"
$copy = Join-Path $RunDir "generals_rostercheck.exe"
Copy-Item $Exe $copy -Force

# Each match's six seats, two teams of three; together they are every army once
$matches = @(
	@("FactionAmerica", "FactionChina", "FactionAmericaAirForceGeneral", "FactionChinaTankGeneral", "FactionAmericaLaserGeneral", "FactionChinaInfantryGeneral"),
	@("FactionAmericaSuperWeaponGeneral", "FactionGLA", "FactionChinaNukeGeneral", "FactionGLAToxinGeneral", "FactionGLAStealthGeneral", "FactionGLADemolitionGeneral")
)
$runs = @()
try {
	for ($m = 0; $m -lt $matches.Count; ++$m) {
		$prefix = "lanerostercheck$($m)_"
		$log = Join-Path $RunDir ($prefix + "DebugLogFile.txt")
		if (Test-Path $log) { Remove-Item $log -Force }
		$a = @("-headless", "-quickstart", "-noshellmap", "-observer", "-multiInstance", "-noFPSLimit",
			"-randommap", ($m + 1), "lane", "-autoskirmish", 6, "-seed", ($m + 1), "-maxframes", 60, "-logPrefix", $prefix)
		for ($s = 0; $s -lt 6; ++$s) { $a += @("-side", $s, $matches[$m][$s]) }
		$runs += [pscustomobject]@{ Log = $log; Proc = (Start-Process -FilePath $copy -ArgumentList $a -WorkingDirectory $RunDir -PassThru) }
	}
	foreach ($r in $runs) { if (-not $r.Proc.WaitForExit(600000)) { Stop-Process -Id $r.Proc.Id -Force } }
} finally {
	foreach ($r in $runs) { if (-not $r.Proc.HasExited) { Stop-Process -Id $r.Proc.Id -Force } }
}

$seen = @{}
foreach ($r in $runs) {
	if (Test-Path $r.Log) {
		foreach ($line in (Select-String -Path $r.Log -Pattern "LANEWAR: player \d+ (\S+) roster of \d+:(.*)$")) {
			$seen[$line.Matches[0].Groups[1].Value] = $line.Matches[0].Groups[2].Value.Trim()
		}
		Remove-Item $r.Log -Force
	}
}
Get-ChildItem $RunDir -Filter "lanerostercheck*" | Remove-Item -Force
Get-ChildItem $RunDir -Filter "generals_rostercheck.exe*" | Remove-Item -Force -ErrorAction SilentlyContinue

$failed = 0
foreach ($side in $expected.Keys) {
	if (-not $seen.ContainsKey($side)) { Write-Host "FAIL $($side): no roster line"; ++$failed; continue }
	$want = $expected[$side] -split " "
	$got = $seen[$side] -split " "
	if (($want -join " ") -ceq ($got -join " ")) { Write-Host "ok   $($side): $($got.Count) units"; continue }
	$extra = @($got | Where-Object { $want -notcontains $_ })
	$missing = @($want | Where-Object { $got -notcontains $_ })
	Write-Host "FAIL $($side): extra [$($extra -join ' ')] missing [$($missing -join ' ')]$(if (-not $extra -and -not $missing) { ' (order)' })"
	++$failed
}
if ($failed) { Write-Host "$failed of $($expected.Count) rosters differ"; exit 1 }
Write-Host "all $($expected.Count) rosters as pinned"
exit 0
