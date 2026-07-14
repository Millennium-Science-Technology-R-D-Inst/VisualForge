#include "pch.h"

#include "EditorCore/DAP/DapProtocol.h"

#include <winrt/Windows.Data.Json.h>

using namespace winrt;
using namespace winrt::Windows::Data::Json;

namespace VisualForge::EditorCore::DAP
{
    namespace
    {
        JsonObject Body(JsonObject const& root)
        {
            return root.GetNamedObject(L"body", nullptr);
        }

        std::wstring StringValue(JsonObject const& object, wchar_t const* name)
        {
            return std::wstring{ object.GetNamedString(name, L"") };
        }

        int NumberValue(JsonObject const& object, wchar_t const* name)
        {
            return static_cast<int>(object.GetNamedNumber(name, 0));
        }
    }

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
            Protocol::JsonMessageSummaryParser::ReadIntField(message.payload, "threadId").value_or(0),
            ParseStackFrames(message.payload),
            ParseScopes(message.payload),
            ParseVariables(message.payload),
            message.payload
        };
    }

    std::vector<DapStackFrame> DapProtocolParser::ParseStackFrames(std::string_view payload)
    {
        std::vector<DapStackFrame> frames;
        try
        {
            auto root = JsonObject::Parse(to_hstring(payload));
            auto body = Body(root);
            if (!body)
            {
                return frames;
            }

            auto values = body.GetNamedArray(L"stackFrames", nullptr);
            if (!values)
            {
                return frames;
            }

            for (auto const& value : values)
            {
                auto frame = value.GetObject();
                auto source = frame.GetNamedObject(L"source", nullptr);
                frames.push_back({
                    NumberValue(frame, L"id"),
                    StringValue(frame, L"name"),
                    source ? StringValue(source, L"path") : std::wstring{},
                    NumberValue(frame, L"line"),
                    NumberValue(frame, L"column")
                });
            }
        }
        catch (hresult_error const&)
        {
        }
        return frames;
    }

    std::vector<DapScope> DapProtocolParser::ParseScopes(std::string_view payload)
    {
        std::vector<DapScope> scopes;
        try
        {
            auto root = JsonObject::Parse(to_hstring(payload));
            auto body = Body(root);
            if (!body)
            {
                return scopes;
            }

            auto values = body.GetNamedArray(L"scopes", nullptr);
            if (!values)
            {
                return scopes;
            }

            for (auto const& value : values)
            {
                auto scope = value.GetObject();
                scopes.push_back({
                    StringValue(scope, L"name"),
                    NumberValue(scope, L"variablesReference")
                });
            }
        }
        catch (hresult_error const&)
        {
        }
        return scopes;
    }

    std::vector<DapVariable> DapProtocolParser::ParseVariables(std::string_view payload)
    {
        std::vector<DapVariable> variables;
        try
        {
            auto root = JsonObject::Parse(to_hstring(payload));
            auto body = Body(root);
            if (!body)
            {
                return variables;
            }

            auto values = body.GetNamedArray(L"variables", nullptr);
            if (!values)
            {
                return variables;
            }

            for (auto const& value : values)
            {
                auto variable = value.GetObject();
                variables.push_back({
                    StringValue(variable, L"name"),
                    StringValue(variable, L"value"),
                    StringValue(variable, L"type"),
                    NumberValue(variable, L"variablesReference")
                });
            }
        }
        catch (hresult_error const&)
        {
        }
        return variables;
    }
}
