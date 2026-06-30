#include "pch.h"

#include "Tool/ToolKind.h"

namespace VisualForge::Tool
{
    std::wstring_view ToDisplayName(ToolKind kind) noexcept
    {
        switch (kind)
        {
        case ToolKind::Clangd:
            return L"clangd";
        case ToolKind::Git:
            return L"Git";
        case ToolKind::MSBuild:
            return L"MSBuild";
        case ToolKind::Compiler:
            return L"MSVC compiler";
        case ToolKind::Vcpkg:
            return L"vcpkg";
        case ToolKind::NuGet:
            return L"NuGet";
        case ToolKind::LLDB:
            return L"LLDB Debug Adapter";
        default:
            return L"Unknown tool";
        }
    }

    std::wstring_view ToExecutableName(ToolKind kind) noexcept
    {
        switch (kind)
        {
        case ToolKind::Clangd:
            return L"clangd.exe";
        case ToolKind::Git:
            return L"git.exe";
        case ToolKind::MSBuild:
            return L"msbuild.exe";
        case ToolKind::Compiler:
            return L"cl.exe";
        case ToolKind::Vcpkg:
            return L"vcpkg.exe";
        case ToolKind::NuGet:
            return L"nuget.exe";
        case ToolKind::LLDB:
            return L"lldb-dap.exe";
        default:
            return L"";
        }
    }
}
