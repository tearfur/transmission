// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cerrno> /* EILSEQ, EINVAL */
#include <cstdlib> // free()
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <fmt/format.h>

#include <small/vector.hpp>

#include <yyjson.h>

#define LIBTRANSMISSION_VARIANT_MODULE

#include "libtransmission/error.h"
#include "libtransmission/quark.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/utils.h"
#include "libtransmission/variant.h"

namespace
{
namespace parse_helpers
{
auto constexpr MaxDepth = size_t{ 64U };

[[nodiscard]] auto make_string_view(char const* const data, size_t const len) -> std::string_view
{
    return { data != nullptr ? data : "", len };
}

[[nodiscard]] bool to_variant(yyjson_val* const source, tr_variant* const target, size_t const depth)
{
    TR_ASSERT(source != nullptr);
    TR_ASSERT(target != nullptr);

    if (yyjson_is_null(source))
    {
        *target = nullptr;
        return true;
    }
    if (yyjson_is_bool(source))
    {
        *target = yyjson_get_bool(source);
        return true;
    }
    if (yyjson_is_uint(source))
    {
        *target = yyjson_get_uint(source);
        return true;
    }
    if (yyjson_is_sint(source))
    {
        *target = yyjson_get_sint(source);
        return true;
    }
    if (yyjson_is_real(source))
    {
        *target = yyjson_get_real(source);
        return true;
    }
    if (yyjson_is_str(source))
    {
        *target = make_string_view(yyjson_get_str(source), yyjson_get_len(source));
        return true;
    }
    if (depth >= MaxDepth)
    {
        return false;
    }

    if (yyjson_is_arr(source))
    {
        *target = tr_variant::Vector{};
        auto* const out = target->get_if<tr_variant::Vector>();
        TR_ASSERT(out != nullptr);
        out->reserve(yyjson_arr_size(source));

        auto index = size_t{};
        auto max = size_t{};
        auto* child = static_cast<yyjson_val*>(nullptr);
        yyjson_arr_foreach(source, index, max, child)
        {
            auto& node = out->emplace_back();
            if (!to_variant(child, &node, depth + 1U))
            {
                return false;
            }
        }

        return true;
    }
    if (yyjson_is_obj(source))
    {
        *target = tr_variant::Map{ yyjson_obj_size(source) };
        auto* const out = target->get_if<tr_variant::Map>();
        TR_ASSERT(out != nullptr);

        auto index = size_t{};
        auto max = size_t{};
        auto* key = static_cast<yyjson_val*>(nullptr);
        auto* val = static_cast<yyjson_val*>(nullptr);
        yyjson_obj_foreach(source, index, max, key, val)
        {
            auto& node = (*out)[tr_quark_new(make_string_view(yyjson_get_str(key), yyjson_get_len(key)))];
            if (!to_variant(val, &node, depth + 1U))
            {
                return false;
            }
        }

        return true;
    }

    return false;
}
} // namespace parse_helpers
} // namespace

std::optional<tr_variant> tr_variant_serde::parse_json(std::string_view input)
{
    auto* begin = std::data(input);
    if (begin == nullptr)
    {
        // Keep a non-null pointer for end_ arithmetic and error snippets.
        begin = "";
    }

    auto const size = std::size(input);
    if (size == 0U)
    {
        end_ = begin;
        error_.set(EINVAL, "No content");
        return {};
    }

    // Keep JSON parsing non-insitu for now. `tr_variant_serde::parse()` accepts
    // `std::string_view`, so this backend cannot safely assume a mutable padded
    // buffer, which yyjson requires for insitu parsing.
    auto err = yyjson_read_err{};
    auto* const doc = yyjson_read_opts(
        const_cast<char*>(begin),
        size,
        YYJSON_READ_STOP_WHEN_DONE | YYJSON_READ_ALLOW_BOM,
        nullptr,
        &err);
    auto const pos = doc != nullptr ? yyjson_doc_get_read_size(doc) : std::min(err.pos, size);
    end_ = begin + pos;

    if (doc != nullptr)
    {
        auto top = tr_variant{};
        auto const ok = parse_helpers::to_variant(yyjson_doc_get_root(doc), &top, 0U);
        yyjson_doc_free(doc);

        if (ok)
        {
            return std::optional<tr_variant>{ std::move(top) };
        }

        error_.set(E2BIG, "Max stack depth reached; unable to continue parsing");
        return {};
    }

    if (err.code == YYJSON_READ_ERROR_EMPTY_CONTENT)
    {
        error_.set(EINVAL, "No content");
        return {};
    }

    auto const text = pos < size ? std::string_view{ begin + pos, std::min(size_t{ 16U }, size - pos) } : std::string_view{};
    error_.set(
        EILSEQ,
        fmt::format(
            fmt::runtime(_("Couldn't parse JSON at position {position} '{text}': {error} ({error_code})")),
            fmt::arg("position", pos),
            fmt::arg("text", text),
            fmt::arg("error", err.msg != nullptr ? err.msg : "Unknown error"),
            fmt::arg("error_code", err.code)));
    return {};
}

// ---

namespace
{
namespace to_string_helpers
{
[[nodiscard]] auto sorted_entries(tr_variant::Map const& map)
{
    static auto constexpr N = 32U;
    auto entries = small::vector<std::pair<std::string_view, tr_variant const*>, N>{};
    entries.reserve(map.size());
    for (auto const& [key, child] : map)
    {
        entries.emplace_back(tr_quark_get_string_view(key), &child);
    }
    std::ranges::sort(entries);
    return entries;
}

[[nodiscard]] yyjson_mut_val* to_json_value(yyjson_mut_doc* const doc, tr_variant const& var)
{
    return var.visit(
        [doc](auto const& value) -> yyjson_mut_val*
        {
            using ValueType = std::remove_cvref_t<decltype(value)>;

            if constexpr (std::is_same_v<ValueType, std::monostate>)
            {
                return nullptr;
            }
            else if constexpr (std::is_same_v<ValueType, std::nullptr_t>)
            {
                return yyjson_mut_null(doc);
            }
            else if constexpr (std::is_same_v<ValueType, bool>)
            {
                return yyjson_mut_bool(doc, value);
            }
            else if constexpr (std::is_same_v<ValueType, int64_t>)
            {
                return yyjson_mut_sint(doc, value);
            }
            else if constexpr (std::is_same_v<ValueType, double>)
            {
                return yyjson_mut_real(doc, value);
            }
            else if constexpr (std::is_same_v<ValueType, std::string> || std::is_same_v<ValueType, std::string_view>)
            {
                auto const sv = std::string_view{ value };
                auto const* const data = std::data(sv);
                return yyjson_mut_strncpy(doc, data != nullptr ? data : "", std::size(sv));
            }
            else if constexpr (std::is_same_v<ValueType, tr_variant::Vector>)
            {
                auto* const out = yyjson_mut_arr(doc);
                if (out == nullptr)
                {
                    return nullptr;
                }

                for (auto const& child : value)
                {
                    auto* const child_json = to_json_value(doc, child);
                    if (child_json == nullptr || !yyjson_mut_arr_add_val(out, child_json))
                    {
                        return nullptr;
                    }
                }

                return out;
            }
            else if constexpr (std::is_same_v<ValueType, tr_variant::Map>)
            {
                auto* const out = yyjson_mut_obj(doc);
                if (out == nullptr)
                {
                    return nullptr;
                }

                for (auto const& [key, child] : sorted_entries(value))
                {
                    auto const* const key_data = std::data(key);
                    auto* const json_key = yyjson_mut_strncpy(doc, key_data != nullptr ? key_data : "", std::size(key));
                    auto* const json_val = to_json_value(doc, *child);
                    if (json_key == nullptr || json_val == nullptr || !yyjson_mut_obj_add(out, json_key, json_val))
                    {
                        return nullptr;
                    }
                }

                return out;
            }

            return nullptr;
        });
}

} // namespace to_string_helpers
} // namespace

std::string tr_variant_serde::to_json_string(tr_variant const& var) const
{
    using namespace to_string_helpers;

    auto* const doc = yyjson_mut_doc_new(nullptr);
    if (doc == nullptr)
    {
        return {};
    }

    auto* const root = to_json_value(doc, var);
    if (root == nullptr)
    {
        yyjson_mut_doc_free(doc);
        return {};
    }

    yyjson_mut_doc_set_root(doc, root);

    auto len = size_t{};
    auto const flags = compact_ ? YYJSON_WRITE_NOFLAG : YYJSON_WRITE_PRETTY;
    auto* const json = yyjson_mut_write_opts(doc, flags, nullptr, &len, nullptr);
    yyjson_mut_doc_free(doc);
    if (json == nullptr)
    {
        return {};
    }

    auto out = std::string{ json, len };
    std::free(json);
    return out;
}
