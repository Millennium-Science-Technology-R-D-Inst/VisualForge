#pragma once

#include "IDE/Shell/ShellLayout.h"

#include <string>
#include <vector>

namespace VisualForge::IDE::Shell
{
    enum class DockState
    {
        Docked,
        AutoHidden,
        Floating,
        DocumentTabbed,
        Closed
    };

    enum class DockCommandKind
    {
        Activate,
        Pin,
        AutoHide,
        Float,
        Dock,
        Maximize,
        Close
    };

    struct DockCommand
    {
        DockCommandKind Kind{ DockCommandKind::Activate };
        std::wstring Text;
        std::wstring IconGlyph;
    };

    struct DockTarget
    {
        DockSide Side{ DockSide::Document };
        std::wstring Name;
        std::wstring Glyph;
    };

    struct FloatingWindowDescriptor
    {
        std::wstring ContentId;
        std::wstring Title;
        int Width{ 720 };
        int Height{ 480 };
        bool OwnedByMainWindow{ true };
    };

    struct DockableContentDescriptor
    {
        std::wstring Id;
        std::wstring Title;
        DockSide PreferredDock{ DockSide::Right };
        DockState State{ DockState::Docked };
        bool CanFloat{ true };
        bool CanAutoHide{ true };
        bool CanClose{ true };
        std::vector<DockCommand> Commands;
        DockSide CurrentDock{ DockSide::Right };
        bool IsActive{ false };
        bool IsMaximized{ false };
        FloatingWindowDescriptor FloatingWindow;
    };

    struct DockDragSession
    {
        std::wstring ContentId;
        std::wstring Title;
        std::vector<DockTarget> Targets;
        FloatingWindowDescriptor PreviewWindow;
    };

    class DockingLayout final
    {
    public:
        static DockingLayout CreateVisualStudioLikeDefault();

        [[nodiscard]] std::vector<DockableContentDescriptor> const& Contents() const noexcept;
        [[nodiscard]] std::vector<DockTarget> const& Targets() const noexcept;
        [[nodiscard]] DockableContentDescriptor const* FindContent(std::wstring const& contentId) const noexcept;
        [[nodiscard]] DockableContentDescriptor* FindContent(std::wstring const& contentId) noexcept;
        [[nodiscard]] DockDragSession CreateDragSession(std::wstring const& contentId) const;
        [[nodiscard]] bool ExecuteCommand(std::wstring const& contentId, DockCommandKind command);
        [[nodiscard]] bool Dock(std::wstring const& contentId, DockSide side);
        [[nodiscard]] bool Float(std::wstring const& contentId, int width = 760, int height = 520);
        [[nodiscard]] bool AutoHide(std::wstring const& contentId);
        [[nodiscard]] bool Pin(std::wstring const& contentId);
        [[nodiscard]] bool Close(std::wstring const& contentId);
        [[nodiscard]] bool Activate(std::wstring const& contentId);

    private:
        std::vector<DockableContentDescriptor> m_contents;
        std::vector<DockTarget> m_targets;
    };

    [[nodiscard]] std::wstring_view ToDockStateName(DockState state) noexcept;
    [[nodiscard]] std::wstring_view ToDockCommandName(DockCommandKind command) noexcept;
}
