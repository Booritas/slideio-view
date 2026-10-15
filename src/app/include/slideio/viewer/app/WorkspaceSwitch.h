#pragma once

#include "slideio/viewer/core/AnnotationMerge.h"
#include "slideio/viewer/core/AnnotationRepository.h"
#include "slideio/viewer/core/WorkspaceSwitchPolicy.h"

#include <functional>
#include <string>

namespace slideio::viewer::app
{

class AnnotationModel;
class AnnotationPersistenceService;

enum class WorkspaceChoice
{
    /// Write the on-screen set into a new folder that has no file for this key.
    Copy,
    /// Write the on-screen set over the file the new folder already has.
    KeepCurrent,
    Merge,
    /// Take whatever the new folder holds, writing nothing.
    LoadNew,
    Cancel,
};

struct WorkspaceSwitchResult
{
    WorkspaceChoice choice = WorkspaceChoice::Cancel;
    /// False when the facts did not warrant a dialog.
    bool asked = false;
    /// The save-failure resolver rescued the flush to a folder the user picked
    /// mid-failure, and made that folder the workspace. It beats the one typed
    /// into Preferences: the switch is abandoned, with no probe and no dialog.
    bool rescued = false;
    /// How step 1's flush to the old folder went, so the caller can say so.
    core::SaveStatus flush = core::SaveStatus::NothingToDo;
    /// Zeroed unless choice == Merge.
    core::MergeOutcome merge;
    core::SaveStatus write = core::SaveStatus::NothingToDo;
    /// Empty when nothing was written.
    std::string writePath;
};

/// Moves the open slide's annotations across a change of workspace folder.
///
/// The order is the design, and it is the reason this lives in app rather than
/// in the dialog's caller: every step of it is testable here, and none of it is
/// testable in a widget.
///
///   1. flush to the OLD root, so what is on screen is also on disk there
///   2. capture the in-memory set
///   3. re-point to the new root and load() -- that load is the probe
///   4. build the facts from the capture and the probe
///   5. ask the resolver, which is the dialog
///   6. apply the choice, writing at most once
///   7. beginSlide against the root now in effect
///
/// Step 7 is not optional on any path, cancel included: step 1 deactivated the
/// service, and a slide left open with a deactivated service stops saving
/// silently.
///
/// The repository's root is reached through callbacks rather than through the
/// repository interface: core::IAnnotationRepository exposes only load, save and
/// pathFor, and a filesystem root does not belong on it -- a remote repository
/// has none.
class WorkspaceSwitch
{
public:
    using Resolver = std::function<WorkspaceChoice(const core::WorkspaceSwitchFacts&,
                                                   const core::WorkspaceSwitchOptions&)>;

    WorkspaceSwitch(core::IAnnotationRepository& repository, AnnotationModel& model,
                    AnnotationPersistenceService& persistence,
                    std::function<std::string()> currentRoot,
                    std::function<void(std::string)> repointTo);

    void setResolver(Resolver resolver);

    /// `key` and `provenance` describe the slide that is open. With no resolver
    /// set, nothing is written: this never guesses at the user's expense.
    WorkspaceSwitchResult run(const std::string& newRoot, const core::AnnotationKey& key,
                              const core::SlideProvenance& provenance);

private:
    core::IAnnotationRepository& m_repository;
    AnnotationModel& m_model;
    AnnotationPersistenceService& m_persistence;
    std::function<std::string()> m_currentRoot;
    std::function<void(std::string)> m_repointTo;
    Resolver m_resolver;
};

} // namespace slideio::viewer::app
