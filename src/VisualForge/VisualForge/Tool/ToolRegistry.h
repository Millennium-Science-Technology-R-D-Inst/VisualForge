#pragma once

#include "Tool/ToolCommand.h"

#include <optional>
#include <string>
#include <vector>

namespace VisualForge::Tool
{
    struct ToolDefinition
    {
        ToolKind Kind{ ToolKind::Git };
        std::wstring DisplayName;
        std::wstring ExecutableName;
        bool Required{ true };
    };

    class ToolRegistry final
    {
    public:
        static ToolRegistry CreateDefault();

        void Register(ToolDefinition definition);

        [[nodiscard]] std::optional<ToolDefinition> Find(ToolKind kind) const;
        [[nodiscard]] std::vector<ToolDefinition> const& Tools() const noexcept;
        [[nodiscard]] ToolCommand CreateCommand(
            ToolKind kind,
            std::vector<std::wstring> arguments,
            std::filesystem::path workingDirectory = {},
            std::wstring displayName = {}) const;

    private:
        std::vector<ToolDefinition> m_tools;
    };
}
