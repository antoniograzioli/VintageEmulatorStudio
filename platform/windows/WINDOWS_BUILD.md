# Windows Build

Public Windows build instructions are maintained in `../../docs/BUILD_WINDOWS.md`.

The script in this directory is the Windows build/validation entry point:

```powershell
powershell -ExecutionPolicy Bypass -File .\platform\windows\build-windows-release.ps1 -JuceRoot C:\path\to\JUCE-8.0.13
```
