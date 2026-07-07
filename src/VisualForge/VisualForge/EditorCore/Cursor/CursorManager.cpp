#include "pch.h"

#include "EditorCore/Cursor/CursorManager.h"

#include <algorithm>

namespace VisualForge::EditorCore::Cursor
{
    void CursorManager::SetPrimaryCursor(std::size_t offset)
    {
        if (m_cursors.empty())
        {
            m_cursors.push_back({ offset });
        }
        else
        {
            m_cursors[0].offset = offset;
        }
    }

    void CursorManager::AddCursor(std::size_t offset)
    {
        auto exists = std::any_of(m_cursors.begin(), m_cursors.end(), [&](Cursor const& cursor)
        {
            return cursor.offset == offset;
        });

        if (!exists)
        {
            m_cursors.push_back({ offset });
        }
    }

    void CursorManager::ClearSecondaryCursors()
    {
        if (m_cursors.size() > 1)
        {
            m_cursors.erase(m_cursors.begin() + 1, m_cursors.end());
        }
    }

    std::vector<Cursor> CursorManager::GetCursors() const
    {
        return m_cursors;
    }

    Cursor CursorManager::PrimaryCursor() const noexcept
    {
        return m_cursors.empty() ? Cursor{} : m_cursors.front();
    }
}
