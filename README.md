# VisualForge

VisualForge is a WinUI 3 + C++/WinRT IDE shell for native C++ development.

The project is organized as three layers:

- IDE Layer: WinUI shell, editor surface, project explorer, Git UI, debug UI, and package UI.
- Integration Layer: adapters that translate IDE intent into tool commands.
- Tool Layer: clangd, git, MSBuild, cl.exe, vcpkg, NuGet, and LLDB/DAP.

## Current Slice

- Main window now loads a WinUI workbench view.
- The workbench contains project, editor, output, Git, debug, and package panes.
- Tool command modeling is implemented in `Tool`.
- Initial adapters are implemented for clangd, Git, MSBuild, vcpkg, NuGet, and LLDB/DAP.
- The project builds successfully as `Debug|x64`.

## Build

```powershell
msbuild src\VisualForge\VisualForge\VisualForge.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /nologo
```

## Language Service Plan

The intended flow is:

```text
MSBuild restore/evaluation
MSBuild GenerateCompileCommands
clangd --compile-commands-dir=<project>
```

`GenerateCompileCommands` is enabled in the project file and the MSBuild adapter emits the explicit command plan. On the current local toolchain, MSBuild completes successfully but does not yet write `compile_commands.json`, so the next project-system task is to add a deterministic compile database generator or bind the exact Visual C++ target that emits it.
