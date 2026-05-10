#pragma once

#include <string>
#include <vector>

namespace slideio::viewer::core
{

/// Layer-neutral, pre-formatted view of a slide/scene metadata tree node.
/// Populated by infrastructure (SlideIOAdapter) from slideio::Metadata; the
/// UI walks this struct without ever including the SlideIO library headers.
struct MetadataNode
{
    enum class Type
    {
        Null,
        Bool,
        Int,
        Double,
        String,
        Array,
        Object
    };

    /// Key for object children; "[i]" for array items; empty at the root.
    std::string name;

    /// Defaults to Null so a SlideInfo with no metadata renders as
    /// "(no metadata)" in the UI without any explicit population step.
    Type type = Type::Null;

    /// Pre-formatted textual representation for scalar nodes (Bool/Int/
    /// Double/String). Unused for Null/Array/Object — the UI synthesizes
    /// summaries like "{N keys}" / "[N items]" for those.
    std::string value;

    std::vector<MetadataNode> children;
};

} // namespace slideio::viewer::core
