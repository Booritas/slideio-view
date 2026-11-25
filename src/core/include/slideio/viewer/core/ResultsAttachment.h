#pragma once

#include "slideio/viewer/core/ResultsSource.h"
#include "slideio/viewer/core/Types.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace slideio::viewer::core
{

/// True when both UTF-8 paths name the same file after making them absolute,
/// lexically normalising them and dropping a trailing separator. Case-insensitive
/// (ASCII) on Windows only. No file system access beyond the current directory.
/// An empty path equals nothing; a path that cannot be converted equals nothing
/// and never throws.
[[nodiscard]] bool sameSlidePath(const std::string& a, const std::string& b);

enum class AttachmentDecision
{
    Attach,
    WarnSlideMoved,
    RefuseScene
};

/// `attachAnywaySlidePath` is the path the user already accepted for this
/// workspace, "" when none is remembered.
[[nodiscard]] AttachmentDecision decideAttachment(const ResultsProvenance& workspace,
                                                  const std::string& openSlidePath, int openScene,
                                                  const std::string& attachAnywaySlidePath);

struct ResultsLayerView
{
    std::string layerName;
    bool visible = true;
    std::optional<Color> colour;
    bool fill = false;
    double fillOpacity = 0.30;
    bool colourByClass = true;
    std::vector<std::string> hiddenClasses;  ///< class names
};

struct ResultsAttachmentSettings
{
    std::string slideId;
    int scene = 0;
    std::string workspacePath;
    std::string attachAnywaySlidePath;
    std::vector<ResultsLayerView> layers;
};

class IResultsSettingsStore
{
public:
    virtual ~IResultsSettingsStore() = default;
    [[nodiscard]] virtual std::optional<ResultsAttachmentSettings> find(const std::string& slideId,
                                                                        int scene) const = 0;
    virtual bool set(const ResultsAttachmentSettings& entry) = 0;
    virtual void remove(const std::string& slideId, int scene) = 0;
    [[nodiscard]] virtual std::map<std::string, Color> classColours() const = 0;  ///< keyed by caseFoldKey
    virtual void setClassColour(const std::string& className, Color colour) = 0;
};

} // namespace slideio::viewer::core
