#include "pch.h"

#include "EditorCore/LSP/LspProtocol.h"

namespace VisualForge::EditorCore::LSP
{
    LspProtocolMessage LspProtocolParser::Parse(JsonRpcMessage const& message)
    {
        auto summary = Protocol::JsonMessageSummaryParser::Parse(message.payload);
        auto diagnostics = summary.Method == L"textDocument/publishDiagnostics"
            ? ParseDiagnostics(message.payload)
            : std::vector<LspDiagnostic>{};
        auto completions = summary.Method.empty()
            ? ParseCompletions(message.payload)
            : std::vector<LspCompletionItem>{};
        auto hover = summary.Method.empty() ? ParseHover(message.payload) : L"";
        auto locations = summary.Method.empty() ? ParseLocations(message.payload) : std::vector<LspLocation>{};

        return { std::move(summary), std::move(diagnostics), std::move(completions), std::move(hover), std::move(locations), message.payload };
    }

    std::vector<LspDiagnostic> LspProtocolParser::ParseDiagnostics(std::string_view payload)
    {
        std::vector<LspDiagnostic> diagnostics;
        auto uri = Protocol::JsonMessageSummaryParser::ReadStringField(payload, "uri").value_or(L"");

        std::size_t searchOffset = 0;
        while (searchOffset < payload.size())
        {
            auto const messageField = payload.find("\"message\"", searchOffset);
            if (messageField == std::string_view::npos)
            {
                break;
            }

            auto const objectStart = payload.rfind('{', messageField);
            auto const object = payload.substr(objectStart == std::string_view::npos ? searchOffset : objectStart, messageField - (objectStart == std::string_view::npos ? searchOffset : objectStart));
            auto const message = Protocol::JsonMessageSummaryParser::ReadStringField(payload.substr(messageField), "message").value_or(L"");
            auto const severityValue = Protocol::JsonMessageSummaryParser::ReadIntField(object, "severity").value_or(0);
            auto const line = Protocol::JsonMessageSummaryParser::ReadIntField(object, "line").value_or(0);
            auto const character = Protocol::JsonMessageSummaryParser::ReadIntField(object, "character").value_or(0);
            auto const endLine = Protocol::JsonMessageSummaryParser::ReadIntField(payload.substr(messageField), "line").value_or(line);
            auto const endCharacter = Protocol::JsonMessageSummaryParser::ReadIntField(payload.substr(messageField), "character").value_or(character);
            auto code = Protocol::JsonMessageSummaryParser::ReadStringField(object, "code").value_or(L"");
            if (code.empty())
            {
                if (auto const numericCode = Protocol::JsonMessageSummaryParser::ReadIntField(object, "code"))
                {
                    code = std::to_wstring(*numericCode);
                }
            }
            diagnostics.push_back({
                uri,
                static_cast<std::size_t>(line),
                static_cast<std::size_t>(character),
                static_cast<std::size_t>(endLine),
                static_cast<std::size_t>(endCharacter),
                std::move(code),
                message,
                static_cast<LspDiagnosticSeverity>(severityValue)
            });

            searchOffset = messageField + 9;
        }

        return diagnostics;
    }

    std::vector<LspCompletionItem> LspProtocolParser::ParseCompletions(std::string_view payload)
    {
        std::vector<LspCompletionItem> completions;
        auto const itemsField = payload.find("\"items\"");
        if (itemsField == std::string_view::npos)
        {
            return completions;
        }

        std::size_t searchOffset = itemsField;
        while (searchOffset < payload.size())
        {
            auto const labelField = payload.find("\"label\"", searchOffset);
            if (labelField == std::string_view::npos)
            {
                break;
            }

            auto const itemEnd = payload.find('}', labelField);
            if (itemEnd == std::string_view::npos)
            {
                break;
            }

            auto const item = payload.substr(labelField, itemEnd - labelField + 1);
            auto label = Protocol::JsonMessageSummaryParser::ReadStringField(item, "label").value_or(L"");
            if (!label.empty())
            {
                auto insertText = Protocol::JsonMessageSummaryParser::ReadStringField(item, "insertText").value_or(label);
                completions.push_back({
                    std::move(label),
                    Protocol::JsonMessageSummaryParser::ReadStringField(item, "detail").value_or(L""),
                    std::move(insertText)
                });
            }

            searchOffset = itemEnd + 1;
        }

        return completions;
    }

    std::wstring LspProtocolParser::ParseHover(std::string_view payload)
    {
        if (payload.find("\"contents\"") == std::string_view::npos)
        {
            return {};
        }

        return Protocol::JsonMessageSummaryParser::ReadStringField(payload, "value").value_or(
            Protocol::JsonMessageSummaryParser::ReadStringField(payload, "contents").value_or(L""));
    }

    std::vector<LspLocation> LspProtocolParser::ParseLocations(std::string_view payload)
    {
        std::vector<LspLocation> locations;
        auto const uri = Protocol::JsonMessageSummaryParser::ReadStringField(payload, "uri").value_or(L"");
        auto const range = payload.find("\"range\"");
        if (uri.empty() || range == std::string_view::npos)
        {
            return locations;
        }

        auto const rangePayload = payload.substr(range);
        auto const start = rangePayload.find("\"start\"");
        auto const end = rangePayload.find("\"end\"");
        auto const startPayload = start == std::string_view::npos ? rangePayload : rangePayload.substr(start);
        auto const endPayload = end == std::string_view::npos ? rangePayload : rangePayload.substr(end);
        auto const line = Protocol::JsonMessageSummaryParser::ReadIntField(startPayload, "line").value_or(0);
        auto const column = Protocol::JsonMessageSummaryParser::ReadIntField(startPayload, "character").value_or(0);
        auto const endLine = Protocol::JsonMessageSummaryParser::ReadIntField(endPayload, "line").value_or(line);
        auto const endColumn = Protocol::JsonMessageSummaryParser::ReadIntField(endPayload, "character").value_or(column);
        locations.push_back({
            uri,
            static_cast<std::size_t>(line),
            static_cast<std::size_t>(column),
            static_cast<std::size_t>(endLine),
            static_cast<std::size_t>(endColumn)
        });
        return locations;
    }
}
