#include "pch.h"

#include "EditorCore/Rendering/EditorRenderer.h"

namespace VisualForge::EditorCore::Rendering
{
    void EditorRenderer::Render(
        TextModel::TextBuffer const& buffer,
        Layout::LineLayoutEngine const& layout,
        Viewport::ViewportController const& viewport)
    {
        (void)buffer;
        auto const range = viewport.GetVisibleRange();
        m_lastRenderedLineCount = 0;
        m_lastRenderedLines.clear();

        for (auto line = range.bufferedFirstLine; line <= range.bufferedLastLine && line < layout.LineCount(); ++line)
        {
            if (auto const* textLine = layout.LineAt(line))
            {
                DrawTextLine(line, *textLine, buffer);
                ++m_lastRenderedLineCount;
            }
        }
    }

    std::size_t EditorRenderer::LastRenderedLineCount() const noexcept
    {
        return m_lastRenderedLineCount;
    }

    std::vector<RenderedTextLine> const& EditorRenderer::LastRenderedLines() const noexcept
    {
        return m_lastRenderedLines;
    }

    bool EditorRenderer::IsDirectWriteReady() const noexcept
    {
        return m_context.IsInitialized();
    }

    void EditorRenderer::DrawTextLine(std::size_t lineIndex, Layout::TextLine const& line, TextModel::TextBuffer const& buffer)
    {
        auto text = buffer.GetText(line.startOffset, line.length);
        auto layout = m_context.CreateTextLayout(text, 100000.0f, line.height > 1.0f ? line.height : 18.0f);

        DWRITE_TEXT_METRICS metrics{};
        DirectWriteRenderMetrics renderMetrics{};
        if (layout)
        {
            layout->GetMetrics(&metrics);
            renderMetrics = m_context.DrawTextLayout(layout.get());
        }

        m_lastRenderedLines.push_back({
            lineIndex,
            line.startOffset,
            line.length,
            metrics.widthIncludingTrailingWhitespace > 0.0f ? metrics.widthIncludingTrailingWhitespace : line.width,
            metrics.height > 0.0f ? metrics.height : line.height,
            renderMetrics.GlyphRunCount,
            renderMetrics.GlyphCount
        });
    }
}
