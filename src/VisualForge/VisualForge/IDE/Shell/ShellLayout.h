#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace VisualForge::IDE::Shell
{
    enum class DockSide
    {
        Left,
        Right,
        Bottom,
        Document
    };

    struct ShellCommand
    {
        std::wstring Id;
        std::wstring Text;
        std::wstring Shortcut;
        std::wstring IconGlyph;
    };

    struct ToolWindowDescriptor
    {
        std::wstring Id;
        std::wstring Title;
        DockSide DefaultDock{ DockSide::Right };
        bool VisibleByDefault{ true };
    };

    struct DocumentDescriptor
    {
        std::wstring Title;
        std::wstring Path;
        bool IsDirty{ false };
    };

    struct SettingsDescriptor
    {
        std::wstring Category;
        std::wstring Page;
        std::vector<std::wstring> Options;
    };

    class ShellLayout final
    {
    public:
        static ShellLayout CreateDefault();

        [[nodiscard]] std::vector<ShellCommand> const& Commands() const noexcept;
        [[nodiscard]] std::vector<ToolWindowDescriptor> const& ToolWindows() const noexcept;
        [[nodiscard]] std::vector<DocumentDescriptor> const& Documents() const noexcept;
        [[nodiscard]] std::vector<SettingsDescriptor> const& SettingsPages() const noexcept;

    private:
        std::vector<ShellCommand> m_commands;
        std::vector<ToolWindowDescriptor> m_toolWindows;
        std::vector<DocumentDescriptor> m_documents;
        std::vector<SettingsDescriptor> m_settingsPages;
    };

    [[nodiscard]] std::wstring_view ToDockSideName(DockSide side) noexcept;
}
