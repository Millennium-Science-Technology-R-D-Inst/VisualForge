#include "pch.h"

#include "IDE/Shell/ShellWorkspace.h"

#include <algorithm>

namespace VisualForge::IDE::Shell
{
    ShellWorkspace ShellWorkspace::CreateDefault()
    {
        ShellWorkspace workspace;
        workspace.m_layout = ShellLayout::CreateDefault();
        workspace.m_docking = DockingLayout::CreateVisualStudioLikeDefault();

        for (auto const& command : workspace.m_layout.Commands())
        {
            workspace.m_commands.push_back({ command, CommandPlacement::Menu, L"Main Menu", true, true });
            workspace.m_commands.push_back({ command, CommandPlacement::CommandPalette, L"Command Palette", true, true });
        }

        workspace.m_documentGroups = {
            { L"main", L"Main Document Group", workspace.m_layout.Documents(), 0 },
            { L"preview", L"Preview Tab Group", {}, 0 },
        };

        for (auto const& content : workspace.m_docking.Contents())
        {
            workspace.m_toolWindowStates.push_back({
                content.Id,
                content.State,
                content.PreferredDock,
                content.Id == L"solutionExplorer",
                content.State != DockState::AutoHidden
            });
        }

        return workspace;
    }

    ShellLayout const& ShellWorkspace::Layout() const noexcept
    {
        return m_layout;
    }

    DockingLayout const& ShellWorkspace::Docking() const noexcept
    {
        return m_docking;
    }

    std::vector<CommandBinding> const& ShellWorkspace::Commands() const noexcept
    {
        return m_commands;
    }

    std::vector<DocumentGroup> const& ShellWorkspace::DocumentGroups() const noexcept
    {
        return m_documentGroups;
    }

    std::vector<ToolWindowRuntimeState> const& ShellWorkspace::ToolWindowStates() const noexcept
    {
        return m_toolWindowStates;
    }

    WorkspaceMode ShellWorkspace::Mode() const noexcept
    {
        return m_mode;
    }

    void ShellWorkspace::SetMode(WorkspaceMode mode) noexcept
    {
        m_mode = mode;
    }

    WorkspaceLayoutProfile const& ShellWorkspace::Profile(WorkspaceMode mode) const noexcept
    {
        return mode == WorkspaceMode::Debugging ? m_debuggingProfile : m_editingProfile;
    }

    WorkspaceLayoutProfile& ShellWorkspace::Profile(WorkspaceMode mode) noexcept
    {
        return mode == WorkspaceMode::Debugging ? m_debuggingProfile : m_editingProfile;
    }

    std::optional<ToolWindowRuntimeState> ShellWorkspace::FindToolWindowState(std::wstring const& contentId) const
    {
        auto found = std::find_if(m_toolWindowStates.begin(), m_toolWindowStates.end(), [&](ToolWindowRuntimeState const& item)
        {
            return item.ContentId == contentId;
        });

        if (found == m_toolWindowStates.end())
        {
            return std::nullopt;
        }

        return *found;
    }

    bool ShellWorkspace::ExecuteDockCommand(std::wstring const& contentId, DockCommandKind command)
    {
        auto const changed = m_docking.ExecuteCommand(contentId, command);
        if (changed)
        {
            SyncToolWindowStates();
        }

        return changed;
    }

    bool ShellWorkspace::DockToolWindow(std::wstring const& contentId, DockSide side)
    {
        auto const changed = m_docking.Dock(contentId, side);
        if (changed)
        {
            SyncToolWindowStates();
        }

        return changed;
    }

    DockDragSession ShellWorkspace::BeginDockDrag(std::wstring const& contentId) const
    {
        return m_docking.CreateDragSession(contentId);
    }

    void ShellWorkspace::SyncToolWindowStates()
    {
        m_toolWindowStates.clear();
        for (auto const& content : m_docking.Contents())
        {
            m_toolWindowStates.push_back({
                content.Id,
                content.State,
                content.CurrentDock,
                content.IsActive,
                content.State != DockState::AutoHidden
            });
        }
    }

    std::wstring_view ToWorkspaceModeName(WorkspaceMode mode) noexcept
    {
        return mode == WorkspaceMode::Debugging ? L"Debugging" : L"Editing";
    }
}
