#pragma once

#include "slideio/viewer/core/Types.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace slideio::viewer::core
{

/// One closed ring of an object's outline, in level-0 coordinates.
struct ResultRing
{
    std::vector<PointF> points;
    bool hole = false;
};

/// One processor object as the overlay draws and hit-tests it.
struct ResultObject
{
    std::string id;                 ///< the processor's ObjectId text
    int layer = -1;                 ///< index into ResultsSource::layers()
    std::vector<int> classes;       ///< indices into that layer's classes, effective set order; empty = unclassified
    std::vector<ResultRing> rings;  ///< level-0 coordinates
    Rect<double> bounds;
};

struct ResultClassInfo
{
    std::string name;
    int64_t objectCount = 0;
};

struct ResultLayerInfo
{
    std::string name;
    int64_t objectCount = 0;
    std::vector<ResultClassInfo> classes;   ///< legend order: descending objectCount, then name
    std::string stageState;                 ///< "", "stale" or "failed"
};

struct ResultsProvenance
{
    std::string slidePath;
    int scene = 0;
};

struct ResultMeasurement
{
    std::string name;
    double value = 0.0;
    std::string unit;
    std::string assertedBy;
};

struct ResultDetails
{
    std::string layer;
    std::vector<std::string> classes;
    std::string stage;
    std::vector<ResultMeasurement> measurements;  ///< own first (store order), then asserted
};

struct ResultQueryAnswer
{
    bool overBudget = false;
    int64_t inView = 0;
    std::vector<ResultObject> objects;
};

class ResultsError : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// Read-only view of a slideio-processor results store. Core defines the
/// contract; infra implements it.
class ResultsSource
{
public:
    virtual ~ResultsSource() = default;
    virtual ResultsProvenance provenance() const = 0;
    virtual std::vector<ResultLayerInfo> layers() const = 0;
    /// Objects of `layer` intersecting `rect`; overBudget with inView set when more than `budget`.
    virtual ResultQueryAnswer query(int layer, const Rect<double>& rect, int64_t budget) = 0;
    virtual std::optional<ResultDetails> details(const std::string& objectId) = 0;
    /// Visits every object of `layer` as (bounds, classes); `progress(fraction)` returning false cancels.
    virtual void scan(int layer,
                      const std::function<void(const Rect<double>&, const std::vector<int>&)>& visit,
                      const std::function<bool(double)>& progress) = 0;
};

constexpr int64_t kResultsDrawBudget = 20000;

} // namespace slideio::viewer::core
