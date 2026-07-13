#include "pch.h"

#include "EditorCore/DAP/DapProtocol.h"

namespace VisualForge::EditorCore::DAP
{
    DapProtocolMessage DapProtocolParser::Parse(LSP::JsonRpcMessage const& message)
    {
        auto summary = Protocol::JsonMessageSummaryParser::Parse(message.payload);
        auto const type = Protocol::JsonMessageSummaryParser::ReadStringField(message.payload, "type").value_or(L"");

        auto kind = DapMessageKind::Unknown;
        if (type == L"response")
        {
            kind = DapMessageKind::Response;
        }
        else if (type == L"event")
        {
            kind = DapMessageKind::Event;
        }
        else if (type == L"request")
        {
            kind = DapMessageKind::Request;
        }

        return {
            kind,
            std::move(summary),
            ParseStackFrames(message.payload),
            ParseScopes(message.payload),
            ParseVariables(message.payload),
            message.payload
        };
    }

    std::vector<DapStackFrame> DapProtocolParser::ParseStackFrames(std::string_view payload)
    {
        std::vector<DapStackFrame> frames;
        auto const field = payload.find("\"stackFrames\"");
        if (field == std::string_view::npos)
        {
            return frames;
        }

        std::size_t offset = field;
        while (offset < payload.size())
        {
            auto const nameField = payload.find("\"name\"", offset);
            if (nameField == std::string_view::npos)
            {
                break;
            }
            auto const objectStart = payload.rfind('{', nameField);
            auto const objectEnd = payload.find('}', nameField);
            if (objectStart == std::string_view::npos || objectEnd == std::string_view::npos)
            {
                break;
            }
            auto const object = payload.substr(objectStart, objectEnd - objectStart + 1);
            frames.push_back({
                Protocol::JsonMessageSummaryParser::ReadIntField(object, "id").value_or(0),
                Protocol::JsonMessageSummaryParser::ReadStringField(object, "name").value_or(L""),
                Protocol::JsonMessageSummaryParser::ReadStringField(object, "path").value_or(L""),
                Protocol::JsonMessageSummaryParser::ReadIntField(object, "line").value_or(0),
                Protocol::JsonMessageSummaryParser::ReadIntField(object, "column").value_or(0)
            });
            offset = objectEnd + 1;
        }
        return frames;
    }

    std::vector<DapScope> DapProtocolParser::ParseScopes(std::string_view payload)
    {
        std::vector<DapScope> scopes;
        auto const field = payload.find("\"scopes\"");
        if (field == std::string_view::npos)
        {
            return scopes;
        }

        std::size_t offset = field;
        while (offset < payload.size())
        {
            auto const nameField = payload.find("\"name\"", offset);
            if (nameField == std::string_view::npos)
            {
                break;
            }
            auto const objectStart = payload.rfind('{', nameField);
            auto const objectEnd = payload.find('}', nameField);
            if (objectStart == std::string_view::npos || objectEnd == std::string_view::npos)
            {
                break;
            }
            auto const object = payload.substr(objectStart, objectEnd - objectStart + 1);
            scopes.push_back({
                Protocol::JsonMessageSummaryParser::ReadStringField(object, "name").value_or(L""),
                Protocol::JsonMessageSummaryParser::ReadIntField(object, "variablesReference").value_or(0)
            });
            offset = objectEnd + 1;
        }
        return scopes;
    }

    std::vector<DapVariable> DapProtocolParser::ParseVariables(std::string_view payload)
    {
        std::vector<DapVariable> variables;
        auto const field = payload.find("\"variables\"");
        if (field == std::string_view::npos)
        {
            return variables;
        }

        std::size_t offset = field;
        while (offset < payload.size())
        {
            auto const nameField = payload.find("\"name\"", offset);
            if (nameField == std::string_view::npos)
            {
                break;
            }
            auto const objectStart = payload.rfind('{', nameField);
            auto const objectEnd = payload.find('}', nameField);
            if (objectStart == std::string_view::npos || objectEnd == std::string_view::npos)
            {
                break;
            }
            auto const object = payload.substr(objectStart, objectEnd - objectStart + 1);
            variables.push_back({
                Protocol::JsonMessageSummaryParser::ReadStringField(object, "name").value_or(L""),
                Protocol::JsonMessageSummaryParser::ReadStringField(object, "value").value_or(L""),
                Protocol::JsonMessageSummaryParser::ReadStringField(object, "type").value_or(L"")
            });
            offset = objectEnd + 1;
        }
        return variables;
    }
}
