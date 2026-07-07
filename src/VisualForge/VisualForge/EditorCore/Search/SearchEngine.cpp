#include "pch.h"

#include "EditorCore/Search/SearchEngine.h"

#include <algorithm>
#include <cwctype>

namespace VisualForge::EditorCore::Search
{
    std::vector<SearchMatch> SearchEngine::FindAll(
        TextModel::TextBuffer const& buffer,
        std::wstring_view query,
        SearchOptions options)
    {
        std::vector<SearchMatch> matches;
        if (query.empty() || buffer.Empty())
        {
            return matches;
        }

        auto snapshot = buffer.CreateSnapshot();
        auto const originalText = snapshot.Text();
        auto const searchableText = Normalize(originalText, options.MatchCase);
        auto const searchableQuery = Normalize(query, options.MatchCase);

        std::size_t position = 0;
        while (position < searchableText.size())
        {
            auto const found = searchableText.find(searchableQuery, position);
            if (found == std::wstring::npos)
            {
                break;
            }

            if (!options.WholeWord || IsWholeWordMatch(originalText, found, query.size()))
            {
                matches.push_back({ found, query.size() });
            }

            position = found + (std::max<std::size_t>)(1, searchableQuery.size());
        }

        return matches;
    }

    std::size_t SearchEngine::ReplaceAll(
        TextModel::TextBuffer& buffer,
        std::wstring_view query,
        std::wstring_view replacement,
        SearchOptions options)
    {
        auto matches = FindAll(buffer, query, options);
        for (auto it = matches.rbegin(); it != matches.rend(); ++it)
        {
            auto const deletedText = buffer.Delete(it->Start, it->Length);
            (void)deletedText;
            buffer.Insert(it->Start, replacement);
        }

        return matches.size();
    }

    std::wstring SearchEngine::Normalize(std::wstring_view text, bool matchCase)
    {
        std::wstring normalized{ text };
        if (!matchCase)
        {
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t value)
            {
                return static_cast<wchar_t>(std::towlower(value));
            });
        }

        return normalized;
    }

    bool SearchEngine::IsWholeWordMatch(std::wstring_view text, std::size_t start, std::size_t length)
    {
        auto const beforeIsWord = start > 0 && IsWordCharacter(text[start - 1]);
        auto const after = start + length;
        auto const afterIsWord = after < text.size() && IsWordCharacter(text[after]);
        return !beforeIsWord && !afterIsWord;
    }

    bool SearchEngine::IsWordCharacter(wchar_t value) noexcept
    {
        return std::iswalnum(value) != 0 || value == L'_';
    }
}
