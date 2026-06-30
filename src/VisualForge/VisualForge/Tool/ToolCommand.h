#pragma once

#include "Tool/ToolKind.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace VisualForge::Tool
{
    struct ToolCommand
    {
        ToolKind Kind{ ToolKind::Git };
        std::wstring Executable;
        std::vector<std::wstring> Arguments;
        std::filesystem::path WorkingDirectory;
        std::wstring DisplayName;

        [[nodiscard]] std::wstring ToCommandLine() const;
    };

    [[nodiscard]] std::wstring QuoteArgument(std::wstring_view argument);
}
