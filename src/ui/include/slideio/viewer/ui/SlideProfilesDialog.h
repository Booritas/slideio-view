#pragma once

#include "slideio/viewer/core/ColorProfileOverride.h"

#include <QDialog>

#include <memory>
#include <string>

namespace slideio::viewer::ui
{

// One row of the manage dialog: what is stored, and what classifyDefaultProfile
// makes of the profile file right now.
struct SlideProfileRow
{
    std::string slideName;      ///< the stored display name, or the id if none
    std::string profilePath;
    std::string status;         ///< "OK", "File missing", "Not a profile", "Not RGB"
    bool displacesEmbedded = false;
};

// Classifies entry.profilePath the same way the open path does (inspectIccHeader
// + classifyDefaultProfile), so the dialog and the slide can never disagree
// about whether a profile is usable. Free function so the tests below can reach
// it without constructing a dialog.
SlideProfileRow buildSlideProfileRow(const core::ColorProfileOverride& entry);

// Lets the user see every stored per-slide ICC profile override and remove the
// ones no longer wanted. Nothing is pruned automatically elsewhere -- this
// dialog's buttons are the only way an entry goes away.
class SlideProfilesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SlideProfilesDialog(core::IColorProfileOverrideStore& store, QWidget* parent = nullptr);
    ~SlideProfilesDialog() override;

    SlideProfilesDialog(const SlideProfilesDialog&) = delete;
    SlideProfilesDialog& operator=(const SlideProfilesDialog&) = delete;

signals:
    // Emitted after any removal, so MainWindow can rebuild the policy and
    // reopen the slide on screen if its own entry went.
    void overridesChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
