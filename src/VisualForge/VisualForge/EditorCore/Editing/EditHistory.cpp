#include "pch.h"

#include "EditorCore/Editing/EditHistory.h"

namespace VisualForge::EditorCore::Editing
{
    void EditHistory::Execute(TextModel::TextBuffer& buffer, EditCommand command)
    {
        Apply(buffer, command);
        m_undo.push_back(std::move(command));
        m_redo.clear();
    }

    bool EditHistory::Undo(TextModel::TextBuffer& buffer)
    {
        if (m_undo.empty())
        {
            return false;
        }

        auto command = m_undo.back();
        m_undo.pop_back();
        Apply(buffer, Inverse(command));
        m_redo.push_back(std::move(command));
        return true;
    }

    bool EditHistory::Redo(TextModel::TextBuffer& buffer)
    {
        if (m_redo.empty())
        {
            return false;
        }

        auto command = m_redo.back();
        m_redo.pop_back();
        Apply(buffer, command);
        m_undo.push_back(std::move(command));
        return true;
    }

    void EditHistory::Clear()
    {
        m_undo.clear();
        m_redo.clear();
    }

    bool EditHistory::CanUndo() const noexcept
    {
        return !m_undo.empty();
    }

    bool EditHistory::CanRedo() const noexcept
    {
        return !m_redo.empty();
    }

    EditCommand EditHistory::Inverse(EditCommand const& command)
    {
        return {
            command.type == EditCommand::Type::Insert ? EditCommand::Type::Delete : EditCommand::Type::Insert,
            command.position,
            command.text
        };
    }

    void EditHistory::Apply(TextModel::TextBuffer& buffer, EditCommand const& command)
    {
        if (command.type == EditCommand::Type::Insert)
        {
            buffer.Insert(command.position, command.text);
        }
        else
        {
            auto const deletedText = buffer.Delete(command.position, command.text.size());
            (void)deletedText;
        }
    }
}
