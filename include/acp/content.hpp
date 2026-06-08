// SPDX-License-Identifier: Apache-2.0
//
// acp/content.hpp — ContentBlock and its constituents.
//
//   Denotationally:
//
//     EmbeddedResource  ≅  TextResource  +  BlobResource
//     ContentBlock      ≅  Text  +  Image  +  Audio  +  ResourceLink  +  Resource
//
//   The protocol reuses MCP's ContentBlock verbatim, so content forwarded
//   from MCP tools round-trips through this codec without translation.
//
#pragma once

#include <acp/codec.hpp>
#include <acp/ids.hpp>

#include <cstdint>
#include <string>

namespace acp {

//==============================================================================
//  Role — the sender/recipient indicator on annotations.
//==============================================================================
enum class Role { Assistant, User };
template <> struct CodecOf<Role> {
    static Codec<Role> get() {
        return enum_codec<Role>(
            EnumMapping<Role>{Role::Assistant, "assistant"},
            EnumMapping<Role>{Role::User,      "user"});
    }
};

//==============================================================================
//  Annotations — hints for clients about how to display content.
//==============================================================================
struct Annotations {
    Maybe<List<Role>>   audience;
    Maybe<std::string>  lastModified;     // ISO 8601
    Maybe<double>       priority;
    Json meta = Json::object();
};
template <> struct CodecOf<Annotations> {
    static Codec<Annotations> get() {
        return record<Annotations>(
            optional("audience",     &Annotations::audience),
            optional("lastModified", &Annotations::lastModified),
            optional("priority",     &Annotations::priority),
            meta("_meta",       &Annotations::meta));
    }
};

//==============================================================================
//  Embedded resource — text | blob
//==============================================================================
struct TextResource {
    std::string uri;
    std::string text;
    Maybe<std::string> mimeType;
};
struct BlobResource {
    std::string uri;
    std::string blob;        // base64
    Maybe<std::string> mimeType;
};
using EmbeddedResource = Sum<TextResource, BlobResource>;

//==============================================================================
//  ContentBlock variants
//==============================================================================
struct TextContent {
    std::string text;
    Maybe<Annotations> annotations;
    Json meta = Json::object();       // _meta — opaque
};
struct ImageContent {
    std::string data;        // base64
    std::string mimeType;
    Maybe<std::string> uri;
    Maybe<Annotations> annotations;
    Json meta = Json::object();
};
struct AudioContent {
    std::string data;        // base64
    std::string mimeType;
    Maybe<Annotations> annotations;
    Json meta = Json::object();
};
struct ResourceLinkContent {
    std::string uri;
    std::string name;
    Maybe<std::string> mimeType;
    Maybe<std::string> title;
    Maybe<std::string> description;
    Maybe<std::int64_t> size;
    Maybe<Annotations> annotations;
    Json meta = Json::object();
};
struct ResourceContent {
    EmbeddedResource resource;
    Maybe<Annotations> annotations;
    Json meta = Json::object();
};

using ContentBlock = Sum<
    TextContent,
    ImageContent,
    AudioContent,
    ResourceLinkContent,
    ResourceContent>;

//==============================================================================
//  Codecs
//==============================================================================
template <> struct CodecOf<TextResource> {
    static Codec<TextResource> get() {
        return record<TextResource>(
            required ("uri",      &TextResource::uri),
            required ("text",     &TextResource::text),
            optional ("mimeType", &TextResource::mimeType));
    }
};
template <> struct CodecOf<BlobResource> {
    static Codec<BlobResource> get() {
        return record<BlobResource>(
            required ("uri",      &BlobResource::uri),
            required ("blob",     &BlobResource::blob),
            optional ("mimeType", &BlobResource::mimeType));
    }
};
template <> struct CodecOf<EmbeddedResource> {
    static Codec<EmbeddedResource> get() {
        // EmbeddedResource lacks an explicit discriminator on the wire;
        // it disambiguates by the presence of "text" vs "blob". Encode the
        // un-tagged way: dispatch by alternative.
        return {
            [](const EmbeddedResource& r) -> Json {
                return match(r,
                    [](const TextResource& x){ return to_json(x); },
                    [](const BlobResource& x){ return to_json(x); });
            },
            [](const Json& j) -> EmbeddedResource {
                if (!j.is_object()) throw CodecError("EmbeddedResource: expected object");
                if (j.contains("text")) return EmbeddedResource{from_json<TextResource>(j)};
                if (j.contains("blob")) return EmbeddedResource{from_json<BlobResource>(j)};
                throw CodecError("EmbeddedResource: must contain 'text' or 'blob'");
            }};
    }
};

template <> struct CodecOf<TextContent> {
    static Codec<TextContent> get() {
        return record<TextContent>(
            required ("text",        &TextContent::text),
            optional ("annotations", &TextContent::annotations),
            meta("_meta",       &TextContent::meta));
    }
};
template <> struct CodecOf<ImageContent> {
    static Codec<ImageContent> get() {
        return record<ImageContent>(
            required ("data",        &ImageContent::data),
            required ("mimeType",    &ImageContent::mimeType),
            optional ("uri",         &ImageContent::uri),
            optional ("annotations", &ImageContent::annotations),
            meta("_meta",       &ImageContent::meta));
    }
};
template <> struct CodecOf<AudioContent> {
    static Codec<AudioContent> get() {
        return record<AudioContent>(
            required ("data",        &AudioContent::data),
            required ("mimeType",    &AudioContent::mimeType),
            optional ("annotations", &AudioContent::annotations),
            meta("_meta",       &AudioContent::meta));
    }
};
template <> struct CodecOf<ResourceLinkContent> {
    static Codec<ResourceLinkContent> get() {
        return record<ResourceLinkContent>(
            required ("uri",         &ResourceLinkContent::uri),
            required ("name",        &ResourceLinkContent::name),
            optional ("mimeType",    &ResourceLinkContent::mimeType),
            optional ("title",       &ResourceLinkContent::title),
            optional ("description", &ResourceLinkContent::description),
            optional ("size",        &ResourceLinkContent::size),
            optional ("annotations", &ResourceLinkContent::annotations),
            meta("_meta",       &ResourceLinkContent::meta));
    }
};
template <> struct CodecOf<ResourceContent> {
    static Codec<ResourceContent> get() {
        return record<ResourceContent>(
            required ("resource",    &ResourceContent::resource),
            optional ("annotations", &ResourceContent::annotations),
            meta("_meta",       &ResourceContent::meta));
    }
};

template <> struct CodecOf<ContentBlock> {
    static Codec<ContentBlock> get() {
        return sum_tagged<ContentBlock>("type",
            arm<ContentBlock, TextContent>        ("text"),
            arm<ContentBlock, ImageContent>       ("image"),
            arm<ContentBlock, AudioContent>       ("audio"),
            arm<ContentBlock, ResourceLinkContent>("resource_link"),
            arm<ContentBlock, ResourceContent>    ("resource"));
    }
};

} // namespace acp
