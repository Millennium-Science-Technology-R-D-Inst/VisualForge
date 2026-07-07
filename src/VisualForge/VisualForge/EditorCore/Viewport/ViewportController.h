#pragma once

#include "EditorCore/Layout/LineLayoutEngine.h"
#include "EditorCore/Viewport/VisibleRange.h"

#include <cstddef>

namespace VisualForge::EditorCore::Viewport
{
    class ViewportController final
    {
    public:
        void SetViewportHeight(float height);
        void SetLineHeight(float lineHeight);
        void SetBufferLines(std::size_t bufferLines);
        void Scroll(float deltaY);
        void ScrollToLine(std::size_t line);
        void UpdateLayout(Layout::LineLayoutEngine const& layout);

        [[nodiscard]] VisibleRange GetVisibleRange() const noexcept;
        [[nodiscard]] float ScrollOffsetY() const noexcept;

    private:
        void Recalculate();

        std::size_t m_totalLines{ 1 };
        std::size_t m_firstVisibleLine{ 0 };
        std::size_t m_lastVisibleLine{ 0 };
        std::size_t m_bufferLines{ 50 };
        float m_scrollOffsetY{ 0.0f };
        float m_viewportHeight{ 600.0f };
        float m_lineHeight{ 18.0f };
    };
}
