// SPDX-License-Identifier: Apache-2.0
//
// acp/core.hpp — the algebraic kernel.
//
//   The ACP protocol is, mathematically, a closed family of inductive types
//   built from a few constructors:
//
//       1            — the unit type (no information)
//       A × B        — products (records with named fields)
//       A + B        — coproducts / tagged sums
//       List A       — finite sequences
//       Maybe A      — A + 1, i.e. optional values
//       Json         — the free, untyped term language we (de)serialise into
//
//   This file gives names to those constructors. Every spec type downstream is
//   a closed expression in this algebra; serialization is a fold over that
//   expression rather than a hand-written ritual.
//
#pragma once

#include <acp/json.hpp>
#include <acp/version.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace acp {

//==============================================================================
//  1 — the unit type.
//
//      Inhabitants: exactly one, namely Unit{}.  Equivalent to the empty
//      record. Encoded as the empty JSON object  {}.
//==============================================================================
struct Unit {
    friend constexpr bool operator==(Unit, Unit) noexcept { return true; }
};

//==============================================================================
//  Maybe A  ≅  A + 1
//
//      We expose std::optional under a name that says what it MEANS in the
//      algebra (optionality is a sum, not a sentinel).
//==============================================================================
template <class A> using Maybe = std::optional<A>;

template <class A>
constexpr Maybe<A> Just(A x) { return Maybe<A>{std::move(x)}; }

inline constexpr std::nullopt_t Nothing = std::nullopt;

//==============================================================================
//  List A  ≅  μX. 1 + A × X      (finite sequences)
//==============================================================================
template <class A> using List = std::vector<A>;

//==============================================================================
//  Sum<A₀, A₁, …, Aₙ>  ≅  A₀ + A₁ + … + Aₙ
//
//      The discriminator is the alternative's POSITION; serialisation layers
//      (sum_tagged) attach a string tag to map positions ↔ wire labels.
//==============================================================================
template <class... As> using Sum = std::variant<As...>;

// match : Sum<As...> × (As → R)... → R   (the eliminator)
//
//     A total case-analysis over a sum. The set of branches is exhaustive
//     because std::visit refuses to compile otherwise.
template <class S, class... Fs>
constexpr decltype(auto) match(S&& s, Fs&&... fs) {
    struct overload : std::remove_reference_t<Fs>... { using std::remove_reference_t<Fs>::operator()...; };
    return std::visit(overload{std::forward<Fs>(fs)...}, std::forward<S>(s));
}

//==============================================================================
//  Compile-time string literal as a type.
//
//      We use these as discriminator tags in tagged sums (the wire labels) and
//      as capability indices. C++20 lets us pass string literals as non-type
//      template parameters via a structural type wrapper:
//==============================================================================
template <std::size_t N>
struct StaticString {
    char data[N]{};
    constexpr StaticString(const char (&s)[N]) {
        for (std::size_t i = 0; i < N; ++i) data[i] = s[i];
    }
    constexpr std::string_view view() const noexcept { return {data, N - 1}; }
    constexpr std::size_t size()      const noexcept { return N - 1; }
    constexpr bool operator==(const StaticString&) const = default;
};

// Phantom carrier for a compile-time tag — used to index typed handlers and
// capability witnesses without ever instantiating a value.
template <StaticString S>
struct Tag {
    static constexpr std::string_view name = S.view();
};

//==============================================================================
//  Newtype<Phantom, A>  — opaque type alias with no runtime overhead.
//
//      SessionId, ToolCallId, etc. are all "strings" on the wire but should not
//      be confused with each other. Newtype gives us the nominal typing the
//      spec implies without paying for it. Equality and hashing inherit from A.
//==============================================================================
template <class Phantom, class A>
struct Newtype {
    A value{};
    constexpr Newtype() = default;
    constexpr explicit Newtype(A v) : value(std::move(v)) {}
    constexpr operator const A&() const noexcept { return value; }
    constexpr const A& get() const noexcept { return value; }
    friend constexpr bool operator==(const Newtype&, const Newtype&) = default;
    friend constexpr auto operator<=>(const Newtype&, const Newtype&) = default;
};

} // namespace acp

// std::hash for any Newtype<P, A> when A is hashable.
template <class P, class A>
struct std::hash<acp::Newtype<P, A>> {
    std::size_t operator()(const acp::Newtype<P, A>& n) const
        noexcept(noexcept(std::hash<A>{}(n.value))) {
        return std::hash<A>{}(n.value);
    }
};
