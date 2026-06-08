// SPDX-License-Identifier: Apache-2.0
//
// schema_conformance.cpp — asserts the wire form matches the ACP v1 spec at
// the byte level for the load-bearing details that a structural round-trip
// alone would NOT catch:
//
//   • exact enum string spellings (snake_case, not camelCase)
//   • the tag KEY for each tagged sum ("type", "sessionUpdate", …)
//   • _meta is omitted from the wire when empty, present when populated
//   • the protocol version constant is 1
//   • required fields are actually required (decode throws when absent)
//   • the session/delete capability serialises to the wire key "delete"
//
#include <acp/acp.hpp>

#include <cassert>
#include <iostream>
#include <string>

using namespace acp;

static void check(bool ok, const char* what) {
    if (!ok) { std::cerr << "FAIL: " << what << "\n"; std::abort(); }
}

int main() {
    // ---- protocol version is exactly 1 ------------------------------------
    check(kProtocolVersion == 1, "kProtocolVersion == 1");

    // ---- StopReason wire spellings ----------------------------------------
    check(to_json(StopReason::EndTurn)         == "end_turn",          "stop end_turn");
    check(to_json(StopReason::MaxTokens)       == "max_tokens",        "stop max_tokens");
    check(to_json(StopReason::MaxTurnRequests) == "max_turn_requests", "stop max_turn_requests");
    check(to_json(StopReason::Refusal)         == "refusal",           "stop refusal");
    check(to_json(StopReason::Cancelled)       == "cancelled",         "stop cancelled");

    // ---- ContentBlock tag key is "type" with snake_case labels ------------
    {
        ContentBlock cb = TextContent{"hi", Nothing, Json::object()};
        Json j = to_json(cb);
        check(j["type"] == "text", "content tag key/label");
    }

    // ---- _meta omitted when empty, present when populated -----------------
    {
        PromptResult r;                       // meta defaults to empty object
        Json j = to_json(r);
        check(!j.contains("_meta"), "_meta omitted when empty");

        r.meta = Json{{"vendor", "acp-cpp"}};
        Json j2 = to_json(r);
        check(j2.contains("_meta") && j2["_meta"]["vendor"] == "acp-cpp",
              "_meta present when populated");
    }

    // ---- required field is enforced on decode -----------------------------
    {
        bool threw = false;
        try { from_json<PromptParams>(Json::object()); }   // missing sessionId+prompt
        catch (const CodecError&) { threw = true; }
        check(threw, "required field enforced");
    }

    // ---- session/delete capability uses wire key "delete" -----------------
    {
        SessionCapabilities sc;
        sc.deleteCap = Just(Unit{});
        Json j = to_json(sc);
        check(j.contains("delete"), "session cap wire key 'delete'");
    }

    // ---- every method Params/Result round-trips (key-stable) --------------
    auto rt = [](auto value, const char* what) {
        using T = decltype(value);
        Json j = to_json(value);
        T back = from_json<T>(j);
        Json j2 = to_json(back);
        check(j == j2, what);
    };
    rt(InitializeParams{}, "InitializeParams rt");
    rt(InitializeResult{}, "InitializeResult rt");
    rt(NewSessionParams{"/tmp", {}, Nothing}, "NewSessionParams rt");
    rt(PromptParams{SessionId{std::string("s")}, {}}, "PromptParams rt");
    rt(PromptResult{StopReason::EndTurn}, "PromptResult rt");
    rt(ReadTextFileParams{SessionId{std::string("s")}, "/f", Nothing, Nothing},
       "ReadTextFileParams rt");
    rt(CreateTerminalParams{SessionId{std::string("s")}, "ls", {}, {}, Nothing, Nothing},
       "CreateTerminalParams rt");

    std::cout << "schema conformance OK\n";
    return 0;
}
