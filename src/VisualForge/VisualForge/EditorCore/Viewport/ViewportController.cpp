#include "pch.h"

#include "EditorCore/Viewport/ViewportController.h"

#include <algorithm>
#include <cmath>

namespace VisualForge::EditorCore::Viewport
{
    void ViewportController::SetViewportHeight(float height)
    {
        m_viewportHeight = (std::max)(1.0f, height);
        Recalculate();
    }

    void ViewportController::SetLineHeight(float lineHeight)
    {
        m_lineHeight = (std::max)(1.0f, lineHeight);
        Recalculate();
    }

    void ViewportController::SetBufferLines(std::size_t bufferLines)
    {
        m_bufferLines = bufferLines;
        Recalculate();
    }

    void ViewportController::Scroll(float deltaY)
    {
        auto const maxOffset = static_cast<float>(m_totalLines > 0 ? m_totalLines - 1 : 0) * m_lineHeight;
        m_scrollOffsetY = (std::clamp)(m_scrollOffsetY + deltaY, 0.0f, maxOffset);
        Recalculate();
    }

    void ViewportController::ScrollToLine(std::size_t line)
    {
        auto const clamped = (std::min)(line, m_totalLines > 0 ? m_totalLines - 1 : 0);
        m_scrollOffsetY = static_cast<float>(clamped) * m_lineHeight;
        Recalculate();
    }

    void ViewportController::UpdateLayout(Layout::LineLayoutEngine const& layout)
    {
        m_totalLines = (std::max<std::size_t>)(1, layout.LineCount());
        Recalculate();
    }

    VisibleRange ViewportController::GetVisibleRange() const noexcept
    {
        auto const bufferedFirst = m_firstVisibleLine > m_bufferLines ? m_firstVisibleLine - m_bufferLines : 0;
        auto const bufferedLast = (std::min)(m_totalLines - 1, m_lastVisibleLine + m_bufferLines);
        return { m_firstVisibleLine, m_lastVisibleLine, bufferedFirst, bufferedLast };
    }

    float ViewportController::ScrollOffsetY() const noexcept
    {
        return m_scrollOffsetY;
    }

    void ViewportController::Recalculate()
    {
        m_firstVisibleLine = (std::min<std::size_t>)(
            static_cast<std::size_t>(std::floor(m_scrollOffsetY / m_lineHeight)),
            m_totalLines > 0 ? m_totalLines - 1 : 0);

        auto visibleLineCount = static_cast<std::size_t>(std::ceil(m_viewportHeight / m_lineHeight)) + 1;
        visibleLineCount = (std::max<std::size_t>)(1, visibleLineCount);
        m_lastVisibleLine = (std::min)(m_totalLines - 1, m_firstVisibleLine + visibleLineCount - 1);
    }
}
