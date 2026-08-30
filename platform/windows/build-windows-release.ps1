[CmdletBinding()]
param(
    [switch] $Regenerate,
    [switch] $IntegrateOnly,
    [string] $JuceRoot = 'C:\JUCE',
    [string] $MSBuildPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptRoot = $PSScriptRoot
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot '..\..'))
$buildRoot = Join-Path $projectRoot 'Builds\VisualStudio2022'
$jucerFile = Join-Path $scriptRoot 'VintageEmulatorStudio.windows.jucer'
$integrationFile = Join-Path $buildRoot 'VESMameIntegration.props'
$sharedProject = Join-Path $buildRoot 'Vintage Emulator Studio_SharedCode.vcxproj'
$standaloneProject = Join-Path $buildRoot 'Vintage Emulator Studio_StandalonePlugin.vcxproj'
$vst3Project = Join-Path $buildRoot 'Vintage Emulator Studio_VST3.vcxproj'
$helperProject = Join-Path $buildRoot 'Vintage Emulator Studio_VST3ManifestHelper.vcxproj'
$mameRoot = Join-Path $projectRoot 'validation\mame-0.289-patched'
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

function Assert-File([string] $Path, [string] $Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing ${Description}: $Path"
    }
}

function Assert-CanonicalRoot {
    Assert-File (Join-Path $projectRoot 'Source\VintageEmulatorStudioProcessor.cpp') 'canonical processor source'
    Assert-File (Join-Path $projectRoot 'Builds\VisualStudio2022\VESMameIntegration.props') 'canonical Windows MAME integration'
    Assert-File (Join-Path $scriptRoot 'VintageEmulatorStudio.windows.jucer') 'canonical Windows Projucer project'
    if ($projectRoot -match 'VintageEmulatorStudioMAME289_Win') {
        throw "Refusing to run from obsolete Windows source tree: $projectRoot"
    }
}

function Write-TextIfChanged([string] $Path, [string] $Text) {
    $oldText = [System.IO.File]::ReadAllText($Path)
    if ($oldText -ne $Text) {
        [System.IO.File]::WriteAllText($Path, $Text, $utf8NoBom)
        Write-Host "Updated $Path"
    } else {
        Write-Host "Already integrated: $Path"
    }
}

function Add-IntegrationImport([string] $ProjectPath) {
    $text = [System.IO.File]::ReadAllText($ProjectPath)
    $text = [regex]::Replace(
        $text,
        '(?m)^\s*<Import Project="VESMameIntegration\.props"\s*/>\r?\n?',
        '')

    $cppTargets = '  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets"/>'
    $replacement = $cppTargets + "`r`n" + '  <Import Project="VESMameIntegration.props" />'
    if ([regex]::Matches($text, [regex]::Escape($cppTargets)).Count -ne 1) {
        throw "Expected one Microsoft.Cpp.targets import in $ProjectPath"
    }

    Write-TextIfChanged $ProjectPath ($text.Replace($cppTargets, $replacement))
}

function Set-SharedCodeFlacOverride {
    $text = [System.IO.File]::ReadAllText($sharedProject)
    $pattern = '(?s)    <ClCompile Include="([^\"]*\\JuceLibraryCode\\include_juce_audio_formats\.cpp)"(?:\s*/>|>.*?</ClCompile>)'
    $matches = [regex]::Matches($text, $pattern)
    if ($matches.Count -ne 1) {
        throw "Expected one JUCE audio-formats compilation item in $sharedProject; found $($matches.Count)"
    }

    $includePath = $matches[0].Groups[1].Value
    $replacement = '    <ClCompile Include="' + $includePath + '">' + "`r`n" + @'
      <PreprocessorDefinitions Condition="'$(Configuration)|$(Platform)'=='Release|x64'">JUCE_INCLUDE_FLAC_CODE=0;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <AdditionalIncludeDirectories Condition="'$(Configuration)|$(Platform)'=='Release|x64'">$(ProjectDir)..\..\validation\mame-0.289-patched\3rdparty\flac\include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
'@

    Write-TextIfChanged $sharedProject ([regex]::Replace($text, $pattern, $replacement))
}

function Apply-ProjucerIntegration {
    Assert-File $integrationFile 'authoritative MAME integration property sheet'
    foreach ($project in @($sharedProject, $standaloneProject, $vst3Project, $helperProject)) {
        Assert-File $project 'generated Visual Studio project'
    }

    Set-SharedCodeFlacOverride
    foreach ($project in @($sharedProject, $standaloneProject, $vst3Project)) {
        Add-IntegrationImport $project
    }

    $helperText = [System.IO.File]::ReadAllText($helperProject)
    if ($helperText.Contains('VESMameIntegration.props')) {
        throw 'The VST3 Manifest Helper must not import the MAME integration.'
    }

    $sharedText = [System.IO.File]::ReadAllText($sharedProject)
    if (-not $sharedText.Contains('JUCE_INCLUDE_FLAC_CODE=0') -or
        -not $sharedText.Contains('3rdparty\flac\include')) {
        throw 'The Shared Code FLAC override was not restored.'
    }

    Write-Host 'Projucer integration verified for Shared Code, Standalone, and VST3.'
}

function Find-MSBuild {
    if ($MSBuildPath) {
        Assert-File $MSBuildPath 'MSBuild executable'
        return (Resolve-Path -LiteralPath $MSBuildPath).Path
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    Assert-File $vswhere 'Visual Studio Installer vswhere.exe'
    $installation = (& $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath | Select-Object -First 1)
    if (-not $installation) {
        throw 'Visual Studio 2022 with MSBuild and C++ tools was not found.'
    }

    $candidate = Join-Path $installation 'MSBuild\Current\Bin\amd64\MSBuild.exe'
    Assert-File $candidate '64-bit MSBuild executable'
    return $candidate
}

function Invoke-Build([string] $MSBuild, [string] $Project, [string] $Platform) {
    Write-Host "Building $(Split-Path -Leaf $Project): Release|$Platform"
    & $MSBuild $Project /t:Build /m /v:minimal /nologo /p:Configuration=Release "/p:Platform=$Platform"
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed ($LASTEXITCODE): $Project"
    }
}

function Assert-MameArtifacts {
    $props = [System.IO.File]::ReadAllText($integrationFile)
    $requiredLibraries = [regex]::Matches($props, '(?i)(?<![A-Za-z0-9_])([A-Za-z0-9_]+\.lib)(?![A-Za-z0-9_])') |
        ForEach-Object { $_.Groups[1].Value.ToLowerInvariant() } |
        Sort-Object -Unique
    $libraryRoots = @(
        (Join-Path $mameRoot 'build\vs2022\bin\x64\Release'),
        (Join-Path $mameRoot 'build\vs2022\bin\x64\Release\mame_vesembedded')
    )
    $systemLibraries = @('user32.lib','advapi32.lib','wsock32.lib','ws2_32.lib','iphlpapi.lib','shell32.lib','userenv.lib','setupapi.lib','opengl32.lib','gdi32.lib','dsound.lib','dxguid.lib','oleaut32.lib','winmm.lib','comctl32.lib','comdlg32.lib','dinput8.lib','ole32.lib','psapi.lib','shcore.lib','shlwapi.lib','uuid.lib')
    $filesystemLibraries = @($requiredLibraries | Where-Object { $systemLibraries -notcontains $_ })
    if ($filesystemLibraries.Count -ne 28) {
        throw "Expected 28 MAME library inputs; found $($filesystemLibraries.Count)."
    }
    $requiredObjects = @([regex]::Matches($props, '(?i)([A-Za-z0-9_]+\.obj)') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    if ($requiredObjects.Count -ne 43) {
        throw "Expected 43 explicit MAME object inputs; found $($requiredObjects.Count)."
    }
    foreach ($library in $requiredLibraries) {
        if ($systemLibraries -contains $library) { continue }
        if (-not ($libraryRoots | Where-Object { Test-Path -LiteralPath (Join-Path $_ $library) -PathType Leaf })) {
            throw "Missing validated MAME library: $library"
        }
    }

    $objectRoot = Join-Path $mameRoot 'build\vs2022\obj\x64\Release\mamevesembedded'
    foreach ($object in $requiredObjects) {
        $path = if ($object -eq '6800dasm.obj') {
            Join-Path $mameRoot 'build\vs2022\obj\x64\Release\dasm\6800dasm.obj'
        } else {
            Join-Path $objectRoot $object
        }
        Assert-File $path "validated MAME object $object"
    }
    Write-Host 'Validated 71 filesystem MAME linker inputs: 43 objects and 28 libraries.'
}

function Get-PeMachine([string] $Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $reader = [System.IO.BinaryReader]::new($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "Not a PE file: $Path" }
        $stream.Position = 0x3c
        $peOffset = $reader.ReadInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { throw "Invalid PE signature: $Path" }
        return $reader.ReadUInt16()
    } finally {
        $stream.Dispose()
    }
}

function Test-ReleaseFile([System.IO.FileInfo] $File) {
    return $File.Name -ne '.DS_Store' -and
           -not $File.Name.StartsWith('._', [System.StringComparison]::Ordinal) -and
           $File.Extension -ne '.psd'
}

function Get-TreeManifest([string] $Root, [bool] $ReleaseFilesOnly = $false) {
    $manifest = @{}
    foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -File) {
        if ($ReleaseFilesOnly -and -not (Test-ReleaseFile $file)) { continue }
        $relative = $file.FullName.Substring($Root.Length).TrimStart('\')
        $manifest[$relative] = $file.Length
    }
    return $manifest
}

function Assert-TreesEqual([string] $Expected, [string] $Actual, [string] $Description, [bool] $FilterExpected = $false) {
    $left = Get-TreeManifest $Expected $FilterExpected
    $right = Get-TreeManifest $Actual
    $differences = @()
    foreach ($name in ($left.Keys + $right.Keys | Sort-Object -Unique)) {
        if (-not $left.ContainsKey($name) -or -not $right.ContainsKey($name) -or $left[$name] -ne $right[$name]) {
            $differences += $name
        }
    }
    if ($differences.Count -ne 0) {
        throw "$Description differs in $($differences.Count) file(s): $($differences[0..([Math]::Min(4, $differences.Count - 1))] -join ', ')"
    }
    Write-Host "$Description matches: $($left.Count) files, $(($left.Values | Measure-Object -Sum).Sum) bytes."
}

function Assert-ReleaseResources([string] $ResourceRoot, [string] $Description) {
    foreach ($directory in @('plugins', 'artwork')) {
        if (-not (Test-Path -LiteralPath (Join-Path $ResourceRoot $directory) -PathType Container)) {
            throw "$Description missing resource directory: $directory"
        }
    }

    $plugins = @(Get-ChildItem -LiteralPath (Join-Path $ResourceRoot 'plugins') -Recurse -File)
    $artwork = @(Get-ChildItem -LiteralPath (Join-Path $ResourceRoot 'artwork') -Recurse -File)
    if ($plugins.Count -ne 3) { throw "$Description expected 3 plugin files; found $($plugins.Count)" }
    if ($artwork.Count -ne 37) { throw "$Description expected 37 artwork files; found $($artwork.Count)" }

    $forbidden = @(
        Get-ChildItem -LiteralPath $ResourceRoot -Recurse -File |
            Where-Object {
                $_.Name -eq '.DS_Store' -or
                $_.Name.StartsWith('._', [System.StringComparison]::Ordinal) -or
                $_.Extension -ieq '.psd' -or
                $_.FullName -match '\\(roms|nvram|cfg|ini|samples|media)\\'
            }
    )
    if ($forbidden.Count -ne 0) {
        throw "$Description contains forbidden release resource(s): $($forbidden[0].FullName)"
    }
}

function Assert-JsonFile([string] $Path, [string] $Description) {
    Assert-File $Path $Description
    try {
        [System.IO.File]::ReadAllText($Path) | ConvertFrom-Json | Out-Null
    } catch {
        throw "Invalid JSON in ${Description}: $Path"
    }
}

function ConvertTo-StrictJsonFile([string] $Path, [string] $Description) {
    Assert-File $Path $Description
    $text = [System.IO.File]::ReadAllText($Path)
    $strictText = [regex]::Replace($text, ',(?=\s*[}\]])', '')
    try {
        $strictText | ConvertFrom-Json | Out-Null
    } catch {
        throw "Could not normalize ${Description} to strict JSON: $Path"
    }
    if ($strictText -ne $text) {
        [System.IO.File]::WriteAllText($Path, $strictText, $utf8NoBom)
        Write-Host "Normalized trailing commas in ${Description}."
    }
}

function Test-BinaryContains([string] $Path, [string[]] $Needles) {
    $text = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($Path))
    return @($Needles | Where-Object { -not $text.Contains($_) })
}

function Get-MachineDrivers {
    $profileSource = [System.IO.File]::ReadAllText((Join-Path $projectRoot 'Source\VintageEmulatorStudioProcessor.cpp'))
    $profileBlock = [regex]::Match($profileSource, '(?s)constexpr EmbeddedMachineProfile machineProfiles\[\]\s*(?:=\s*)?\{(.*?)\};')
    if (-not $profileBlock.Success) { throw 'Could not locate machineProfiles.' }
    $drivers = @([regex]::Matches($profileBlock.Groups[1].Value, '\{\s*"[^"]+"\s*,\s*"([^"]+)"') | ForEach-Object { $_.Groups[1].Value })
    if ($drivers.Count -ne 45 -or ($drivers | Sort-Object -Unique).Count -ne 45) {
        throw "Expected 45 unique machine profiles; found $($drivers.Count)."
    }

    $engineSource = [System.IO.File]::ReadAllText((Join-Path $mameRoot 'src\ves\embeddedinstruments\EmbeddedEmulatorEngine.cpp'))
    $missingRegistrations = @($drivers | Where-Object { -not $engineSource.Contains("&GAME_NAME($_)") })
    if ($missingRegistrations.Count -ne 0) {
        throw "Machine driver registration(s) missing: $($missingRegistrations -join ', ')"
    }
    return $drivers
}

function Get-Dumpbin([string] $MSBuild) {
    $installationRoot = Split-Path (Split-Path (Split-Path (Split-Path (Split-Path $MSBuild -Parent) -Parent) -Parent) -Parent) -Parent
    $dumpbin = Get-ChildItem -LiteralPath (Join-Path $installationRoot 'VC\Tools\MSVC') -Recurse -Filter dumpbin.exe |
        Where-Object { $_.FullName -match 'Hostx64\\x64\\dumpbin\.exe$' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if (-not $dumpbin) { throw 'dumpbin.exe (Hostx64/x64) was not found.' }
    return $dumpbin.FullName
}

function Assert-Dependencies([string] $Dumpbin, [string] $Binary) {
    $output = (& $Dumpbin /dependents $Binary 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0) { throw "dumpbin failed for $Binary" }
    $dependencies = @([regex]::Matches($output, '(?im)^\s+([A-Za-z0-9_.-]+\.dll)\s*$') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    $forbidden = @($dependencies | Where-Object { $_ -match '(?i)SDL|libgcc|libstdc\+\+|libwinpthread|msys-|mingw' })
    if ($forbidden.Count -ne 0) { throw "Forbidden runtime dependency in ${Binary}: $($forbidden -join ', ')" }
    foreach ($required in @('MSVCP140.dll', 'VCRUNTIME140.dll', 'VCRUNTIME140_1.dll')) {
        if ($dependencies -notcontains $required) { throw "Expected MSVC runtime dependency $required missing from $Binary" }
    }
    Write-Host "Dependencies verified ($(Split-Path -Leaf $Binary)): $($dependencies -join ', ')"
}

function Stage-And-Validate([string] $MSBuild) {
    $standaloneOutput = Join-Path $buildRoot 'x64\Release\Standalone Plugin'
    $vst3Output = Join-Path $buildRoot 'x64\Release\VST3\Vintage Emulator Studio.vst3'
    $standaloneBinary = Join-Path $standaloneOutput 'Vintage Emulator Studio.exe'
    $vst3Binary = Join-Path $vst3Output 'Contents\x86_64-win\Vintage Emulator Studio.vst3'
    $moduleInfo = Join-Path $vst3Output 'Contents\Resources\moduleinfo.json'
    foreach ($item in @($standaloneBinary, $vst3Binary, $moduleInfo)) { Assert-File $item 'release output' }
    ConvertTo-StrictJsonFile $moduleInfo 'VST3 moduleinfo.json'

    $distWindows = Join-Path $projectRoot 'Dist\Windows'
    $distRoot = Join-Path $distWindows 'x64'
    $resolvedProject = [System.IO.Path]::GetFullPath($projectRoot).TrimEnd('\')
    $resolvedDist = [System.IO.Path]::GetFullPath($distRoot).TrimEnd('\')
    $expectedPrefix = ([System.IO.Path]::GetFullPath($distWindows).TrimEnd('\')) + '\'
    if (-not $resolvedDist.StartsWith($expectedPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or $resolvedDist -eq $resolvedProject) {
        throw "Refusing to replace unsafe Dist path: $resolvedDist"
    }
    if (Test-Path -LiteralPath $distRoot) { Remove-Item -LiteralPath $distRoot -Recurse -Force }

    $distStandalone = Join-Path $distRoot 'Standalone'
    $distVst3 = Join-Path $distRoot 'VST3'
    New-Item -ItemType Directory -Path $distStandalone, $distVst3 | Out-Null
    Copy-Item -LiteralPath $standaloneBinary -Destination $distStandalone
    Copy-Item -LiteralPath (Join-Path $standaloneOutput 'Resources') -Destination $distStandalone -Recurse
    Copy-Item -LiteralPath $vst3Output -Destination $distVst3 -Recurse

    $stagedStandaloneBinary = Join-Path $distStandalone 'Vintage Emulator Studio.exe'
    $stagedVst3Root = Join-Path $distVst3 'Vintage Emulator Studio.vst3'
    $stagedVst3Binary = Join-Path $stagedVst3Root 'Contents\x86_64-win\Vintage Emulator Studio.vst3'
    $stagedModuleInfo = Join-Path $stagedVst3Root 'Contents\Resources\moduleinfo.json'
    $nonReleaseArtwork = @(
        Get-ChildItem -LiteralPath (Join-Path $distStandalone 'Resources\artwork'), (Join-Path $stagedVst3Root 'Contents\Resources\artwork') -Recurse -File |
            Where-Object { -not (Test-ReleaseFile $_) }
    )
    foreach ($file in $nonReleaseArtwork) { Remove-Item -LiteralPath $file.FullName -Force }
    Write-Host "Excluded $($nonReleaseArtwork.Count) macOS metadata/artwork-source files from Dist."

    foreach ($binary in @($stagedStandaloneBinary, $stagedVst3Binary)) {
        if ((Get-PeMachine $binary) -ne 0x8664) { throw "Not PE x64: $binary" }
    }
    Assert-JsonFile $stagedModuleInfo 'staged VST3 moduleinfo.json'

    Assert-TreesEqual (Join-Path $standaloneOutput 'Resources') (Join-Path $distStandalone 'Resources') 'Standalone release resources' $true
    Assert-TreesEqual $vst3Output $stagedVst3Root 'VST3 release bundle' $true
    Assert-ReleaseResources (Join-Path $distStandalone 'Resources') 'Standalone release'
    Assert-ReleaseResources (Join-Path $stagedVst3Root 'Contents\Resources') 'VST3 release'

    $drivers = Get-MachineDrivers
    foreach ($binary in @($stagedStandaloneBinary, $stagedVst3Binary)) {
        $missing = @(Test-BinaryContains $binary $drivers)
        if ($missing.Count -ne 0) { throw "Machine names absent from $(Split-Path -Leaf $binary): $($missing -join ', ')" }
    }
    Write-Host 'All 45 machine profiles are registered in source and present in both staged binaries.'

    $dumpbin = Get-Dumpbin $MSBuild
    Assert-Dependencies $dumpbin $stagedStandaloneBinary
    Assert-Dependencies $dumpbin $stagedVst3Binary

    foreach ($resourceRoot in @(
        (Join-Path $distStandalone 'Resources'),
        (Join-Path $stagedVst3Root 'Contents\Resources')
    )) {
        foreach ($kind in @('plugins', 'artwork')) {
            $files = @(Get-ChildItem -LiteralPath (Join-Path $resourceRoot $kind) -Recurse -File)
            Write-Host "$resourceRoot\$kind : $($files.Count) files, $(($files | Measure-Object Length -Sum).Sum) bytes"
        }
    }

    $standaloneInfo = Get-Item -LiteralPath $stagedStandaloneBinary
    $vst3Info = Get-Item -LiteralPath $stagedVst3Binary
    Write-Host "Standalone: $($standaloneInfo.FullName) ($($standaloneInfo.Length) bytes)"
    Write-Host "VST3 binary: $($vst3Info.FullName) ($($vst3Info.Length) bytes)"
    Write-Host "Release output ready: $distRoot"
}

Assert-CanonicalRoot
Assert-File $jucerFile 'Windows Projucer project'
if ($Regenerate) {
    $projucer = Join-Path $JuceRoot 'Projucer.exe'
    Assert-File $projucer 'Projucer executable'
    Write-Host "Regenerating Visual Studio projects with $projucer"
    $projucerProcess = Start-Process -FilePath $projucer -ArgumentList @('--resave', $jucerFile) -Wait -PassThru -NoNewWindow
    if ($projucerProcess.ExitCode -ne 0) { throw "Projucer regeneration failed ($($projucerProcess.ExitCode))." }
}

Apply-ProjucerIntegration
if ($IntegrateOnly) { return }

Assert-MameArtifacts
$msbuild = Find-MSBuild
Invoke-Build $msbuild $sharedProject 'x64'
Invoke-Build $msbuild $standaloneProject 'x64'
Invoke-Build $msbuild $helperProject 'Win32'
Invoke-Build $msbuild $vst3Project 'x64'
Stage-And-Validate $msbuild
