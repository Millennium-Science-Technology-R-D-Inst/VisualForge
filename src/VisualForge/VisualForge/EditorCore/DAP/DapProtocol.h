#pragma once

#include "EditorCore/LSP/JsonRpc.h"
#include "EditorCore/Protocol/JsonMessageSummary.h"

#include <string>

namespace VisualForge::EditorCore::DAP
{
    enum class DapMessageKind
    {
        Unknown,
        Response,
        Event,
        Request
    };

    struct DapProtocolMessage
    {
        DapMessageKind Kind{ DapMessageKind::Unknown };
        Protocol::JsonMessageSummary Summary;
        std::string Payload;
    };

    class DapProtocolParser final
    {
    public:
        [[nodiscard]] static DapProtocolMessage Parse(LSP::JsonRpcMessage const& message);
    };
}
