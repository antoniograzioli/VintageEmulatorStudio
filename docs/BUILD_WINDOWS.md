# Windows Build

VES supports Windows x64 Standalone and VST3 builds.

## Requirements

- Windows 10 or 11 x64.
- Visual Studio 2022 with the Desktop development with C++ workload and MSVC v143.
- Windows 10/11 SDK. The validated project configuration resolves the `10.0` SDK selector to Windows SDK `10.0.26100.0`.
- JUCE/Projucer compatible with JUCE 8.0.13. JUCE stays external to this repository and may be installed anywhere.
- MSYS2 UCRT64/MAME build environment for generating MAME Visual Studio projects.

Release projects use the dynamic MSVC runtime (`/MD`). Release/test systems require the current supported Microsoft Visual C++ Redistributable v14 x64, with a runtime version at least as new as the MSVC 14.44 build tools used for validation.

## Projucer Regeneration

The Windows JUCE project file is:

```text
platform\windows\VintageEmulatorStudio.windows.jucer
```

Open it in Projucer and choose **Save Project**, or regenerate through the release script. Then repair and verify the generated integration:

```powershell
powershell -ExecutionPolicy Bypass -File .\platform\windows\build-windows-release.ps1 -JuceRoot C:\path\to\JUCE-8.0.13 -IntegrateOnly
```

Command-line regeneration plus integration check:

```powershell
powershell -ExecutionPolicy Bypass -File .\platform\windows\build-windows-release.ps1 -JuceRoot C:\path\to\JUCE-8.0.13 -Regenerate -IntegrateOnly
```

`-JuceRoot` is the authoritative JUCE checkout location for the Windows release script. It must point to the JUCE checkout root, the directory containing `modules\`. `Projucer.exe` is required only when using `-Regenerate`. The script also passes this path to MSBuild as `VesJuceRoot`; manual MSBuild users can pass `/p:VesJuceRoot=C:\path\to\JUCE-8.0.13` or set `VES_JUCE_ROOT`.

`Builds\VisualStudio2022\VESJuceRoot.props` defines the portable JUCE root contract for the committed Visual Studio projects. `Builds\VisualStudio2022\VESMameIntegration.props` is the authoritative Windows definition of MAME objects, libraries, system libraries, compile settings, and resource staging. The integration step restores settings Projucer cannot fully express, including property-sheet imports, portable JUCE paths, and the Release x64 FLAC/MAME include settings.

## MAME Project Generation And Build

Open an MSYS2 UCRT64 shell, change to `validation/mame-0.289-patched`, and generate the Visual Studio projects:

```sh
make SUBTARGET=vesembedded OSD=windows PTR64=1 vs2022
```

Expected generated solution:

```text
validation\mame-0.289-patched\build\projects\windows\mamevesembedded\vs2022\mamevesembedded.sln
```

Build the generated solution from PowerShell or a Visual Studio developer command prompt:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" ".\validation\mame-0.289-patched\build\projects\windows\mamevesembedded\vs2022\mamevesembedded.sln" /t:Build /m /p:Configuration=Release /p:Platform=x64
```

Adjust the Visual Studio edition path if needed. The release script locates MSBuild with `vswhere.exe` automatically.

## VES Build

With the MAME artifacts already built, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\platform\windows\build-windows-release.ps1 -JuceRoot C:\path\to\JUCE-8.0.13
```

The script verifies the generated-project integration, checks the expected MAME artifacts, builds Shared Code, Standalone, VST3 Manifest Helper, and VST3, then stages output under `Dist\Windows\x64`.

It validates PE x64 architecture, MAME linker inputs, Windows system libraries, selected machine registrations, VST3 `moduleinfo.json`, artwork/plugin resources, absence of ROM/NVRAM/user media, and runtime dependency expectations.
