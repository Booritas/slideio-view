#include "slideio/viewer/ui/WorkspaceChangeDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace slideio::viewer::ui
{

namespace
{

QString nativePath(const std::string& path)
{
    return QDir::toNativeSeparators(QString::fromStdString(path));
}

} // namespace

WorkspaceChangeDialog::WorkspaceChangeDialog(const core::WorkspaceSwitchFacts& facts,
                                             const core::WorkspaceSwitchOptions& options,
                                             QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Change annotation folder"));

    auto* layout = new QVBoxLayout(this);

    auto* headline = new QLabel(
        facts.newFileExists
            ? tr("The new annotation folder already has annotations for this slide.")
            : tr("The new annotation folder has no annotations for this slide."),
        this);
    headline->setWordWrap(true);
    QFont bold = headline->font();
    bold.setBold(true);
    headline->setFont(bold);
    layout->addWidget(headline);

    QString detail = tr("On screen now: %1 — saved in %2")
                         .arg(facts.currentCount)
                         .arg(nativePath(facts.oldPath));
    if (facts.newFileExists && facts.newFileReadable) {
        detail += tr("\nIn the new folder: %1 — %2").arg(facts.newCount).arg(nativePath(facts.newPath));
    } else if (facts.newFileExists) {
        detail += tr("\nThe new folder's file could not be read, so it will not be written to: %1")
                      .arg(nativePath(facts.newPath));
    }
    if (facts.flushFailed) {
        detail += tr("\nThe current folder could not be written. The annotations on screen are "
                     "still here, and copying them is how they are kept.");
    }
    if (!facts.currentWritable) {
        detail += tr("\nThe annotations on screen are open read-only and are not carried over.");
    }
    auto* detailLabel = new QLabel(detail, this);
    detailLabel->setWordWrap(true);
    detailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(detailLabel);

    auto* explanation = new QLabel(
        tr("Annotations are read from the folder in use, so changing it replaces what you see. "
           "Nothing is deleted from the current folder. Only the scene you are viewing is carried "
           "over; other scenes and other slides stay where they are. Undo history is cleared."),
        this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto* buttons = new QDialogButtonBox(this);

    const auto addChoice = [this, buttons](const QString& text, app::WorkspaceChoice choice,
                                           QDialogButtonBox::ButtonRole role) {
        QPushButton* button = buttons->addButton(text, role);
        connect(button, &QPushButton::clicked, this, [this, choice]() {
            m_choice = choice;
            accept();
        });
        return button;
    };

    QPushButton* preferred = nullptr;
    if (options.merge) {
        preferred = addChoice(tr("Merge"), app::WorkspaceChoice::Merge,
                              QDialogButtonBox::AcceptRole);
    }
    if (options.copy) {
        preferred = addChoice(tr("Copy to the new folder"), app::WorkspaceChoice::Copy,
                              QDialogButtonBox::AcceptRole);
    }
    if (options.keepCurrent) {
        QPushButton* keep = addChoice(tr("Keep what's on screen"), app::WorkspaceChoice::KeepCurrent,
                                      QDialogButtonBox::AcceptRole);
        keep->setToolTip(tr("Replaces the %1 annotations in the new folder's file.")
                             .arg(facts.newCount));
    }
    if (options.loadNew) {
        addChoice(facts.newFileExists ? tr("Use the new folder's") : tr("Switch without copying"),
                  app::WorkspaceChoice::LoadNew, QDialogButtonBox::DestructiveRole);
    }

    QPushButton* cancel = buttons->addButton(QDialogButtonBox::Cancel);
    connect(cancel, &QPushButton::clicked, this, [this]() {
        m_choice = app::WorkspaceChoice::Cancel;
        reject();
    });

    if (preferred != nullptr) {
        preferred->setDefault(true);
        preferred->setFocus();
    } else {
        // Nothing constructive is on offer -- the new folder's file could not be
        // parsed, or these annotations are read-only. The remaining choice
        // switches away from what is on screen, so Enter must not reach for it.
        //
        // setDefault() alone does not achieve that, which check X16 caught:
        // QPushButton::autoDefault is on for every button in a dialog, and an
        // auto-default button takes over as the default while it has focus. No
        // one had given Cancel focus, so focus went to the first button in the
        // box -- the switch -- and it became the default regardless of what was
        // set here. Focus is what decides; setDefault only decides among
        // unfocused buttons.
        cancel->setDefault(true);
        cancel->setFocus();
    }

    layout->addWidget(buttons);
}

app::WorkspaceChoice WorkspaceChangeDialog::choice() const
{
    return m_choice;
}

} // namespace slideio::viewer::ui
