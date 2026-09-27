# Photographs the same frame of the same match with two builds and counts the pixels between them.
#
# It exists because a tree can go missing without anything in the engine looking wrong: the buffer
# fills, the draw call is made, and the trees are simply not on the screen.  Nothing but the picture
# catches that.  Run it with a reference build in Run\generals_base.exe - build one from a checkout
# without the change under test - and every view should come back under its own limit, all of the
# difference the tree shadows.  A stand of missing trees moves several percent and looks like nothing else.
#
#   .\tree-check.ps1                 # eight views over five maps, each against its own limit
#   .\tree-check.ps1 -Limit 4        # one limit for every view; a tree-dense map with long shadows reached ~3.7%
#
# Each view carries its own limit (lim in $cases) because the noise is not the same everywhere.  An
# identical build against itself on 2026-09-27 read 0.35% on ForgottenForestZH and 1.93% on Alpine
# Assault, and a harmless commit repeated every view within 0.03 points.  A single limit loose enough
# for Alpine Assault would let four times its noise through on the forest.  So each limit is that
# view's measured noise plus about 0.75 of a point, rounded up: a real drawing change still trips it,
# the run-to-run shimmer does not.  Re-measure and move them if the reference machine changes.
#
# Exit code is the number of views over the limit.
param([double]$Limit = 0)

Add-Type -AssemblyName System.Drawing
$run = Join-Path $PSScriptRoot "GeneralsMD\Run"
$shots = "$env:USERPROFILE\Documents\Command and Conquer Generals Zero Hour Data"
$tmp = Join-Path $env:TEMP "treecheck"
if (-not (Test-Path $tmp)) { $null = New-Item -ItemType Directory $tmp }

# -turbo cuts a view from about 90 seconds to about 25, but the cloud shadows scroll on the wall
# clock, so a turbo shot and a paced one of the same frame differ on 7% of the pixels.  Both builds
# have to run the same way: turbo when the reference build knows the switch, paced when it does not.
$baseExe = Join-Path $run 'generals_base.exe'
$pace = @()
if (Select-String -Path $baseExe -Pattern '-turbo' -SimpleMatch -Quiet) { $pace = @('-turbo') }
else { "generals_base.exe predates -turbo: both builds run paced, about 90 seconds a view" }

$cases = @(
  @{map='Flash Effect';       x='1200'; y='945';  f=400;  lim=1.6},   # noise 0.77
  @{map='Flash Effect';       x='1816'; y='1861'; f=1200; lim=2.3},   # noise 1.54
  @{map='Flash Effect';       x='1378'; y='1384'; f=2400; lim=2.0},   # noise 1.21
  @{map='ForgottenForestZH';  x='1620'; y='1470'; f=600;  lim=1.2},   # noise 0.37
  @{map='ForgottenForestZH';  x='1543'; y='1636'; f=1800; lim=1.1},   # noise 0.35
  @{map='Golden Oasis';       x='2643'; y='3580'; f=900;  lim=2.4},   # noise 1.64
  @{map='Alpine Assault';     x='760';  y='920';  f=700;  lim=2.7},   # noise 1.93
  @{map='Killing Fields';     x='1024'; y='1024'; f=1500; lim=1.7}    # noise 0.94
)

# -msaa 0 is not a preference, it is what makes the picture a picture of the game.  The screenshot
# reads the back buffer, and a multisampled back buffer cannot be read: the capture falls through to
# a desktop grab, and whatever window is sitting over the game is what lands in the .bmp.  A build
# from before that fallback was made to say so is silent about it.  Options.ini decides the sample
# count otherwise, so without this switch the answer depends on the machine the run happens on.
#
# The shot folder is emptied before every launch for the neighbouring reason: this takes the newest
# .bmp in it afterwards, and a run that writes none - the reference build needs more than the wait
# below to reach frame 2400 - would otherwise hand back the previous case's picture and compare two
# different frames.  Empty, that is a stated failure instead of a number.
#
# -dx11post off for the same kind of reason: the post chain runs by default since v1.0.0, and a
# reference build from before that draws without it, so the bloom would be the difference counted.
function Shoot($exe, $c, $tag) {
  Get-ChildItem "$shots\sshot*.bmp" -ErrorAction SilentlyContinue | Remove-Item -Force
  $args = @('-win','-xres','1280','-yres','720','-quickstart','-noshellmap','-multiInstance','-msaa','0','-dx11post','off',
            '-map',"`"Maps\$($c.map)\$($c.map).map`"",'-autoskirmish','4','-aidiff','easy','-seed','5',
            '-maxframes',($c.f+80),'-screenshot',$c.f,'-camera',$c.x,$c.y,'-logPrefix',"chk_$tag`_") + $pace
  try {
    $p = Start-Process (Join-Path $run $exe) -ArgumentList $args -WorkingDirectory $run -PassThru
    $p.PriorityClass = 'AboveNormal'
    $null = $p.WaitForExit(900000)
  }
  finally {
    Get-Process -Name generals -ErrorAction SilentlyContinue | Stop-Process -Force
  }
  $f = Get-ChildItem "$shots\sshot*.bmp" -ErrorAction SilentlyContinue |
       Sort-Object LastWriteTime | Select-Object -Last 1
  if ($null -eq $f) { throw "$exe wrote no screenshot for $tag" }
  $img = [System.Drawing.Image]::FromFile($f.FullName)
  $out = "$tmp\chk_$tag.png"
  $img.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
  $img.Dispose()
  return $out
}

function DiffPct($a, $b) {
  $ia = New-Object System.Drawing.Bitmap($a)
  $ib = New-Object System.Drawing.Bitmap($b)
  $w = $ia.Width; $h = [Math]::Min($ia.Height, 520)
  $n = 0; $t = 0
  for ($y = 0; $y -lt $h; $y += 2) {
    for ($x = 0; $x -lt $w; $x += 2) {
      $pa = $ia.GetPixel($x, $y); $pb = $ib.GetPixel($x, $y)
      $d = [Math]::Abs($pa.R - $pb.R) + [Math]::Abs($pa.G - $pb.G) + [Math]::Abs($pa.B - $pb.B)
      $t++
      if ($d -gt 40) { $n++ }
    }
  }
  $ia.Dispose(); $ib.Dispose()
  return [Math]::Round(100.0 * $n / $t, 2)
}

$fail = 0
foreach ($c in $cases) {
  $tag = ($c.map -replace '[^A-Za-z]','') + "_$($c.x)_$($c.f)"
  $a = Shoot 'generals_base.exe' $c "base_$tag"
  $b = Shoot 'generals.exe'      $c "new_$tag"
  $d = DiffPct $a $b
  $lim = if ($Limit -gt 0) { $Limit } else { $c.lim }
  $verdict = if ($d -le $lim) { 'ok' } else { 'DIFFERENT'; }
  if ($d -gt $lim) { $fail++ }
  "{0,-20} cam {1,5},{2,-5} frame {3,-5} diff {4,5}% / {5}%  {6}" -f $c.map, $c.x, $c.y, $c.f, $d, $lim, $verdict
}
"---"
if ($fail -eq 0) { "all $($cases.Count) views match the reference build within their limits" }
else { "$fail of $($cases.Count) views differ by more than their limits" }
exit $fail
