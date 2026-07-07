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

        return { kind, std::move(summary), message.payload };
    }
}
