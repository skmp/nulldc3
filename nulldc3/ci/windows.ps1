<#
windows.ps1: the Windows job of CI (.github/workflows/ci.yml), the same on a runner and on a machine of one's own.
Written for Windows PowerShell 5.1 (powershell.exe), which every Windows 10 and 11 has; the workflow runs it with
"shell: powershell". Every path comes from the parameters; on a runner RUNNER_TEMP is the default of -Work.

    windows.ps1 -Mode ci|full -Dist <dist-native> -Ui <ui checkout> [-Work <dir>] [-Out <dir>] [-Data <dir>]
                [-Python <python.exe>] [-NoNpmCi] [-Jobs <n>]

 1. The UI checkout: its LICENSE (the zip's LICENSE-devtools) has to be there and the checkout at the commit the
    tree pins for devtools (git rev-parse HEAD:devtools); else the script stops and says what it compared.
 2. Visual Studio's newest developer environment (vswhere -latest, vcvars64.bat), and nulldc3-winrun.exe built by
    tests\winrun\bootstrap.cmd into <work>\winrun: every program the script starts that was built here runs inside it
    (ctest and the release emulator; PL-D69).
 3. The release emulator: Release, the UI bundle -Dist embedded (an explicit path: no bundle there is a configure
    error), -DNULLDC_PREFIX_MAP=ON, -DCMAKE_VERBOSE_MAKEFILE=OFF, into <work>\release; its configure lines
    "Dreamcast DevTools: ..." into <out>\nulldc3-<version>-windows-x86_64.configure.txt.
 4. The tests project (<work>\tests) with the release emulator as NULLDC_EMULATOR, the vector submodules of the tree,
    the UI checkout (npm ci there, unless -NoNpmCi) as NULLDC_DEVTOOLS_DIR; ci: what CI has (PL-D58), missing data
    skips its tests; full: the data of the gates from -Data, which has elf\, wren7-rtl\, caique-rtl\ and
    shrike4-rtl\, and NULLDC_TESTS_REQUIRE_DATA=ON. ctest -j4 --output-junit inside an outer nulldc3-winrun; the
    skipped tests with their reasons go into $env:GITHUB_STEP_SUMMARY when it is set (junit.py), and onto stdout.
 5. The zip <out>\nulldc3-<version>-windows-x86_64.zip, made with Windows' own tar.exe (bsdtar, -a: a zip whose
    entry names have "/", never Compress-Archive, PL-D82): the folder nulldc3-<version>-windows-x86_64\ with
    nulldc3.exe, LICENSE, LICENSE-GPL, LICENSE-LGPL, LICENSE-shell (nulldc3\LICENSE), LICENSE-devtools (the UI
    checkout's LICENSE) and README.txt (readme-windows.txt of this directory). Its entries are listed and checked.
 6. The smoke run (smoke.py) of the zip unpacked with tar.exe: --version, a headless guest run and the debugger's
    describe, each through nulldc3-winrun, with P0's keys and a --portable directory below <work>.
-Jobs <n>: ninja's -j (default: ninja's own). Exit status 0 when the build, the tests, the zip and the smoke run all
passed.
#>
param(
    [Parameter(Mandatory = $true)][ValidateSet('ci', 'full')][string]$Mode,
    [Parameter(Mandatory = $true)][string]$Dist,
    [Parameter(Mandatory = $true)][string]$Ui,
    [string]$Work = '',
    [string]$Out = '',
    [string]$Data = '',
    [string]$Python = '',
    [switch]$NoNpmCi,
    [int]$Jobs = 0
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Fail([string]$text) {
    Write-Output "windows.ps1: $text"
    exit 1
}

# a program, with its exit status checked (Windows PowerShell does not stop on a program's exit status)
function Run([string]$what, [scriptblock]$block) {
    & $block
    if ($LASTEXITCODE -ne 0) { Fail "$what failed with exit status $LASTEXITCODE" }
}

# A directory removed, tried again for a minute: an executable that has just run (or that Defender scans because it
# was just unpacked) stays locked a moment after its process ended. What is left after that is said, not fatal
function Remove-Tree([string]$path) {
    for ($i = 0; $i -lt 60 -and (Test-Path $path); $i++) {
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $path
        if (Test-Path $path) { Start-Sleep -Seconds 1 }
    }
    if (Test-Path $path) { Write-Output "windows.ps1: $path could not be removed (a file of it is still in use)" }
}

function Write-Lf([string]$path, [string[]]$lines) {
    [System.IO.File]::WriteAllText($path, (($lines -join "`n") + "`n"), (New-Object System.Text.UTF8Encoding($false)))
}

$tree = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ($Work -eq '') {
    if (-not $env:RUNNER_TEMP) { Fail '-Work <dir> is needed outside a runner' }
    $Work = Join-Path $env:RUNNER_TEMP 'nulldc3-ci'
}
New-Item -ItemType Directory -Force -Path $Work | Out-Null
$Work = (Resolve-Path $Work).Path
if ($Out -eq '') { $Out = Join-Path $Work 'out' }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$Dist = (Resolve-Path $Dist).Path
$Ui = (Resolve-Path $Ui).Path
if ($Python -eq '') { $Python = (Get-Command python.exe).Source }
$tar = Join-Path $env:SystemRoot 'System32\tar.exe'
$ninjaJobs = @()
if ($Jobs -gt 0) { $ninjaJobs = @('-j', "$Jobs") }

# 1. the UI checkout
if (-not (Test-Path (Join-Path $Ui 'LICENSE') -PathType Leaf)) {
    Fail "there is no $Ui\LICENSE, the UI's licence that the zip carries as LICENSE-devtools"
}
$pin = (& git -C $tree rev-parse HEAD:devtools)
if ($LASTEXITCODE -ne 0) { Fail "the tree $tree pins no commit for devtools (git rev-parse HEAD:devtools)" }
$uiHead = (& git -C $Ui rev-parse HEAD)
if ($LASTEXITCODE -ne 0) { Fail "the UI checkout $Ui is not a git checkout (git -C $Ui rev-parse HEAD)" }
if ($uiHead -ne $pin) { Fail "the UI checkout $Ui is at $uiHead, the tree pins devtools at ${pin}: its LICENSE is not the pinned UI's" }
$uiVersion = (Get-Content -Raw (Join-Path $Dist 'bundle-info.json') | ConvertFrom-Json).version

# 2. Visual Studio and nulldc3-winrun
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
if (-not $vs) { Fail 'no Visual Studio with the C++ tools (vswhere -latest)' }
$env:VSCMD_SKIP_SENDTELEMETRY = '1'
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
# the developer environment, read into this process (nothing of it is printed)
$lines = & cmd.exe /d /c "`"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { Fail "$vcvars failed" }
foreach ($line in $lines) {
    $at = $line.IndexOf('=')
    if ($at -gt 0) { Set-Item -Path ('env:' + $line.Substring(0, $at)) -Value $line.Substring($at + 1) }
}
Write-Output "Visual Studio: $vs"
Run 'tests\winrun\bootstrap.cmd' { & cmd.exe /d /c "`"$tree\tests\winrun\bootstrap.cmd`" `"$Work\winrun`"" }
$winrun = Join-Path $Work 'winrun\nulldc3-winrun.exe'

# 3. the release emulator
$release = Join-Path $Work 'release'
# its stdout kept for the configure lines (stderr, CMake's warnings, goes to the console: Windows PowerShell would
# stop on the first line of a program's stderr that is redirected)
$configure = @(& cmake -S $tree -B $release -G Ninja -DCMAKE_BUILD_TYPE=Release "-DNULLDC_DEVTOOLS_DIST=$Dist" `
    -DNULLDC_PREFIX_MAP=ON -DCMAKE_VERBOSE_MAKEFILE=OFF -Wno-dev)
$status = $LASTEXITCODE
$configure | Write-Output
if ($status -ne 0) { Fail "the release emulator's configure failed with exit status $status" }
Run 'the release build' { & ninja -C $release @ninjaJobs }
$version = (Get-Content (Join-Path $release 'nulldc3-version.txt') -TotalCount 1).Trim()
$lines = @($configure | Where-Object { $_ -match '^-- Dreamcast DevTools: ' } | ForEach-Object { $_.Substring(3) })
if (-not ($lines | Where-Object { $_ -match '^Dreamcast DevTools: embedding ' })) {
    Fail 'the release build embeds no UI (no line "Dreamcast DevTools: embedding" in its configure output)'
}
$name = "nulldc3-$version-windows-x86_64"
Write-Lf (Join-Path $Out "$name.configure.txt") $lines
Write-Output "nulldc3 $version, windows-x86_64: $($lines -match 'embedding')"
if ($env:GITHUB_STEP_SUMMARY) {
    Add-Content -Path $env:GITHUB_STEP_SUMMARY -Value ("nullDC ``$version`` windows-x86_64: " + (($lines -match 'embedding') -replace '^Dreamcast DevTools: ', ''))
}
$failed = @()

# 4. the tests project, ctest inside nulldc3-winrun
if (-not $NoNpmCi) {
    Push-Location $Ui
    Run 'npm ci' { & npm.cmd ci }
    Pop-Location
} elseif (-not (Test-Path (Join-Path $Ui 'node_modules'))) {
    Fail "-NoNpmCi: the UI checkout $Ui has no node_modules"
}
$npm = (Get-Command npm.cmd).Source
$tests = Join-Path $Work 'tests'
$args_ = @("-DNULLDC_EMULATOR=$release\nulldc3.exe", "-DNULLDC_SH4_TESTS_DIR=$tree\tests\vendor\sh4-tests",
    "-DNULLDC_ARM7_TESTS_DIR=$tree\tests\vendor\arm7di-tests-dreamcast", "-DNULLDC_DEVTOOLS_DIR=$Ui",
    "-DPython3_EXECUTABLE=$Python", "-DNULLDC_NPM=$npm")
if ($Mode -eq 'full') {
    if ($Data -eq '') { Fail 'full: -Data <dir> with elf\, wren7-rtl\, caique-rtl\ and shrike4-rtl\' }
    $Data = (Resolve-Path $Data).Path
    $args_ += @("-DNULLDC_ELF_DIR=$Data\elf", "-DNULLDC_WREN7_DIR=$Data\wren7-rtl", "-DNULLDC_CAIQUE_DIR=$Data\caique-rtl",
        "-DNULLDC_SHRIKE4_DIR=$Data\shrike4-rtl", '-DNULLDC_TESTS_REQUIRE_DATA=ON')
} else {
    $args_ += @('-DNULLDC_ELF_DIR=', '-DNULLDC_WREN7_DIR=', '-DNULLDC_CAIQUE_DIR=', '-DNULLDC_SHRIKE4_DIR=',
        '-DNULLDC_TESTS_REQUIRE_DATA=OFF')
}
# a configure of its own every time: nothing a run of the other mode cached stays
if (Test-Path "$tests\CMakeCache.txt") { Remove-Item -Force "$tests\CMakeCache.txt" }
Run 'the tests project configure' { & cmake -S "$tree\tests" -B $tests -G Ninja -Wno-dev @args_ }
Run 'the tests project build' { & ninja -C $tests @ninjaJobs }
# '--' quoted: Windows PowerShell drops a bare -- on its way to a program
& $winrun --timeout 7200 '--' ctest --test-dir $tests -j4 --output-on-failure --output-junit "$tests\junit.xml"
$ctest = $LASTEXITCODE
$summary = @()
if ($env:GITHUB_STEP_SUMMARY) { $summary = @('--summary', $env:GITHUB_STEP_SUMMARY) }
& $Python -B "$PSScriptRoot\junit.py" "$tests\junit.xml" --title "Tests, windows-x86_64, $Mode mode" @summary
if ($ctest -ne 0) { $failed += "ctest (exit status $ctest)" }

# 5. the zip
$stage = Join-Path $Work 'zip'
Remove-Tree $stage
$folder = Join-Path $stage $name
New-Item -ItemType Directory -Force -Path $folder | Out-Null
Copy-Item (Join-Path $release 'nulldc3.exe') $folder
Copy-Item (Join-Path $tree 'LICENSE') (Join-Path $folder 'LICENSE')
Copy-Item (Join-Path $tree 'LICENSE-GPL') (Join-Path $folder 'LICENSE-GPL')
Copy-Item (Join-Path $tree 'LICENSE-LGPL') (Join-Path $folder 'LICENSE-LGPL')
Copy-Item (Join-Path $tree 'nulldc3\LICENSE') (Join-Path $folder 'LICENSE-shell')
Copy-Item (Join-Path $Ui 'LICENSE') (Join-Path $folder 'LICENSE-devtools')
$readme = (Get-Content -Raw (Join-Path $PSScriptRoot 'readme-windows.txt')).Replace('{{version}}', $version).Replace("`r`n", "`n").Replace("`n", "`r`n")
[System.IO.File]::WriteAllText((Join-Path $folder 'README.txt'), $readme, (New-Object System.Text.UTF8Encoding($false)))
$zip = Join-Path $Out "$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Run 'tar.exe -a -c' { & $tar -a -c -f $zip -C $stage $name }
$entries = @(& $tar -t -f $zip)
if ($LASTEXITCODE -ne 0) { Fail "tar.exe -t of $zip failed" }
Write-Output "the zip's entries:"
$entries | ForEach-Object { Write-Output "  $_" }
$wanted = @('LICENSE', 'LICENSE-GPL', 'LICENSE-LGPL', 'LICENSE-devtools', 'LICENSE-shell', 'README.txt', 'nulldc3.exe')
$files = @($entries | Where-Object { -not $_.EndsWith('/') } | ForEach-Object { $_ })
$bad = @($entries | Where-Object { $_.Contains('\') -or -not $_.StartsWith("$name/") })
$names = @($files | ForEach-Object { $_.Substring($name.Length + 1) } | Sort-Object)
if ($bad.Count -ne 0 -or (Compare-Object -SyncWindow 0 $names ($wanted | Sort-Object))) {
    $failed += 'the zip'
    Write-Output "FAIL the zip: entries outside $name/ or with a backslash: [$($bad -join ', ')]; files [$($names -join ', ')], expected [$($wanted -join ', ')]"
} else {
    Write-Output "PASS the zip: every entry below $name/ with / alone, the files $($names -join ', ')"
}
Remove-Tree $stage

# 6. the smoke run of the zip unpacked
$smoke = Join-Path $Work 'smoke'
Remove-Tree $smoke
New-Item -ItemType Directory -Force -Path (Join-Path $smoke 'unpacked') | Out-Null
Run 'tar.exe -x' { & $tar -x -f $zip -C (Join-Path $smoke 'unpacked') }
$exe = Join-Path $smoke "unpacked\$name\nulldc3.exe"
& $Python -B "$PSScriptRoot\smoke.py" --exe $exe --version $version --ui-version $uiVersion `
    --guest "$tree\tests\debugger\guest\dsptest\dsptest.elf" --work (Join-Path $smoke 'run') `
    --portable (Join-Path $smoke 'portable') '--' $winrun --timeout 120 '--'
if ($LASTEXITCODE -ne 0) { $failed += 'the smoke run' }
Remove-Tree $smoke

if ($failed.Count -ne 0) {
    Write-Output "windows.ps1: failed: $($failed -join '; ')"
    exit 1
}
Write-Output "windows.ps1: $zip"
exit 0
