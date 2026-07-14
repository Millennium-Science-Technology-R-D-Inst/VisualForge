#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <utility>
#include <winrt/Windows.Foundation.h>

namespace VisualForge::Integration
{
    struct ProjectConfigurationEntry
    {
        std::wstring Configuration;
        std::wstring Platform;
    };

    class ProjectWorkspaceService final
    {
    public:
        [[nodiscard]] static winrt::Windows::Foundation::IAsyncAction ReadConfigurations(
            std::filesystem::path const& projectPath,
            std::vector<ProjectConfigurationEntry>& result);

        [[nodiscard]] static winrt::Windows::Foundation::IAsyncOperation<bool> AddItem(
            std::filesystem::path const& projectPath,
            std::filesystem::path const& itemPath,
            std::wstring itemKind,
            std::wstring& error);

        [[nodiscard]] static winrt::Windows::Foundation::IAsyncOperation<bool> ContainsItem(
            std::filesystem::path const& projectPath,
            std::filesystem::path const& itemPath);

        [[nodiscard]] static winrt::Windows::Foundation::IAsyncOperation<bool> RenameItem(
            std::filesystem::path const& projectPath,
            std::filesystem::path const& oldPath,
            std::filesystem::path const& newPath,
            std::wstring& error);

        [[nodiscard]] static winrt::Windows::Foundation::IAsyncOperation<bool> RemoveItem(
            std::filesystem::path const& projectPath,
            std::filesystem::path const& itemPath,
            std::wstring& error);
    };
}
