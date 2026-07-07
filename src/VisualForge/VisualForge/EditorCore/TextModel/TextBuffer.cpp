#include "pch.h"

#include "EditorCore/TextModel/TextBuffer.h"

namespace VisualForge::EditorCore::TextModel
{
    TextBuffer::TextBuffer(std::wstring text)
    {
        Load(std::move(text));
    }

    void TextBuffer::Load(std::wstring text)
    {
        m_pieceTable.Reset(std::move(text));
        ++m_version;
    }

    void TextBuffer::Insert(std::size_t position, std::wstring_view text)
    {
        if (text.empty())
        {
            return;
        }

        m_pieceTable.Insert(position, text);
        ++m_version;
    }

    std::wstring TextBuffer::Delete(std::size_t position, std::size_t length)
    {
        auto deletedText = m_pieceTable.Delete(position, length);
        if (!deletedText.empty())
        {
            ++m_version;
        }

        return deletedText;
    }

    wchar_t TextBuffer::CharAt(std::size_t position) const
    {
        return m_pieceTable.CharAt(position);
    }

    std::wstring TextBuffer::GetText(std::size_t start, std::size_t length) const
    {
        return m_pieceTable.GetText(start, length);
    }

    std::size_t TextBuffer::Length() const noexcept
    {
        return m_pieceTable.Length();
    }

    bool TextBuffer::Empty() const noexcept
    {
        return m_pieceTable.Empty();
    }

    std::uint64_t TextBuffer::Version() const noexcept
    {
        return m_version;
    }

    TextSnapshot TextBuffer::CreateSnapshot() const
    {
        return TextSnapshot{ m_pieceTable.GetAllText() };
    }
}
