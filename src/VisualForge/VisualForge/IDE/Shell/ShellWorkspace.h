#pragma once

#include "IDE/Shell/DockingLayout.h"
#include "IDE/Shell/ShellLayout.h"

#include <optional>
#include <string>
#include <vector>

namespace VisualForge::IDE::Shell
{
    enum class WorkspaceMode
    {
        Editing,
        Debugging
    };

    struct WorkspaceLayoutProfile
    {
        int BottomToolHeight{ 245 };
        int RightToolTabIndex{ 0 };
        int BottomToolTabIndex{ 1 };
        bool ShowDiagnosticTools{ false };
    };

    enum class CommandPlacement
    {
        Menu,
        Toolbar,
        ContextMenu,
        CommandPalette
    };

    struct CommandBinding
    {
        ShellCommand Command;
        CommandPlacement Placement{ CommandPlacement::Menu };
        std::wstring Group;
        bool Enabled{ true };
        bool Visible{ true };
    };

    struct DocumentGroup
    {
        std::wstring Id;
        std::wstring Title;
        std::vector<DocumentDescriptor> Documents;
        int ActiveDocumentIndex{ 0 };
    };

    struct ToolWindowRuntimeState
    {
        std::wstring ContentId;
        DockState State{ DockState::Docked };
        DockSide CurrentDock{ DockSide::Right };
        bool IsActive{ false };
        bool IsPinned{ true };
    };

    class ShellWorkspace final
    {
    public:
        static ShellWorkspace CreateDefault();

        [[nodiscard]] ShellLayout const& Layout() const noexcept;
        [[nodiscard]] DockingLayout const& Docking() const noexcept;
        [[nodiscard]] std::vector<CommandBinding> const& Commands() const noexcept;
        [[nodiscard]] std::vector<DocumentGroup> const& DocumentGroups() const noexcept;
        [[nodiscard]] std::vector<ToolWindowRuntimeState> const& ToolWindowStates() const noexcept;
        [[nodiscard]] WorkspaceMode Mode() const noexcept;
        void SetMode(WorkspaceMode mode) noexcept;
        [[nodiscard]] WorkspaceLayoutProfile const& Profile(WorkspaceMode mode) const noexcept;
        [[nodiscard]] WorkspaceLayoutProfile& Profile(WorkspaceMode mode) noexcept;
        [[nodiscard]] std::optional<ToolWindowRuntimeState> FindToolWindowState(std::wstring const& contentId) const;
        [[nodiscard]] bool ExecuteDockCommand(std::wstring const& contentId, DockCommandKind command);
        [[nodiscard]] bool DockToolWindow(std::wstring const& contentId, DockSide side);
        [[nodiscard]] DockDragSession BeginDockDrag(std::wstring const& contentId) const;

    private:
        void SyncToolWindowStates();

        ShellLayout m_layout;
        DockingLayout m_docking;
        std::vector<CommandBinding> m_commands;
        std::vector<DocumentGroup> m_documentGroups;
        std::vector<ToolWindowRuntimeState> m_toolWindowStates;
        WorkspaceMode m_mode{ WorkspaceMode::Editing };
        WorkspaceLayoutProfile m_editingProfile{ 245, 0, 1, false };
        WorkspaceLayoutProfile m_debuggingProfile{ 320, 3, 3, true };
    };

    [[nodiscard]] std::wstring_view ToWorkspaceModeName(WorkspaceMode mode) noexcept;
}
