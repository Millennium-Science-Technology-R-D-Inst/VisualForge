#include "pch.h"

#include "EditorCore/TextModel/TextSnapshot.h"

#include <algorithm>
#include <stdexcept>

namespace VisualForge::EditorCore::TextModel
{
    TextSnapshot::TextSnapshot(std::wstring text) :
        m_text(std::move(text))
    {
    }

    wchar_t TextSnapshot::CharAt(std::size_t offset) const
    {
        if (offset >= m_text.size())
        {
            throw std::out_of_range("TextSnapshot::CharAt offset is outside the snapshot.");
        }

        return m_text[offset];
    }

    std::wstring TextSnapshot::GetText(std::size_t start, std::size_t length) const
    {
        if (start > m_text.size())
        {
            throw std::out_of_range("TextSnapshot::GetText start is outside the snapshot.");
        }

        auto const available = m_text.size() - start;
        return m_text.substr(start, (std::min)(length, available));
    }

    std::wstring_view TextSnapshot::Text() const noexcept
    {
        return m_text;
    }

    std::size_t TextSnapshot::Length() const noexcept
    {
        return m_text.size();
    }

    bool TextSnapshot::Empty() const noexcept
    {
        return m_text.empty();
    }
}
