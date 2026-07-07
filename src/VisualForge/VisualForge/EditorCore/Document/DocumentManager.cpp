#include "pch.h"

#include "EditorCore/Document/DocumentManager.h"

#include <fstream>
#include <stdexcept>
#include <vector>

namespace VisualForge::EditorCore::Document
{
    namespace
    {
        void AppendWide(std::wstring& output, std::uint32_t codePoint)
        {
            if (codePoint <= 0xFFFF)
            {
                output.push_back(static_cast<wchar_t>(codePoint));
            }
            else
            {
                codePoint -= 0x10000;
                output.push_back(static_cast<wchar_t>(0xD800 + (codePoint >> 10)));
                output.push_back(static_cast<wchar_t>(0xDC00 + (codePoint & 0x3FF)));
            }
        }

        std::wstring DecodeUtf8(std::vector<char> const& bytes)
        {
            std::wstring output;
            output.reserve(bytes.size());

            std::size_t index = bytes.size() >= 3
                && static_cast<unsigned char>(bytes[0]) == 0xEF
                && static_cast<unsigned char>(bytes[1]) == 0xBB
                && static_cast<unsigned char>(bytes[2]) == 0xBF
                ? 3
                : 0;

            while (index < bytes.size())
            {
                auto const first = static_cast<unsigned char>(bytes[index++]);
                std::uint32_t codePoint = first;
                auto extraBytes = 0;

                if ((first & 0x80) == 0)
                {
                    extraBytes = 0;
                }
                else if ((first & 0xE0) == 0xC0)
                {
                    codePoint = first & 0x1F;
                    extraBytes = 1;
                }
                else if ((first & 0xF0) == 0xE0)
                {
                    codePoint = first & 0x0F;
                    extraBytes = 2;
                }
                else if ((first & 0xF8) == 0xF0)
                {
                    codePoint = first & 0x07;
                    extraBytes = 3;
                }
                else
                {
                    AppendWide(output, L'?');
                    continue;
                }

                auto valid = true;
                for (auto count = 0; count < extraBytes; ++count)
                {
                    if (index >= bytes.size())
                    {
                        valid = false;
                        break;
                    }

                    auto const next = static_cast<unsigned char>(bytes[index++]);
                    if ((next & 0xC0) != 0x80)
                    {
                        valid = false;
                        break;
                    }

                    codePoint = (codePoint << 6) | (next & 0x3F);
                }

                AppendWide(output, valid ? codePoint : L'?');
            }

            return output;
        }

        std::wstring ReadUtf8File(std::filesystem::path const& path)
        {
            std::ifstream stream{ path, std::ios::binary };
            if (!stream)
            {
                throw std::runtime_error("DocumentManager::Open could not open the source file.");
            }

            std::vector<char> bytes{
                std::istreambuf_iterator<char>{ stream },
                std::istreambuf_iterator<char>{}
            };

            return DecodeUtf8(bytes);
        }
    }

    TextDocument& DocumentManager::Open(std::filesystem::path path)
    {
        auto key = path.wstring();
        auto [it, inserted] = m_documents.try_emplace(key, path);
        if (inserted && std::filesystem::exists(path))
        {
            it->second.LoadText(ReadUtf8File(path));
        }

        return it->second;
    }

    TextDocument& DocumentManager::NewUntitled(std::wstring initialText)
    {
        auto path = std::filesystem::path{ L"untitled:" + std::to_wstring(m_nextUntitledId++) };
        auto key = path.wstring();
        auto [it, inserted] = m_documents.try_emplace(key, path);
        if (inserted)
        {
            it->second.LoadText(std::move(initialText));
        }

        return it->second;
    }

    bool DocumentManager::Save(std::filesystem::path const& path)
    {
        if (auto document = Find(path))
        {
            document->Save();
            return true;
        }

        return false;
    }

    bool DocumentManager::Close(std::filesystem::path const& path)
    {
        return m_documents.erase(path.wstring()) > 0;
    }

    TextDocument* DocumentManager::Find(std::filesystem::path const& path)
    {
        auto found = m_documents.find(path.wstring());
        return found == m_documents.end() ? nullptr : &found->second;
    }

    std::size_t DocumentManager::Count() const noexcept
    {
        return m_documents.size();
    }
}
