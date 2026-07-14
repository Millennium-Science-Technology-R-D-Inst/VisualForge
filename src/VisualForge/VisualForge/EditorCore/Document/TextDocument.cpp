#include "pch.h"

#include "EditorCore/Document/TextDocument.h"

#include <fstream>
#include <stdexcept>
#include <vector>

namespace VisualForge::EditorCore::Document
{
    namespace
    {
        void AppendUtf8(std::vector<char>& output, std::uint32_t codePoint)
        {
            if (codePoint <= 0x7F)
            {
                output.push_back(static_cast<char>(codePoint));
            }
            else if (codePoint <= 0x7FF)
            {
                output.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
                output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            }
            else if (codePoint <= 0xFFFF)
            {
                output.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
                output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            }
            else
            {
                output.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
                output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
                output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            }
        }

        std::vector<char> EncodeUtf8(std::wstring_view text)
        {
            std::vector<char> output;
            output.reserve(text.size());

            for (std::size_t index = 0; index < text.size(); ++index)
            {
                auto codePoint = static_cast<std::uint32_t>(text[index]);
                if (codePoint >= 0xD800 && codePoint <= 0xDBFF && index + 1 < text.size())
                {
                    auto const low = static_cast<std::uint32_t>(text[index + 1]);
                    if (low >= 0xDC00 && low <= 0xDFFF)
                    {
                        codePoint = 0x10000 + (((codePoint - 0xD800) << 10) | (low - 0xDC00));
                        ++index;
                    }
                }

                AppendUtf8(output, codePoint);
            }

            return output;
        }

        std::vector<char> EncodeUtf16(std::wstring_view text, bool bigEndian)
        {
            std::vector<char> output;
            output.reserve(text.size() * 2 + 2);
            output.push_back(static_cast<char>(bigEndian ? 0xFE : 0xFF));
            output.push_back(static_cast<char>(bigEndian ? 0xFF : 0xFE));
            for (auto const character : text)
            {
                auto const value = static_cast<std::uint16_t>(character);
                if (bigEndian)
                {
                    output.push_back(static_cast<char>((value >> 8) & 0xFF));
                    output.push_back(static_cast<char>(value & 0xFF));
                }
                else
                {
                    output.push_back(static_cast<char>(value & 0xFF));
                    output.push_back(static_cast<char>((value >> 8) & 0xFF));
                }
            }
            return output;
        }
    }

    TextDocument::TextDocument(std::filesystem::path path) :
        m_path(std::move(path))
    {
    }

    void TextDocument::LoadText(std::wstring text)
    {
        m_buffer.Load(std::move(text));
        m_history.Clear();
        m_layout.Reflow(m_buffer);
        m_viewport.UpdateLayout(m_layout);
        m_isDirty = false;
    }

    void TextDocument::SetEncoding(TextEncoding encoding) noexcept
    {
        m_encoding = encoding;
    }

    void TextDocument::Save()
    {
        if (m_path.empty())
        {
            throw std::logic_error("TextDocument::Save requires a document path.");
        }

        auto snapshot = m_buffer.CreateSnapshot();
        auto bytes = m_encoding == TextEncoding::Utf16Le
            ? EncodeUtf16(snapshot.Text(), false)
            : m_encoding == TextEncoding::Utf16Be
            ? EncodeUtf16(snapshot.Text(), true)
            : EncodeUtf8(snapshot.Text());
        if (m_encoding == TextEncoding::Utf8Bom)
        {
            bytes.insert(bytes.begin(), { static_cast<char>(0xEF), static_cast<char>(0xBB), static_cast<char>(0xBF) });
        }
        auto temporaryPath = m_path;
        temporaryPath += L".visualforge.tmp";
        std::ofstream stream{ temporaryPath, std::ios::binary | std::ios::trunc };
        if (!stream)
        {
            throw std::runtime_error("TextDocument::Save could not open the destination file.");
        }

        if (!bytes.empty())
        {
            stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
		stream.flush();
		if (!stream)
		{
			stream.close();
			std::error_code cleanupError;
			std::filesystem::remove(temporaryPath, cleanupError);
			throw std::runtime_error("TextDocument::Save could not write the destination file.");
		}
		stream.close();
		if (!MoveFileExW(
			temporaryPath.c_str(),
			m_path.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			std::error_code cleanupError;
			std::filesystem::remove(temporaryPath, cleanupError);
			throw std::runtime_error("TextDocument::Save could not replace the destination file.");
		}

        m_isDirty = false;
    }

    void TextDocument::SaveAs(std::filesystem::path path)
    {
		auto previousPath = m_path;
		m_path = std::move(path);
		try
		{
			Save();
		}
		catch (...)
		{
			m_path = std::move(previousPath);
			throw;
		}
    }

    void TextDocument::Insert(std::size_t position, std::wstring text)
    {
        m_history.Execute(m_buffer, { Editing::EditCommand::Type::Insert, position, std::move(text) });
        m_layout.Invalidate(0, m_layout.LineCount());
        m_isDirty = true;
    }

    void TextDocument::Delete(std::size_t position, std::size_t length)
    {
        auto deleted = m_buffer.GetText(position, length);
        m_history.Execute(m_buffer, { Editing::EditCommand::Type::Delete, position, std::move(deleted) });
        m_layout.Invalidate(0, m_layout.LineCount());
        m_isDirty = true;
    }

    bool TextDocument::Undo()
    {
        auto const changed = m_history.Undo(m_buffer);
        if (changed)
        {
            m_layout.Invalidate(0, m_layout.LineCount());
            m_isDirty = true;
        }

        return changed;
    }

    bool TextDocument::Redo()
    {
        auto const changed = m_history.Redo(m_buffer);
        if (changed)
        {
            m_layout.Invalidate(0, m_layout.LineCount());
            m_isDirty = true;
        }

        return changed;
    }

    void TextDocument::Reflow()
    {
        m_layout.Reflow(m_buffer);
        m_viewport.UpdateLayout(m_layout);
    }

    std::filesystem::path const& TextDocument::Path() const noexcept
    {
        return m_path;
    }

    TextModel::TextBuffer& TextDocument::Buffer() noexcept
    {
        return m_buffer;
    }

    TextModel::TextBuffer const& TextDocument::Buffer() const noexcept
    {
        return m_buffer;
    }

    LSP::LspClient& TextDocument::LanguageService() noexcept
    {
        return m_lsp;
    }

    Layout::LineLayoutEngine& TextDocument::Layout() noexcept
    {
        return m_layout;
    }

    Viewport::ViewportController& TextDocument::Viewport() noexcept
    {
        return m_viewport;
    }

    Cursor::CursorManager& TextDocument::Cursors() noexcept
    {
        return m_cursors;
    }

    bool TextDocument::IsDirty() const noexcept
    {
        return m_isDirty;
    }

    bool TextDocument::CanUndo() const noexcept
    {
        return m_history.CanUndo();
    }

    bool TextDocument::CanRedo() const noexcept
    {
        return m_history.CanRedo();
    }
}
