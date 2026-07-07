#pragma once

#include "EditorCore/Layout/LineLayoutEngine.h"
#include "EditorCore/Rendering/DirectWriteContext.h"
#include "EditorCore/TextModel/TextBuffer.h"
#include "EditorCore/Viewport/ViewportController.h"

#include <vector>

namespace VisualForge::EditorCore::Rendering
{
    struct RenderedTextLine
    {
        std::size_t LineIndex{ 0 };
        std::size_t StartOffset{ 0 };
        std::size_t Length{ 0 };
        float Width{ 0 };
        float Height{ 0 };
        std::size_t GlyphRunCount{ 0 };
        std::size_t GlyphCount{ 0 };
    };

    class EditorRenderer final
    {
    public:
        void Render(
            TextModel::TextBuffer const& buffer,
            Layout::LineLayoutEngine const& layout,
            Viewport::ViewportController const& viewport);

        [[nodiscard]] std::size_t LastRenderedLineCount() const noexcept;
        [[nodiscard]] std::vector<RenderedTextLine> const& LastRenderedLines() const noexcept;
        [[nodiscard]] bool IsDirectWriteReady() const noexcept;

    private:
        void DrawTextLine(std::size_t lineIndex, Layout::TextLine const& line, TextModel::TextBuffer const& buffer);

        DirectWriteContext m_context;
        std::size_t m_lastRenderedLineCount{ 0 };
        std::vector<RenderedTextLine> m_lastRenderedLines;
    };
}
