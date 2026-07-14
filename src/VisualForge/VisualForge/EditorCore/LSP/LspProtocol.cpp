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
        auto textEdits = summary.Method.empty() ? ParseWorkspaceEdits(message.payload) : std::vector<LspTextEdit>{};
        auto codeActions = summary.Method.empty() && message.payload.find("\"result\":[") != std::string::npos
            ? ParseCodeActions(message.payload) : std::vector<LspCodeAction>{};
        auto documentSymbols = summary.Method.empty() && message.payload.find("\"result\":[") != std::string::npos
            ? ParseDocumentSymbols(message.payload) : std::vector<LspDocumentSymbol>{};
        if (textEdits.empty() && message.payload.find("\"result\":[") != std::string::npos)
        {
            textEdits = ParseDocumentEdits(message.payload);
        }
        auto semanticTokens = summary.Method.empty() ? ParseSemanticTokens(message.payload) : std::vector<LspSemanticToken>{};

        return { std::move(summary), std::move(diagnostics), std::move(completions), std::move(hover), std::move(locations), std::move(textEdits), std::move(codeActions), std::move(documentSymbols), std::move(semanticTokens), message.payload };
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
        std::size_t searchOffset = 0;
        while (searchOffset < payload.size())
        {
            auto const uriField = payload.find("\"uri\"", searchOffset);
            if (uriField == std::string_view::npos)
            {
                break;
            }
            auto const nextUriField = payload.find("\"uri\"", uriField + 5);
            auto const range = payload.find("\"range\"", uriField);
            if (range == std::string_view::npos
                || (nextUriField != std::string_view::npos && nextUriField < range))
            {
                searchOffset = uriField + 5;
                continue;
            }

            auto const objectEnd = nextUriField == std::string_view::npos ? payload.size() : nextUriField;
            auto const object = payload.substr(uriField, objectEnd - uriField);
            auto const uri = Protocol::JsonMessageSummaryParser::ReadStringField(object, "uri").value_or(L"");
            auto const rangePayload = object.substr(range - uriField);
            auto const start = rangePayload.find("\"start\"");
            auto const end = rangePayload.find("\"end\"");
            auto const startPayload = start == std::string_view::npos ? rangePayload : rangePayload.substr(start);
            auto const endPayload = end == std::string_view::npos ? rangePayload : rangePayload.substr(end);
            auto const line = Protocol::JsonMessageSummaryParser::ReadIntField(startPayload, "line").value_or(0);
            auto const column = Protocol::JsonMessageSummaryParser::ReadIntField(startPayload, "character").value_or(0);
            auto const endLine = Protocol::JsonMessageSummaryParser::ReadIntField(endPayload, "line").value_or(line);
            auto const endColumn = Protocol::JsonMessageSummaryParser::ReadIntField(endPayload, "character").value_or(column);
            if (!uri.empty())
            {
                locations.push_back({ uri, static_cast<std::size_t>(line), static_cast<std::size_t>(column),
                    static_cast<std::size_t>(endLine), static_cast<std::size_t>(endColumn) });
            }
            searchOffset = uriField + 5;
        }
        return locations;
    }

    std::vector<LspTextEdit> LspProtocolParser::ParseWorkspaceEdits(std::string_view payload)
    {
        std::vector<LspTextEdit> edits;
        auto const changesField = payload.find("\"changes\"");
        if (changesField == std::string_view::npos)
        {
            return edits;
        }

        auto const changesEnd = payload.find("\"documentChanges\"", changesField + 9);
        auto const limit = changesEnd == std::string_view::npos ? payload.size() : changesEnd;
        std::size_t searchOffset = changesField;
        while (searchOffset < limit)
        {
            auto const rangeField = payload.find("\"range\"", searchOffset);
            if (rangeField == std::string_view::npos || rangeField >= limit)
            {
                break;
            }

            auto const arrayStart = payload.rfind('[', rangeField);
            auto const keyEnd = arrayStart == std::string_view::npos
                ? std::string_view::npos : payload.rfind('"', arrayStart == 0 ? 0 : arrayStart - 1);
            auto const keyStart = keyEnd == std::string_view::npos || keyEnd == 0
                ? std::string_view::npos : payload.rfind('"', keyEnd - 1);
            if (keyStart == std::string_view::npos || keyEnd <= keyStart)
            {
                searchOffset = rangeField + 7;
                continue;
            }

            auto const nextRange = payload.find("\"range\"", rangeField + 7);
            auto const editEnd = nextRange == std::string_view::npos || nextRange >= limit ? limit : nextRange;
            auto const edit = payload.substr(rangeField, editEnd - rangeField);
            auto const start = edit.find("\"start\"");
            auto const end = edit.find("\"end\"");
            if (start == std::string_view::npos || end == std::string_view::npos)
            {
                searchOffset = rangeField + 7;
                continue;
            }

            auto const startPayload = edit.substr(start);
            auto const endPayload = edit.substr(end);
            auto const line = Protocol::JsonMessageSummaryParser::ReadIntField(startPayload, "line").value_or(0);
            auto const column = Protocol::JsonMessageSummaryParser::ReadIntField(startPayload, "character").value_or(0);
            auto const endLine = Protocol::JsonMessageSummaryParser::ReadIntField(endPayload, "line").value_or(line);
            auto const endColumn = Protocol::JsonMessageSummaryParser::ReadIntField(endPayload, "character").value_or(column);

            auto const key = payload.substr(keyStart, keyEnd - keyStart + 1);
            auto keyPayload = std::string{ "\"uri\":" } + std::string{ key };
            auto const uri = Protocol::JsonMessageSummaryParser::ReadStringField(keyPayload, "uri").value_or(L"");
            auto const newText = Protocol::JsonMessageSummaryParser::ReadStringField(edit, "newText").value_or(L"");
            if (!uri.empty())
            {
                edits.push_back({ { uri, static_cast<std::size_t>(line), static_cast<std::size_t>(column),
                    static_cast<std::size_t>(endLine), static_cast<std::size_t>(endColumn) }, newText });
            }
            searchOffset = rangeField + 7;
        }

        return edits;
    }

    std::vector<LspTextEdit> LspProtocolParser::ParseDocumentEdits(std::string_view payload)
    {
        std::vector<LspTextEdit> edits;
        auto const resultField = payload.find("\"result\"");
        auto const arrayStart = resultField == std::string_view::npos ? std::string_view::npos : payload.find('[', resultField);
        auto const arrayEnd = arrayStart == std::string_view::npos ? std::string_view::npos : payload.find(']', arrayStart);
        if (arrayStart == std::string_view::npos || arrayEnd == std::string_view::npos)
        {
            return edits;
        }

        std::size_t searchOffset = arrayStart;
        while (searchOffset < arrayEnd)
        {
            auto const rangeField = payload.find("\"range\"", searchOffset);
            if (rangeField == std::string_view::npos || rangeField >= arrayEnd)
            {
                break;
            }
            auto const nextRange = payload.find("\"range\"", rangeField + 7);
            auto const editEnd = nextRange == std::string_view::npos || nextRange >= arrayEnd ? arrayEnd : nextRange;
            auto const edit = payload.substr(rangeField, editEnd - rangeField);
            auto const start = edit.find("\"start\"");
            auto const end = edit.find("\"end\"");
            if (start != std::string_view::npos && end != std::string_view::npos)
            {
                auto const startPayload = edit.substr(start);
                auto const endPayload = edit.substr(end);
                auto const line = Protocol::JsonMessageSummaryParser::ReadIntField(startPayload, "line").value_or(0);
                auto const column = Protocol::JsonMessageSummaryParser::ReadIntField(startPayload, "character").value_or(0);
                auto const endLine = Protocol::JsonMessageSummaryParser::ReadIntField(endPayload, "line").value_or(line);
                auto const endColumn = Protocol::JsonMessageSummaryParser::ReadIntField(endPayload, "character").value_or(column);
                edits.push_back({ { L"", static_cast<std::size_t>(line), static_cast<std::size_t>(column),
                    static_cast<std::size_t>(endLine), static_cast<std::size_t>(endColumn) },
                    Protocol::JsonMessageSummaryParser::ReadStringField(edit, "newText").value_or(L"") });
            }
            searchOffset = rangeField + 7;
        }
        return edits;
    }

    std::vector<LspCodeAction> LspProtocolParser::ParseCodeActions(std::string_view payload)
    {
        std::vector<LspCodeAction> actions;
        auto const resultField = payload.find("\"result\"");
        auto const arrayStart = resultField == std::string_view::npos ? std::string_view::npos : payload.find('[', resultField);
        auto const arrayEnd = arrayStart == std::string_view::npos ? std::string_view::npos : payload.rfind(']');
        if (arrayStart == std::string_view::npos || arrayEnd == std::string_view::npos || arrayEnd <= arrayStart)
        {
            return actions;
        }

        std::size_t searchOffset = arrayStart + 1;
        while (searchOffset < arrayEnd)
        {
            auto const titleField = payload.find("\"title\"", searchOffset);
            if (titleField == std::string_view::npos || titleField >= arrayEnd)
            {
                break;
            }
            auto const nextTitle = payload.find("\"title\"", titleField + 7);
            auto const actionEnd = nextTitle == std::string_view::npos || nextTitle >= arrayEnd ? arrayEnd : nextTitle;
            auto const actionPayload = payload.substr(titleField, actionEnd - titleField);
            auto title = Protocol::JsonMessageSummaryParser::ReadStringField(actionPayload, "title").value_or(L"");
            auto edits = ParseWorkspaceEdits(actionPayload);
            if (edits.empty() && actionPayload.find("\"range\"") != std::string_view::npos)
            {
                edits = ParseDocumentEdits(std::string{ "{\"result\":" } + std::string{ actionPayload } + "}");
            }
            if (!title.empty() && !edits.empty())
            {
                actions.push_back({ std::move(title), std::move(edits) });
            }
            searchOffset = actionEnd;
        }
        return actions;
    }

    std::vector<LspDocumentSymbol> LspProtocolParser::ParseDocumentSymbols(std::string_view payload)
    {
        std::vector<LspDocumentSymbol> symbols;
        auto const resultField = payload.find("\"result\"");
        auto const arrayStart = resultField == std::string_view::npos ? std::string_view::npos : payload.find('[', resultField);
        auto const arrayEnd = arrayStart == std::string_view::npos ? std::string_view::npos : payload.rfind(']');
        if (arrayStart == std::string_view::npos || arrayEnd == std::string_view::npos || arrayEnd <= arrayStart)
        {
            return symbols;
        }

        std::size_t searchOffset = arrayStart + 1;
        while (searchOffset < arrayEnd)
        {
            auto const nameField = payload.find("\"name\"", searchOffset);
            if (nameField == std::string_view::npos || nameField >= arrayEnd)
            {
                break;
            }
            auto const nextName = payload.find("\"name\"", nameField + 6);
            auto const symbolEnd = nextName == std::string_view::npos || nextName >= arrayEnd ? arrayEnd : nextName;
            auto const symbolPayload = payload.substr(nameField, symbolEnd - nameField);
            auto const rangeField = symbolPayload.find("\"range\"");
            if (rangeField != std::string_view::npos)
            {
                auto const rangePayload = symbolPayload.substr(rangeField);
                auto const start = rangePayload.find("\"start\"");
                auto const end = rangePayload.find("\"end\"");
                if (start != std::string_view::npos && end != std::string_view::npos)
                {
                    auto const startPayload = rangePayload.substr(start);
                    auto const endPayload = rangePayload.substr(end);
                    auto const line = Protocol::JsonMessageSummaryParser::ReadIntField(startPayload, "line").value_or(0);
                    auto const column = Protocol::JsonMessageSummaryParser::ReadIntField(startPayload, "character").value_or(0);
                    auto const endLine = Protocol::JsonMessageSummaryParser::ReadIntField(endPayload, "line").value_or(line);
                    auto const endColumn = Protocol::JsonMessageSummaryParser::ReadIntField(endPayload, "character").value_or(column);
                    auto const name = Protocol::JsonMessageSummaryParser::ReadStringField(symbolPayload, "name").value_or(L"");
                    if (!name.empty())
                    {
                        symbols.push_back({
                            name,
                            Protocol::JsonMessageSummaryParser::ReadStringField(symbolPayload, "detail").value_or(L""),
                            static_cast<std::size_t>(Protocol::JsonMessageSummaryParser::ReadIntField(symbolPayload, "kind").value_or(0)),
                            { L"", static_cast<std::size_t>(line), static_cast<std::size_t>(column),
                                static_cast<std::size_t>(endLine), static_cast<std::size_t>(endColumn) }
                        });
                    }
                }
            }
            searchOffset = symbolEnd;
        }
        return symbols;
    }

    std::vector<LspSemanticToken> LspProtocolParser::ParseSemanticTokens(std::string_view payload)
    {
        std::vector<LspSemanticToken> tokens;
        auto const dataField = payload.find("\"data\"");
        if (dataField == std::string_view::npos)
        {
            return tokens;
        }
        auto const arrayStart = payload.find('[', dataField);
        auto const arrayEnd = arrayStart == std::string_view::npos ? std::string_view::npos : payload.find(']', arrayStart);
        if (arrayStart == std::string_view::npos || arrayEnd == std::string_view::npos)
        {
            return tokens;
        }

        std::vector<std::size_t> values;
        std::size_t cursor = arrayStart + 1;
        while (cursor < arrayEnd)
        {
            while (cursor < arrayEnd && (payload[cursor] == ' ' || payload[cursor] == '\t' || payload[cursor] == ',' || payload[cursor] == '\r' || payload[cursor] == '\n'))
            {
                ++cursor;
            }
            if (cursor >= arrayEnd)
            {
                break;
            }
            auto const begin = cursor;
            while (cursor < arrayEnd && payload[cursor] >= '0' && payload[cursor] <= '9')
            {
                ++cursor;
            }
            if (begin == cursor)
            {
                ++cursor;
                continue;
            }
            try
            {
                values.push_back(static_cast<std::size_t>(std::stoull(std::string{ payload.substr(begin, cursor - begin) })));
            }
            catch (...)
            {
                values.push_back(0);
            }
        }

        std::size_t line{};
        std::size_t column{};
        for (std::size_t index = 0; index + 4 < values.size(); index += 5)
        {
            line += values[index];
            column = values[index] == 0 ? column + values[index + 1] : values[index + 1];
            tokens.push_back({ line, column, values[index + 2], values[index + 3] });
        }
        return tokens;
    }
}
