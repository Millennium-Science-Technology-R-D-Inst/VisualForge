#include "pch.h"

#include "EditorCore/EditorView/EditorControl.h"

namespace VisualForge::EditorCore::EditorView
{
    void EditorControl::Document(Document::TextDocument* document) noexcept
    {
        m_document = document;
    }

    Document::TextDocument* EditorControl::Document() const noexcept
    {
        return m_document;
    }

    bool EditorControl::InsertText(std::wstring_view text)
    {
        return m_document ? m_input.InsertText(*m_document, text) : false;
    }

    bool EditorControl::Backspace()
    {
        return m_document ? m_input.Backspace(*m_document) : false;
    }

    bool EditorControl::DeleteForward()
    {
        return m_document ? m_input.DeleteForward(*m_document) : false;
    }

    bool EditorControl::MoveLeft()
    {
        return m_document ? m_input.MoveLeft(*m_document) : false;
    }

    bool EditorControl::MoveRight()
    {
        return m_document ? m_input.MoveRight(*m_document) : false;
    }

    void EditorControl::RenderVisibleContent()
    {
        if (!m_document)
        {
            return;
        }

        m_document->Reflow();
        m_renderer.Render(m_document->Buffer(), m_document->Layout(), m_document->Viewport());
    }
}
