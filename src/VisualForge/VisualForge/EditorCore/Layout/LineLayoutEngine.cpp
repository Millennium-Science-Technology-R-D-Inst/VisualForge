#include "pch.h"

#include "EditorCore/Layout/LineLayoutEngine.h"

#include <algorithm>

namespace VisualForge::EditorCore::Layout
{
    void LineLayoutEngine::Invalidate(std::size_t startLine, std::size_t endLine)
    {
        if (endLine < startLine)
        {
            std::swap(startLine, endLine);
        }

        m_dirtyStartLine = (std::min)(m_dirtyStartLine, startLine);
        m_dirtyEndLine = (std::max)(m_dirtyEndLine, endLine);
    }

    void LineLayoutEngine::Reflow(TextModel::TextBuffer const& buffer)
    {
        if (m_bufferVersion == buffer.Version() && m_dirtyEndLine == static_cast<std::size_t>(-1))
        {
            return;
        }

        auto snapshot = buffer.CreateSnapshot();
        auto const text = snapshot.Text();
        m_lines.clear();

        std::size_t lineStart = 0;
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            if (text[index] == L'\n')
            {
                auto length = index - lineStart;
                if (length > 0 && text[index - 1] == L'\r')
                {
                    --length;
                }

                m_lines.push_back({ lineStart, length, 18.0f, static_cast<float>(length) * 8.0f });
                lineStart = index + 1;
            }
        }

        if (lineStart <= text.size())
        {
            auto const length = text.size() - lineStart;
            m_lines.push_back({ lineStart, length, 18.0f, static_cast<float>(length) * 8.0f });
        }

        if (m_lines.empty())
        {
            m_lines.push_back({});
        }

        m_bufferVersion = buffer.Version();
        m_dirtyStartLine = 0;
        m_dirtyEndLine = static_cast<std::size_t>(-1);
    }

    std::vector<TextLine> const& LineLayoutEngine::Lines() const noexcept
    {
        return m_lines;
    }

    std::size_t LineLayoutEngine::LineCount() const noexcept
    {
        return m_lines.size();
    }

    TextLine const* LineLayoutEngine::LineAt(std::size_t lineIndex) const noexcept
    {
        if (lineIndex >= m_lines.size())
        {
            return nullptr;
        }

        return &m_lines[lineIndex];
    }

    std::size_t LineLayoutEngine::DirtyStartLine() const noexcept
    {
        return m_dirtyStartLine;
    }

    std::size_t LineLayoutEngine::DirtyEndLine() const noexcept
    {
        return m_dirtyEndLine;
    }
}
