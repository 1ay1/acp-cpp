// SPDX-License-Identifier: Apache-2.0
//
// Smoke test for the codec kernel. Exercises:
//   • primitives
//   • Maybe / List functor lifts
//   • record (required / optional / defaulted)
//   • sum_tagged with tagged union
//   • enum_codec
//   • Newtype
//
#include <acp/codec.hpp>

#include "agtest.hpp"
#include <iostream>
#include <string>

namespace test {

// ----- A small product (record) ---------------------------------------------
struct Point {
    std::int64_t x = 0;
    std::int64_t y = 0;
    acp::Maybe<std::string> label;
};

// ----- A small sum (tagged union over geometric shapes) ---------------------
struct Circle    { double radius = 0.0; };
struct Rectangle { double width = 0.0; double height = 0.0; };
using Shape = acp::Sum<Circle, Rectangle>;

// ----- A small enum --------------------------------------------------------
enum class Color { Red, Green, Blue };

// ----- A Newtype-tagged id -------------------------------------------------
struct UserIdTag;
using UserId = acp::Newtype<UserIdTag, std::string>;

} // namespace test

namespace acp {
template <> struct CodecOf<test::Point> {
    static Codec<test::Point> get() {
        return record<test::Point>(
            required ("x",     &test::Point::x),
            required ("y",     &test::Point::y),
            optional ("label", &test::Point::label));
    }
};
template <> struct CodecOf<test::Circle> {
    static Codec<test::Circle> get() {
        return record<test::Circle>(required("radius", &test::Circle::radius));
    }
};
template <> struct CodecOf<test::Rectangle> {
    static Codec<test::Rectangle> get() {
        return record<test::Rectangle>(
            required("width",  &test::Rectangle::width),
            required("height", &test::Rectangle::height));
    }
};
template <> struct CodecOf<test::Shape> {
    static Codec<test::Shape> get() {
        return sum_tagged<test::Shape>("type",
            arm<test::Shape, test::Circle>   ("circle"),
            arm<test::Shape, test::Rectangle>("rect"));
    }
};
template <> struct CodecOf<test::Color> {
    static Codec<test::Color> get() {
        using namespace test;
        return enum_codec<Color>(
            EnumMapping<Color>{Color::Red,   "red"},
            EnumMapping<Color>{Color::Green, "green"},
            EnumMapping<Color>{Color::Blue,  "blue"});
    }
};
} // namespace acp

TEST_CASE("kernel_smoke") {
    using namespace acp;

    // --- Primitive round-trip -----------------------------------------------
    {
        auto j = to_json<std::int64_t>(42);
        assert(from_json<std::int64_t>(j) == 42);
    }

    // --- Maybe lift ---------------------------------------------------------
    {
        Maybe<std::string> nothing = Nothing;
        Maybe<std::string> just    = Just<std::string>("hi");
        auto j1 = to_json(nothing);
        auto j2 = to_json(just);
        assert(j1.is_null());
        assert(j2 == "hi");
        assert(!from_json<Maybe<std::string>>(j1).has_value());
        assert(from_json<Maybe<std::string>>(j2) == "hi");
    }

    // --- List lift ----------------------------------------------------------
    {
        List<int> xs{1, 2, 3};
        auto j  = to_json(xs);
        auto rt = from_json<List<int>>(j);
        assert(rt == xs);
    }

    // --- Record (Point) -----------------------------------------------------
    {
        test::Point p{3, 4, Just<std::string>("origin-ish")};
        Json j = to_json(p);
        assert(j["x"] == 3);
        assert(j["y"] == 4);
        assert(j["label"] == "origin-ish");
        auto rt = from_json<test::Point>(j);
        assert(rt.x == 3 && rt.y == 4 && rt.label == "origin-ish");

        // optional omission
        test::Point q{1, 2, Nothing};
        Json jq = to_json(q);
        assert(!jq.contains("label"));
        auto qrt = from_json<test::Point>(jq);
        assert(!qrt.label.has_value());
    }

    // --- Sum (Shape) --------------------------------------------------------
    {
        test::Shape c = test::Circle{1.5};
        Json jc = to_json(c);
        assert(jc["type"] == "circle");
        assert(jc["radius"] == 1.5);
        auto crt = from_json<test::Shape>(jc);
        assert(std::holds_alternative<test::Circle>(crt));
        assert(std::get<test::Circle>(crt).radius == 1.5);

        test::Shape r = test::Rectangle{4.0, 5.0};
        Json jr = to_json(r);
        assert(jr["type"] == "rect");
        auto rrt = from_json<test::Shape>(jr);
        assert(std::holds_alternative<test::Rectangle>(rrt));
    }

    // --- Enum --------------------------------------------------------------
    {
        Json j = to_json(test::Color::Green);
        assert(j == "green");
        assert(from_json<test::Color>(j) == test::Color::Green);
    }

    // --- Newtype -----------------------------------------------------------
    {
        test::UserId u{std::string("alice")};
        Json j = to_json(u);
        assert(j == "alice");
        auto rt = from_json<test::UserId>(j);
        assert(rt.value == "alice");
    }

    std::cout << "kernel smoke test OK\n";
}
