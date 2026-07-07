#include "pch.h"

#include "EditorCore/TextModel/PieceTable.h"

#include <algorithm>
#include <stdexcept>

namespace VisualForge::EditorCore::TextModel
{
    PieceTable::PieceTable(std::wstring originalText)
    {
        Reset(std::move(originalText));
    }

    void PieceTable::Reset(std::wstring originalText)
    {
        m_original = std::move(originalText);
        m_addBuffer.clear();
        m_pieces.clear();
        m_length = m_original.size();

        if (!m_original.empty())
        {
            m_pieces.push_back({ BufferKind::Original, 0, m_original.size() });
        }
    }

    void PieceTable::Insert(std::size_t position, std::wstring_view text)
    {
        if (position > m_length)
        {
            throw std::out_of_range("PieceTable::Insert position is outside the buffer.");
        }

        if (text.empty())
        {
            return;
        }

        auto const addStart = m_addBuffer.size();
        m_addBuffer.append(text);
        Piece inserted{ BufferKind::Add, addStart, text.size() };

        if (m_pieces.empty())
        {
            m_pieces.push_back(inserted);
            m_length += text.size();
            return;
        }

        auto location = Locate(position);
        if (location.pieceIndex == m_pieces.size())
        {
            m_pieces.push_back(inserted);
        }
        else
        {
            auto current = m_pieces[location.pieceIndex];
            std::vector<Piece> replacement;

            if (location.innerOffset > 0)
            {
                replacement.push_back({ current.buffer, current.start, location.innerOffset });
            }

            replacement.push_back(inserted);

            if (location.innerOffset < current.length)
            {
                replacement.push_back({
                    current.buffer,
                    current.start + location.innerOffset,
                    current.length - location.innerOffset
                });
            }

            m_pieces.erase(m_pieces.begin() + static_cast<std::ptrdiff_t>(location.pieceIndex));
            m_pieces.insert(
                m_pieces.begin() + static_cast<std::ptrdiff_t>(location.pieceIndex),
                replacement.begin(),
                replacement.end());
        }

        m_length += text.size();
        Normalize();
    }

    std::wstring PieceTable::Delete(std::size_t position, std::size_t length)
    {
        if (position > m_length)
        {
            throw std::out_of_range("PieceTable::Delete position is outside the buffer.");
        }

        auto const actualLength = (std::min)(length, m_length - position);
        if (actualLength == 0)
        {
            return {};
        }

        auto deletedText = GetText(position, actualLength);
        auto const deleteEnd = position + actualLength;
        std::size_t cursor = 0;
        std::vector<Piece> nextPieces;
        nextPieces.reserve(m_pieces.size());

        for (auto const& piece : m_pieces)
        {
            auto const pieceStart = cursor;
            auto const pieceEnd = cursor + piece.length;

            if (pieceEnd <= position || pieceStart >= deleteEnd)
            {
                nextPieces.push_back(piece);
            }
            else
            {
                if (position > pieceStart)
                {
                    nextPieces.push_back({ piece.buffer, piece.start, position - pieceStart });
                }

                if (deleteEnd < pieceEnd)
                {
                    auto const keepStartInPiece = deleteEnd - pieceStart;
                    nextPieces.push_back({
                        piece.buffer,
                        piece.start + keepStartInPiece,
                        pieceEnd - deleteEnd
                    });
                }
            }

            cursor = pieceEnd;
        }

        m_pieces = std::move(nextPieces);
        m_length -= actualLength;
        Normalize();
        return deletedText;
    }

    std::wstring PieceTable::GetText(std::size_t start, std::size_t length) const
    {
        if (start > m_length)
        {
            throw std::out_of_range("PieceTable::GetText start is outside the buffer.");
        }

        auto const actualLength = (std::min)(length, m_length - start);
        std::wstring result;
        result.reserve(actualLength);

        auto const end = start + actualLength;
        std::size_t cursor = 0;
        for (auto const& piece : m_pieces)
        {
            auto const pieceStart = cursor;
            auto const pieceEnd = cursor + piece.length;
            if (pieceEnd > start && pieceStart < end)
            {
                auto const localStart = start > pieceStart ? start - pieceStart : 0;
                auto const localEnd = (std::min)(pieceEnd, end) - pieceStart;
                auto const buffer = BufferFor(piece);
                result.append(buffer.substr(piece.start + localStart, localEnd - localStart));
            }

            cursor = pieceEnd;
            if (cursor >= end)
            {
                break;
            }
        }

        return result;
    }

    std::wstring PieceTable::GetAllText() const
    {
        return GetText(0, m_length);
    }

    wchar_t PieceTable::CharAt(std::size_t position) const
    {
        if (position >= m_length)
        {
            throw std::out_of_range("PieceTable::CharAt position is outside the buffer.");
        }

        auto const location = Locate(position);
        auto const& piece = m_pieces[location.pieceIndex];
        return BufferFor(piece)[piece.start + location.innerOffset];
    }

    std::size_t PieceTable::Length() const noexcept
    {
        return m_length;
    }

    bool PieceTable::Empty() const noexcept
    {
        return m_length == 0;
    }

    PieceTable::PieceLocation PieceTable::Locate(std::size_t position) const
    {
        if (position > m_length)
        {
            throw std::out_of_range("PieceTable::Locate position is outside the buffer.");
        }

        if (position == m_length)
        {
            return { m_pieces.size(), 0 };
        }

        std::size_t cursor = 0;
        for (std::size_t index = 0; index < m_pieces.size(); ++index)
        {
            auto const& piece = m_pieces[index];
            if (position < cursor + piece.length)
            {
                return { index, position - cursor };
            }

            cursor += piece.length;
        }

        return { m_pieces.size(), 0 };
    }

    std::wstring_view PieceTable::BufferFor(Piece const& piece) const noexcept
    {
        return piece.buffer == BufferKind::Original ? std::wstring_view{ m_original } : std::wstring_view{ m_addBuffer };
    }

    void PieceTable::Normalize()
    {
        std::vector<Piece> normalized;
        normalized.reserve(m_pieces.size());

        for (auto const& piece : m_pieces)
        {
            if (piece.length == 0)
            {
                continue;
            }

            if (!normalized.empty())
            {
                auto& previous = normalized.back();
                if (previous.buffer == piece.buffer && previous.start + previous.length == piece.start)
                {
                    previous.length += piece.length;
                    continue;
                }
            }

            normalized.push_back(piece);
        }

        m_pieces = std::move(normalized);
    }
}
