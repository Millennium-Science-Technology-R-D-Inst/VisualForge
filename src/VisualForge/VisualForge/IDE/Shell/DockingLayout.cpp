#include "pch.h"

#include "IDE/Shell/DockingLayout.h"

#include <algorithm>

namespace VisualForge::IDE::Shell
{
    namespace
    {
        std::vector<DockCommand> ToolWindowCommands()
        {
            return {
                { DockCommandKind::Activate, L"Activate", L"\xE8A5" },
                { DockCommandKind::Pin, L"Pin", L"\xE718" },
                { DockCommandKind::AutoHide, L"Auto Hide", L"\xE77A" },
                { DockCommandKind::Float, L"Float", L"\xE740" },
                { DockCommandKind::Maximize, L"Maximize", L"\xE922" },
                { DockCommandKind::Close, L"Close", L"\xE711" },
            };
        }
    }

    DockingLayout DockingLayout::CreateVisualStudioLikeDefault()
    {
        DockingLayout layout;

        layout.m_targets = {
            { DockSide::Left, L"Dock Left", L"\xE76B" },
            { DockSide::Right, L"Dock Right", L"\xE76C" },
            { DockSide::Bottom, L"Dock Bottom", L"\xE74B" },
            { DockSide::Document, L"Document Tab Group", L"\xE8A5" },
        };

        auto commands = ToolWindowCommands();
        layout.m_contents = {
            { L"options", L"Options", DockSide::Left, DockState::Docked, true, true, false, commands, DockSide::Left, true },
            { L"solutionExplorer", L"Solution Explorer", DockSide::Right, DockState::Docked, true, true, true, commands, DockSide::Right },
            { L"gitChanges", L"Git Changes", DockSide::Right, DockState::Docked, true, true, true, commands, DockSide::Right },
            { L"properties", L"Properties", DockSide::Right, DockState::Docked, true, true, true, commands, DockSide::Right },
            { L"errorList", L"Error List", DockSide::Bottom, DockState::Docked, true, true, true, commands, DockSide::Bottom },
            { L"output", L"Output", DockSide::Bottom, DockState::Docked, true, true, true, commands, DockSide::Bottom },
            { L"terminal", L"Developer PowerShell", DockSide::Bottom, DockState::Docked, true, true, true, commands, DockSide::Bottom },
            { L"diagnostics", L"Diagnostic Tools", DockSide::Right, DockState::Floating, true, true, true, commands, DockSide::Right },
            { L"documentOutline", L"Document Outline", DockSide::Right, DockState::Docked, true, true, true, commands, DockSide::Right },
        };

        return layout;
    }

    std::vector<DockableContentDescriptor> const& DockingLayout::Contents() const noexcept
    {
        return m_contents;
    }

    std::vector<DockTarget> const& DockingLayout::Targets() const noexcept
    {
        return m_targets;
    }

    DockableContentDescriptor const* DockingLayout::FindContent(std::wstring const& contentId) const noexcept
    {
        auto found = std::find_if(m_contents.begin(), m_contents.end(), [&](DockableContentDescriptor const& item)
        {
            return item.Id == contentId;
        });

        return found == m_contents.end() ? nullptr : &(*found);
    }

    DockableContentDescriptor* DockingLayout::FindContent(std::wstring const& contentId) noexcept
    {
        auto found = std::find_if(m_contents.begin(), m_contents.end(), [&](DockableContentDescriptor const& item)
        {
            return item.Id == contentId;
        });

        return found == m_contents.end() ? nullptr : &(*found);
    }

    DockDragSession DockingLayout::CreateDragSession(std::wstring const& contentId) const
    {
        auto const found = FindContent(contentId);

        DockDragSession session;
        session.ContentId = contentId;
        session.Title = found ? found->Title : contentId;
        session.Targets = m_targets;
        session.PreviewWindow = { contentId, session.Title, 760, 520, true };
        return session;
    }

    bool DockingLayout::ExecuteCommand(std::wstring const& contentId, DockCommandKind command)
    {
        switch (command)
        {
        case DockCommandKind::Activate:
            return Activate(contentId);
        case DockCommandKind::Pin:
            return Pin(contentId);
        case DockCommandKind::AutoHide:
            return AutoHide(contentId);
        case DockCommandKind::Float:
            return Float(contentId);
        case DockCommandKind::Dock:
            return Dock(contentId, DockSide::Right);
        case DockCommandKind::Maximize:
            if (auto content = FindContent(contentId))
            {
                content->IsMaximized = !content->IsMaximized;
                return Activate(contentId);
            }

            return false;
        case DockCommandKind::Close:
            return Close(contentId);
        default:
            return false;
        }
    }

    bool DockingLayout::Dock(std::wstring const& contentId, DockSide side)
    {
        if (auto content = FindContent(contentId))
        {
            content->State = side == DockSide::Document ? DockState::DocumentTabbed : DockState::Docked;
            content->CurrentDock = side;
            content->IsMaximized = false;
            content->FloatingWindow = {};
            return Activate(contentId);
        }

        return false;
    }

    bool DockingLayout::Float(std::wstring const& contentId, int width, int height)
    {
        if (auto content = FindContent(contentId); content && content->CanFloat)
        {
            content->State = DockState::Floating;
            content->IsMaximized = false;
            content->FloatingWindow = { content->Id, content->Title, width, height, true };
            return Activate(contentId);
        }

        return false;
    }

    bool DockingLayout::AutoHide(std::wstring const& contentId)
    {
        if (auto content = FindContent(contentId); content && content->CanAutoHide)
        {
            content->State = DockState::AutoHidden;
            content->IsMaximized = false;
            return Activate(contentId);
        }

        return false;
    }

    bool DockingLayout::Pin(std::wstring const& contentId)
    {
        if (auto content = FindContent(contentId))
        {
            if (content->State == DockState::AutoHidden)
            {
                content->State = DockState::Docked;
            }

            return Activate(contentId);
        }

        return false;
    }

    bool DockingLayout::Close(std::wstring const& contentId)
    {
        if (auto content = FindContent(contentId); content && content->CanClose)
        {
            content->State = DockState::Closed;
            content->IsActive = false;
            content->IsMaximized = false;
            content->FloatingWindow = {};
            return true;
        }

        return false;
    }

    bool DockingLayout::Activate(std::wstring const& contentId)
    {
        auto activated = false;
        for (auto& content : m_contents)
        {
            auto const isActive = content.Id == contentId && content.State != DockState::Closed;
            content.IsActive = isActive;
            activated = activated || isActive;
        }

        return activated;
    }

    std::wstring_view ToDockStateName(DockState state) noexcept
    {
        switch (state)
        {
        case DockState::Docked:
            return L"Docked";
        case DockState::AutoHidden:
            return L"Auto Hidden";
        case DockState::Floating:
            return L"Floating";
        case DockState::DocumentTabbed:
            return L"Document Tabbed";
        case DockState::Closed:
            return L"Closed";
        default:
            return L"Unknown";
        }
    }

    std::wstring_view ToDockCommandName(DockCommandKind command) noexcept
    {
        switch (command)
        {
        case DockCommandKind::Activate:
            return L"Activate";
        case DockCommandKind::Pin:
            return L"Pin";
        case DockCommandKind::AutoHide:
            return L"Auto Hide";
        case DockCommandKind::Float:
            return L"Float";
        case DockCommandKind::Dock:
            return L"Dock";
        case DockCommandKind::Maximize:
            return L"Maximize";
        case DockCommandKind::Close:
            return L"Close";
        default:
            return L"Unknown";
        }
    }
}
