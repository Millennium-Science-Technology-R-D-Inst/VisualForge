#pragma once

#include "EditorCore/Layout/TextLine.h"
#include "EditorCore/TextModel/TextBuffer.h"

#include <cstddef>
#include <vector>

namespace VisualForge::EditorCore::Layout
{
    class LineLayoutEngine final
    {
    public:
        void Invalidate(std::size_t startLine, std::size_t endLine);
        void Reflow(TextModel::TextBuffer const& buffer);

        [[nodiscard]] std::vector<TextLine> const& Lines() const noexcept;
        [[nodiscard]] std::size_t LineCount() const noexcept;
        [[nodiscard]] TextLine const* LineAt(std::size_t lineIndex) const noexcept;
        [[nodiscard]] std::size_t DirtyStartLine() const noexcept;
        [[nodiscard]] std::size_t DirtyEndLine() const noexcept;

    private:
        std::vector<TextLine> m_lines;
        std::size_t m_dirtyStartLine{ 0 };
        std::size_t m_dirtyEndLine{ static_cast<std::size_t>(-1) };
        std::uint64_t m_bufferVersion{ 0 };
    };
}
