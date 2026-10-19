#include "slideio/viewer/core/AnnotationCategories.h"

#include <algorithm>
#include <cstddef>
#include <string>

namespace slideio::viewer::core
{
namespace
{

/// Lowercases one ASCII byte, with no reference to the global C locale.
///
/// std::tolower consults it, and under a single-byte locale such as CP1252 it
/// maps bytes at and above 0x80 -- which in UTF-8 are the lead and
/// continuation bytes of a letter, not a letter. Whether that happened
/// depended on whether anything in the process had called setlocale, which is
/// not a thing this function should be able to notice.
unsigned char toLowerAsciiByte(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c - 'A' + 'a') : c;
}

/// True for the ASCII whitespace characters, and never for a byte at or above
/// 0x80. std::isspace has the locale problem toLowerAsciiByte describes, and
/// trimming the lead byte off a UTF-8 letter would leave a stub that is not
/// valid UTF-8.
bool isAsciiSpace(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

/// The folded form of one Latin codepoint in U+0080..U+017F, or `cp` itself.
///
/// Three regular blocks, no table: the Latin-1 Supplement shifts by 0x20, and
/// Latin Extended-A pairs an uppercase letter with the codepoint above it --
/// on even boundaries in two stretches and odd boundaries in the one between
/// them. Four codepoints break their stretch: the multiplication sign and
/// eszett sit in the Latin-1 run without belonging to it, and U+0178 and
/// U+017F map outside Latin Extended-A entirely. The two gaps in the stretches
/// -- U+0138 and U+0149, which have no uppercase form -- fall through to the
/// identity return, which is the right answer for them.
char32_t foldLatinCodepoint(char32_t cp)
{
    // U+00D7 is the multiplication sign, not a letter, and U+00DF is eszett,
    // whose simple fold is itself.
    if (cp >= 0x00C0 && cp <= 0x00DE && cp != 0x00D7) {
        return cp + 0x20;
    }
    if (cp == 0x0178) {
        // Capital Y with diaeresis folds DOWN out of Latin Extended-A, back
        // into the Latin-1 Supplement. The only backwards mapping here.
        return 0x00FF;
    }
    if (cp == 0x017F) {
        // Long s, which Unicode folds to plain ASCII s. Reachable from older
        // scanned text pasted into a category name.
        return 0x0073;
    }
    if (cp >= 0x0100 && cp <= 0x0137) {
        return (cp % 2 == 0) ? cp + 1 : cp;
    }
    if (cp >= 0x0139 && cp <= 0x0148) {
        return (cp % 2 == 1) ? cp + 1 : cp;
    }
    if (cp >= 0x014A && cp <= 0x0177) {
        return (cp % 2 == 0) ? cp + 1 : cp;
    }
    if (cp >= 0x0179 && cp <= 0x017E) {
        return (cp % 2 == 1) ? cp + 1 : cp;
    }
    return cp;
}

/// Appends `cp` to `out` as UTF-8. Only the one- and two-byte forms occur:
/// foldLatinCodepoint never returns anything above U+017F, and the caller only
/// hands it codepoints it decoded from a two-byte sequence.
void appendUtf8(std::string& out, char32_t cp)
{
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
        return;
    }
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
}

std::string trimmed(const std::string& text)
{
    const auto notSpace = [](unsigned char c) { return !isAsciiSpace(c); };
    const auto first = std::find_if(text.begin(), text.end(), notSpace);
    const auto last = std::find_if(text.rbegin(), text.rend(), notSpace).base();
    return first < last ? std::string(first, last) : std::string{};
}

} // namespace

std::string caseFoldKey(const std::string& text)
{
    std::string folded;
    folded.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto lead = static_cast<unsigned char>(text[i]);

        // ASCII, including every byte of a pure-ASCII category name.
        if (lead < 0x80) {
            folded.push_back(static_cast<char>(toLowerAsciiByte(lead)));
            continue;
        }

        // The two-byte forms that carry U+0080..U+07FF. Latin-1 Supplement and
        // Latin Extended-A live in C3, C4 and C5; the decode is restricted to
        // those three leads so no other script is touched or re-encoded.
        const bool latinLead = lead >= 0xC3 && lead <= 0xC5;
        const bool hasContinuation =
            i + 1 < text.size() && (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80;
        if (!latinLead || !hasContinuation) {
            // Anything else -- a longer sequence, another script, or a byte
            // that is not valid UTF-8 at all -- is copied through untouched.
            // A category name is whatever the user typed, and a key that
            // mangled it would still compare equal to itself; passing it
            // through keeps that true without pretending to fold it.
            folded.push_back(text[i]);
            continue;
        }

        const auto trail = static_cast<unsigned char>(text[i + 1]);
        const char32_t cp = (static_cast<char32_t>(lead & 0x1F) << 6) | (trail & 0x3F);
        appendUtf8(folded, foldLatinCodepoint(cp));
        ++i;
    }

    return folded;
}

bool isUnclassifiedCategory(const std::string& text)
{
    return caseFoldKey(trimmed(text)) == "unclassified";
}

std::vector<std::string> normalizeCategoryList(const std::vector<std::string>& raw)
{
    std::vector<std::string> result;
    std::vector<std::string> seen;
    for (const std::string& entry : raw) {
        const std::string clean = trimmed(entry);
        if (clean.empty() || isUnclassifiedCategory(clean)) {
            continue;
        }
        const std::string key = caseFoldKey(clean);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
            continue;
        }
        seen.push_back(key);
        result.push_back(clean);
    }
    return result;
}

std::vector<RetagChange> retagClassification(const std::vector<Annotation>& annotations,
                                             const std::string& from, const std::string& to,
                                             std::chrono::system_clock::time_point modifiedAt)
{
    const std::string target = trimmed(to);
    const std::string fromKey = caseFoldKey(trimmed(from));
    if (fromKey.empty() || target.empty() || fromKey == caseFoldKey(target)
        || isUnclassifiedCategory(target) || isUnclassifiedCategory(from)) {
        // Nothing to do, and nothing to record. An empty target would silently
        // unclassify every match as one undo step, so it is refused here, where a
        // test can reach it. "Unclassified" is refused on both sides: as a target
        // it would write a string categoryTally folds into the synthetic row, and
        // as a source it would miss every empty one. Renaming a category to itself
        // must not leave a step on the undo stack that does nothing.
        return {};
    }

    std::vector<RetagChange> changes;
    for (const Annotation& source : annotations) {
        if (caseFoldKey(trimmed(source.properties().classification)) != fromKey) {
            continue;
        }
        AnnotationProperties properties = source.properties();
        properties.classification = target;
        AnnotationMetadata metadata = source.metadata();
        metadata.modifiedAt = modifiedAt;
        // The four-argument constructor, never setProperties(): that one reads
        // the system clock and restamps modifiedAt, which would make this
        // function impure and break the exactness `after` promises.
        changes.push_back(RetagChange{source.id(), source,
                                      Annotation{source.id(), source.geometry(),
                                                 std::move(properties), metadata}});
    }
    return changes;
}

} // namespace slideio::viewer::core
