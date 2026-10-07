#pragma once

#include "slideio/viewer/core/Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace slideio::viewer::core
{

/// A 16-character lowercase hex identity derived from a slide's content
/// properties: the file size and the geometry of every scene it holds.
///
/// Stable across moves and renames, and identical whichever scene of a
/// multi-scene file is open -- every input is file-level. The open scene's own
/// dimensions, channel count, pyramid depth and resolution are deliberately
/// excluded, since each varies by scene and would key one file to several IDs.
/// `driverId` is excluded too: it is empty until the slide has been opened,
/// which is after the point the identity is needed, and a file opened by
/// auto-detection is the same slide as one opened with its driver named.
///
/// NOT cryptographic and NOT tamper-evident. The hash is FNV-1a, chosen because
/// 64 bits is all a 16-hex identity can carry and a vendored crypto
/// implementation would buy a property nothing here uses. This identifies a
/// slide for the user's own stored settings; nothing authenticates against it.
/// A consumer must never treat a matching ID as proof of provenance.
///
/// Total: an empty scene vector yields a stable ID rather than throwing.
/// Callers that cannot enumerate a slide must decline to look anything up
/// rather than trust the ID that results -- every unreadable file produces it.
std::string computeSlideId(const std::vector<SceneInfo>& scenes, uint64_t fileSizeBytes);

/// Convenience overload over `info.scenes`. Delegates to the primitive above so
/// an identity computed before a slide is opened and one computed afterwards
/// agree by construction, not by two call sites staying in step.
std::string computeSlideId(const SlideInfo& info, uint64_t fileSizeBytes);

} // namespace slideio::viewer::core
