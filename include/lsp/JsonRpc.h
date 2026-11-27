//------------------------------------------------------------------------------
// JsonRpc.h
// JSON-RPC 2.0 protocol types and message handling utilities
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#pragma once

#include "JsonTypes.h"
#include <charconv>
#include <cstdint>
#include <iostream>
#include <optional>
#include <rfl/json.hpp> // IWYU pragma: keep
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace lsp {

using ID_t = std::variant<int, std::string>;
using Params_t = std::optional<rfl::json::Raw>;

inline constexpr int32_t kJsonRpcParseError = -32700;
inline constexpr int32_t kJsonRpcInvalidRequest = -32600;

inline int32_t mapRequestErrorCode(rfl::ErrorKind kind) noexcept {
    switch (kind) {
        case rfl::ErrorKind::syntax_error:
            return kJsonRpcParseError;
        default:
            return kJsonRpcInvalidRequest;
    }
}

struct RpcRequest {
    /// includes notifications
    std::string jsonrpc;
    std::optional<ID_t> id;
    std::string method;
    Params_t params;
};

struct RpcNotification {
    std::string jsonrpc;
    std::string method;
    Params_t params;
};

struct RpcResponse {
    std::string jsonrpc;
    std::optional<ID_t> id;
    Params_t result;
};

struct RpcError {
    /// A number indicating the error type that occurred.
    int32_t code;
    /// A string providing a short description of the error.
    std::string message;
};
struct RpcErrorResponse {
    std::string jsonrpc;
    std::optional<ID_t> id;
    RpcError error;
};

template<typename T>
void sendMessage(const T& message) {
    auto message_str = rfl::json::write<rfl::UnderlyingEnums>(message);
    std::cout << "Content-Length: " << message_str.length() << "\r\n\r\n";
    std::cout << message_str;
    std::cout.flush();
}

inline void sendNotification(const std::string& method, rfl::json::Raw params) {
    sendMessage(RpcNotification{
        .jsonrpc = "2.0",
        .method = method,
        .params = std::move(params),
    });
    std::cerr << "---> " << method;
    // std::cerr << ": " << params.str() << std::endl;
    std::cerr << std::endl;
}

template<class... Ps, typename T>
    requires(!std::is_same_v<std::remove_cvref_t<T>, rfl::json::Raw>)
inline void sendNotification(const std::string& method, const T& params) {
    sendNotification(method, rfl::json::Raw{rfl::json::write<Ps...>(params)});
}

inline void sendRequest(const std::string& method, rfl::json::Raw params) {
    sendMessage(RpcRequest{
        .jsonrpc = "2.0",
        .id = 0,
        .method = method,
        .params = std::move(params),
    });
    std::cerr << "---> " << method;
    // std::cerr << ": " << params.str() << std::endl;
    std::cerr << std::endl;
}

template<class... Ps, typename T>
    requires(!std::is_same_v<std::remove_cvref_t<T>, rfl::json::Raw>)
inline void sendRequest(const std::string& method, const T& params) {
    sendRequest(method, rfl::json::Raw{rfl::json::write<Ps...>(params)});
}

template<typename T>
std::optional<T> readJson(std::string& line, std::string& content) {
    while (std::getline(std::cin, line)) {
        if (!line.starts_with("Content-Length: ")) {
            std::cerr << "<-/- " << "Invalid Line: " << line << std::endl;
            continue;
        }

        std::string_view lengthText = std::string_view(line).substr(16);
        if (lengthText.ends_with('\r'))
            lengthText.remove_suffix(1);
        size_t contentLength = 0;
        auto [lengthEnd, lengthError] = std::from_chars(lengthText.data(),
                                                        lengthText.data() + lengthText.size(),
                                                        contentLength);
        // Lengths past 1 GiB are treated as corrupt framing rather than allocated
        if (lengthError != std::errc() || lengthEnd != lengthText.data() + lengthText.size() ||
            contentLength > (size_t(1) << 30)) {
            std::cerr << "<-/- " << "Invalid Content-Length: " << line << std::endl;
            continue;
        }
        content.resize(contentLength);

        // Read maybe charset and empty line. Neovim and Vscode both don't
        // have this header row.
        while (std::getline(std::cin, line) && line.size() > 1) {
        }

        if (!std::cin.read(content.data(), static_cast<std::streamsize>(contentLength))) {
            std::cerr << "<-/- " << "Input closed in the middle of a message" << std::endl;
            return std::nullopt;
        }

        rfl::Result<T> request = rfl::json::read<T>(content);
        if (!request) {
            auto response = rfl::json::read<RpcResponse>(content);
            if (response) {
                continue;
            }
            std::cerr << "Error parsing JSON: " << content << std::endl;
            std::cerr << "Rfl Error: " << request.error().what() << std::endl;
            sendMessage(
                RpcErrorResponse{.jsonrpc = "2.0",
                                 .id = 0,
                                 .error = RpcError{
                                     .code = mapRequestErrorCode(request.error().kind()),
                                     .message = "Error parsing JSON: " + request.error().what() +
                                                " for content: " + content,
                                 }});
            continue;
        }

        return std::move(request.value());
    }
    return std::nullopt;
}

} // namespace lsp
