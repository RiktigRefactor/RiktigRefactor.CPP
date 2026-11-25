# Windows: load the Visual Studio C++ environment for this job and build with clang-cl.
# clang-cl finds the MSVC and Windows SDK headers and libraries through the environment that vcvarsall sets up.
# Variables go to GITHUB_ENV and new PATH entries to GITHUB_PATH, so later steps see them.
$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio with the C++ tools was not found.' }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvarsall.bat'

$before = @{}
Get-ChildItem env: | ForEach-Object { $before[$_.Name] = $_.Value }

$lines = cmd /c "`"$vcvars`" $env:VCARCH > nul && set"
if ($LASTEXITCODE -ne 0) { throw "vcvarsall.bat $env:VCARCH failed." }

foreach ($line in $lines) {
    if ($line -notmatch '^([^=]+)=(.*)$') { continue }
    $name = $Matches[1]
    $value = $Matches[2]
    if ($name -ieq 'PATH') {
        $current = $env:PATH -split ';'
        foreach ($entry in ($value -split ';')) {
            if ($entry -and ($current -notcontains $entry)) { Add-Content -Path $env:GITHUB_PATH -Value $entry }
        }
    } elseif ($before[$name] -ne $value) {
        Add-Content -Path $env:GITHUB_ENV -Value "$name=$value"
    }
}

Add-Content -Path $env:GITHUB_ENV -Value 'CC=clang-cl'
Add-Content -Path $env:GITHUB_ENV -Value 'CXX=clang-cl'
