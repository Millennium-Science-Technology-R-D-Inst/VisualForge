#include "pch.h"

#include "EditorCore/Layout/GlyphRunCache.h"

namespace VisualForge::EditorCore::Layout
{
    void GlyphRunCache::Put(std::size_t lineIndex, CachedGlyphRun run)
    {
        m_runs[lineIndex] = std::move(run);
    }

    CachedGlyphRun const* GlyphRunCache::Find(std::size_t lineIndex) const noexcept
    {
        auto found = m_runs.find(lineIndex);
        return found == m_runs.end() ? nullptr : &found->second;
    }

    void GlyphRunCache::Invalidate(std::size_t startLine, std::size_t endLine)
    {
        for (auto it = m_runs.begin(); it != m_runs.end();)
        {
            if (it->first >= startLine && it->first <= endLine)
            {
                it = m_runs.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void GlyphRunCache::Clear()
    {
        m_runs.clear();
    }
}
