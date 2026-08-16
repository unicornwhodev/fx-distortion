# Musique Distortion

Free Windows x64 distortion and saturation effect from the Musique FX collection.

## Download
Ready-to-use builds are intended for GitHub **Releases** as a Windows x64 installer and a portable Standalone + VST3 + factory-presets package.

## Build
```powershell
.\_build_all.ps1 -Configuration Release -BootstrapJuce
```
Or use `-JuceDir C:\Dev\JUCE` with an existing JUCE 8.0.4 checkout.

## Package
```powershell
.\_package_release.ps1 -Configuration Release -BootstrapJuce
```

The repo contains only product code, factory presets, the small local `FXShared` runtime/UI dependency, and release tooling. Internal DSP tests and QA artefacts are excluded.

The plugin is free to use; source is **source-available**, not open source. See [LICENSE.md](LICENSE.md).
