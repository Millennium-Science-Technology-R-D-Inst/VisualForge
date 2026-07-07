#pragma once

#include "IDE/Shell/DockingLayout.h"
#include "IDE/Shell/ShellLayout.h"

#include <optional>
#include <string>
#include <vector>

namespace VisualForge::IDE::Shell
{
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
    };
}
