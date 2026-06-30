#include "pch.h"

#include "Tool/ToolRegistry.h"

#include <algorithm>

namespace VisualForge::Tool
{
    ToolRegistry ToolRegistry::CreateDefault()
    {
        ToolRegistry registry;
        registry.Register({ ToolKind::Clangd, std::wstring{ ToDisplayName(ToolKind::Clangd) }, std::wstring{ ToExecutableName(ToolKind::Clangd) }, true });
        registry.Register({ ToolKind::Git, std::wstring{ ToDisplayName(ToolKind::Git) }, std::wstring{ ToExecutableName(ToolKind::Git) }, true });
        registry.Register({ ToolKind::MSBuild, std::wstring{ ToDisplayName(ToolKind::MSBuild) }, std::wstring{ ToExecutableName(ToolKind::MSBuild) }, true });
        registry.Register({ ToolKind::Compiler, std::wstring{ ToDisplayName(ToolKind::Compiler) }, std::wstring{ ToExecutableName(ToolKind::Compiler) }, true });
        registry.Register({ ToolKind::Vcpkg, std::wstring{ ToDisplayName(ToolKind::Vcpkg) }, std::wstring{ ToExecutableName(ToolKind::Vcpkg) }, false });
        registry.Register({ ToolKind::NuGet, std::wstring{ ToDisplayName(ToolKind::NuGet) }, std::wstring{ ToExecutableName(ToolKind::NuGet) }, false });
        registry.Register({ ToolKind::LLDB, std::wstring{ ToDisplayName(ToolKind::LLDB) }, std::wstring{ ToExecutableName(ToolKind::LLDB) }, true });
        return registry;
    }

    void ToolRegistry::Register(ToolDefinition definition)
    {
        auto existing = std::find_if(m_tools.begin(), m_tools.end(), [&](ToolDefinition const& item)
        {
            return item.Kind == definition.Kind;
        });

        if (existing == m_tools.end())
        {
            m_tools.push_back(std::move(definition));
        }
        else
        {
            *existing = std::move(definition);
        }
    }

    std::optional<ToolDefinition> ToolRegistry::Find(ToolKind kind) const
    {
        auto existing = std::find_if(m_tools.begin(), m_tools.end(), [&](ToolDefinition const& item)
        {
            return item.Kind == kind;
        });

        if (existing == m_tools.end())
        {
            return std::nullopt;
        }

        return *existing;
    }

    std::vector<ToolDefinition> const& ToolRegistry::Tools() const noexcept
    {
        return m_tools;
    }

    ToolCommand ToolRegistry::CreateCommand(
        ToolKind kind,
        std::vector<std::wstring> arguments,
        std::filesystem::path workingDirectory,
        std::wstring displayName) const
    {
        auto definition = Find(kind);

        ToolCommand command;
        command.Kind = kind;
        command.Executable = definition ? definition->ExecutableName : std::wstring{ ToExecutableName(kind) };
        command.Arguments = std::move(arguments);
        command.WorkingDirectory = std::move(workingDirectory);
        command.DisplayName = displayName.empty() ? std::wstring{ ToDisplayName(kind) } : std::move(displayName);
        return command;
    }
}
