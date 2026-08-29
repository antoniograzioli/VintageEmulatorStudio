# Canonical Windows Release x64 Build

This procedure builds the canonical Windows x64 Standalone and VST3 products from the repository root. It does not build or install ROMs and does not install the VST3 into the user's plugin directories.

## Prerequisites

- Windows 10 or 11 x64.
- Visual Studio 2022 with the Desktop development with C++ workload and MSVC v143. The validated compiler is MSVC 14.44.35207/14.44.35227.
- Windows 10/11 SDK. The validated installation resolves the Projucer projects' `10.0` SDK selector to Windows SDK `10.0.26100.0`.
- JUCE/Projucer compatible with JUCE 8.0.13. The script defaults to `C:\JUCE`; pass `-JuceRoot` to use another location.
- MSYS2 UCRT64/MAME build environment for generating the MAME Visual Studio projects.

The Release projects use the dynamic MSVC runtime (`/MD`). Release/test systems therefore require the latest supported Microsoft Visual C++ Redistributable v14 x64, with a runtime version at least as new as the MSVC 14.44 build tools used here. Install Microsoft's x64 package, `vc_redist.x64.exe`, from the [latest supported Visual C++ Redistributable downloads](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170) page. Current Windows 10/11 systems commonly already have it through installed software or servicing, but release documentation and testing must not assume it is present.

## Projucer regeneration

1. From the canonical repository root, open `platform\windows\VintageEmulatorStudio.windows.jucer` in Projucer and choose **Save Project**.
2. From the project root, run:

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\platform\windows\build-windows-release.ps1 -IntegrateOnly
   ```

Projucer cannot express the validated per-file Release x64 FLAC setting and shared property-sheet imports together. The command idempotently restores and verifies only these generated-project settings:

- imports of `Builds\VisualStudio2022\VESMameIntegration.props` in Shared Code, Standalone, and VST3;
- `JUCE_INCLUDE_FLAC_CODE=0` and the MAME FLAC include directory on the Shared Code `include_juce_audio_formats.cpp` item for Release x64.

`VESMameIntegration.props` remains the single authoritative definition of MAME objects, libraries, system libraries, compile settings, and resource staging. Do not edit generated `.vcxproj` files manually.

The equivalent command-line regeneration and integration check is:

```powershell
powershell -ExecutionPolicy Bypass -File .\platform\windows\build-windows-release.ps1 -Regenerate -IntegrateOnly
```

Use `-JuceRoot D:\path\to\JUCE` when Projucer is not at `C:\JUCE`.

## MAME project generation and build

Open the MSYS2 UCRT64 shell, change to `validation/mame-0.289-patched`, and generate the Visual Studio projects with the proven Windows configuration:

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

Adjust the Visual Studio edition path if it is not Community. The release script locates MSBuild with `vswhere.exe` automatically.

## VES build and Dist staging

The canonical release build command is:

```powershell
powershell -ExecutionPolicy Bypass -File .\platform\windows\build-windows-release.ps1
```

It applies/verifies the regeneration-safe integration, checks the existing MAME artifacts, and runs these builds in order:

```powershell
MSBuild "Builds\VisualStudio2022\Vintage Emulator Studio_SharedCode.vcxproj" /t:Build /m /p:Configuration=Release /p:Platform=x64
MSBuild "Builds\VisualStudio2022\Vintage Emulator Studio_StandalonePlugin.vcxproj" /t:Build /m /p:Configuration=Release /p:Platform=x64
MSBuild "Builds\VisualStudio2022\Vintage Emulator Studio_VST3ManifestHelper.vcxproj" /t:Build /m /p:Configuration=Release /p:Platform=Win32
MSBuild "Builds\VisualStudio2022\Vintage Emulator Studio_VST3.vcxproj" /t:Build /m /p:Configuration=Release /p:Platform=x64
```

Finally it replaces only `Dist\Windows\x64`, stages the Standalone and VST3 from their build outputs, omits macOS metadata (`.DS_Store` and `._*`) and Photoshop source files from the release artwork, and validates the staged files:

- PE x64 architecture for Standalone and VST3.
- 71 filesystem MAME linker inputs: 43 explicit objects and 28 archives.
- 22 Windows system libraries declared by `VESMameIntegration.props`.
- 45 machine profiles/registrations and machine strings in both binaries.
- VST3 `moduleinfo.json` with a real JSON parser.
- 37 artwork files and 3 plugin files in each staged product.
- No ROM, NVRAM, user media/settings, PSD, `.DS_Store`, or AppleDouble metadata in staged resources.
- No SDL, MinGW, MSYS, libgcc, libstdc++, or winpthread runtime dependency.
- Expected MSVC `/MD` runtime imports.
- No dependency on the Windows RC tree.

The resulting release-candidate layout is:

```text
Dist\Windows\x64\
  Standalone\
    Vintage Emulator Studio.exe
    Resources\
      plugins\
      artwork\
  VST3\
    Vintage Emulator Studio.vst3\
      Contents\
        x86_64-win\
          Vintage Emulator Studio.vst3
        Resources\
          moduleinfo.json
          plugins\
          artwork\
```
