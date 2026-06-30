#pragma once

#include <string_view>

namespace VisualForge::Tool
{
    enum class ToolKind
    {
        Clangd,
        Git,
        MSBuild,
        Compiler,
        Vcpkg,
        NuGet,
        LLDB
    };

    std::wstring_view ToDisplayName(ToolKind kind) noexcept;
    std::wstring_view ToExecutableName(ToolKind kind) noexcept;
}
