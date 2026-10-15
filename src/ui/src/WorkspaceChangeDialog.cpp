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
    }

    layout->addWidget(buttons);
}

app::WorkspaceChoice WorkspaceChangeDialog::choice() const
{
    return m_choice;
}

} // namespace slideio::viewer::ui
