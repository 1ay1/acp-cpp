// SPDX-License-Identifier: Apache-2.0
#include <acp/content.hpp>

#include <cassert>
#include <iostream>

int main() {
    using namespace acp;

    // Text
    {
        ContentBlock t = TextContent{"hello", Nothing, Json::object()};
        Json j = to_json(t);
        assert(j["type"] == "text");
        assert(j["text"] == "hello");
        auto rt = from_json<ContentBlock>(j);
        assert(std::holds_alternative<TextContent>(rt));
        assert(std::get<TextContent>(rt).text == "hello");
    }

    // Image (variant arm with required mimeType)
    {
        ContentBlock i = ImageContent{"AAA=", "image/png", Nothing, Nothing, Json::object()};
        Json j = to_json(i);
        assert(j["type"] == "image");
        assert(j["mimeType"] == "image/png");
        assert(j["data"] == "AAA=");
        assert(!j.contains("uri"));
        auto rt = from_json<ContentBlock>(j);
        assert(std::holds_alternative<ImageContent>(rt));
    }

    // Resource (text variant of embedded resource)
    {
        EmbeddedResource er = TextResource{"file:///x.py", "print(1)", Just<std::string>("text/x-python")};
        ContentBlock r = ResourceContent{er, Nothing, Json::object()};
        Json j = to_json(r);
        assert(j["type"] == "resource");
        assert(j["resource"]["uri"] == "file:///x.py");
        assert(j["resource"]["text"] == "print(1)");
        auto rt = from_json<ContentBlock>(j);
        const auto& rc = std::get<ResourceContent>(rt);
        const auto& tr = std::get<TextResource>(rc.resource);
        assert(tr.text == "print(1)");
        assert(tr.mimeType == "text/x-python");
    }

    // resource_link
    {
        ContentBlock l = ResourceLinkContent{
            "file:///a.pdf", "a.pdf",
            Just<std::string>("application/pdf"),
            Nothing, Nothing, Just<std::int64_t>(1024), Nothing, Json::object()};
        Json j = to_json(l);
        assert(j["type"] == "resource_link");
        assert(j["uri"] == "file:///a.pdf");
        assert(j["size"] == 1024);
        auto rt = from_json<ContentBlock>(j);
        const auto& rl = std::get<ResourceLinkContent>(rt);
        assert(rl.name == "a.pdf");
        assert(rl.size == 1024);
    }

    std::cout << "content round-trip OK\n";
    return 0;
}
