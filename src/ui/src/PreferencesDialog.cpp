#include "slideio/viewer/ui/PreferencesDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace slideio::viewer::ui
{

PreferencesDialog::PreferencesDialog(const QString& workspaceDirectory, const QString& userName,
                                     const QStringList& categories,
                                     const BuiltLevelCacheSettings& cacheSettings, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));

    m_workspaceEdit = new QLineEdit(workspaceDirectory, this);
    m_userNameEdit = new QLineEdit(userName, this);
    m_userNameEdit->setPlaceholderText(tr("Required before creating annotations"));

    m_categoriesEdit = new QPlainTextEdit(categories.join('\n'), this);
    m_categoriesEdit->setToolTip(tr("One per line. Used for counting and for the Classification field."));
    m_categoriesEdit->setPlaceholderText(tr("One per line"));

    auto* browse = new QPushButton(tr("Browse..."), this);
    connect(browse, &QPushButton::clicked, this, &PreferencesDialog::browseForWorkspace);

    auto* workspaceRow = new QHBoxLayout;
    workspaceRow->addWidget(m_workspaceEdit, 1);
    workspaceRow->addWidget(browse);

    auto* form = new QFormLayout;
    form->addRow(tr("Annotation workspace:"), workspaceRow);
    form->addRow(tr("Your name:"), m_userNameEdit);
    form->addRow(tr("Annotation categories:"), m_categoriesEdit);

    // Said plainly, because the alternative -- moving a user's files as a side
    // effect of changing a setting -- is worse than leaving them where they are.
    auto* note = new QLabel(
        tr("Annotations already saved stay where they are. Changing this folder "
           "affects only annotations saved from now on."),
        this);
    note->setWordWrap(true);

    // Built level cache. Off by default; a change applies to the next slide opened.
    auto* cacheGroup = new QGroupBox(tr("Built level cache"), this);
    cacheGroup->setToolTip(
        tr("Speeds up reopening large slides that have no stored overview. Entries hold image pixels and "
           "the slide's file path; keep the folder on local, access-controlled storage. Turning the cache off "
           "does not delete entries; they are deleted when their retention runs out."));
    m_cacheEnabledBox = new QCheckBox(tr("Cache built levels on disk"), cacheGroup);
    m_cacheEnabledBox->setChecked(cacheSettings.enabled);
    m_cacheFolderEdit = new QLineEdit(cacheSettings.folder, cacheGroup);
    m_cacheFolderEdit->setPlaceholderText(defaultBuiltLevelCacheFolder());
    m_cacheBrowseButton = new QPushButton(tr("Browse..."), cacheGroup);
    connect(m_cacheBrowseButton, &QPushButton::clicked, this, &PreferencesDialog::browseForCacheFolder);
    m_cacheDaysSpin = new QSpinBox(cacheGroup);
    m_cacheDaysSpin->setRange(clampCacheRetentionDays(0), clampCacheRetentionDays(1000000));
    m_cacheDaysSpin->setSuffix(tr(" days"));
    m_cacheDaysSpin->setValue(clampCacheRetentionDays(cacheSettings.retentionDays));
    connect(m_cacheEnabledBox, &QCheckBox::toggled, this, &PreferencesDialog::updateCacheControls);

    auto* cacheFolderRow = new QHBoxLayout;
    cacheFolderRow->addWidget(m_cacheFolderEdit, 1);
    cacheFolderRow->addWidget(m_cacheBrowseButton);

    auto* cacheForm = new QFormLayout(cacheGroup);
    cacheForm->addRow(m_cacheEnabledBox);
    cacheForm->addRow(tr("Folder:"), cacheFolderRow);
    cacheForm->addRow(tr("Keep for:"), m_cacheDaysSpin);
    updateCacheControls();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(cacheGroup);
    layout->addWidget(buttons);
}

QString PreferencesDialog::workspaceDirectory() const
{
    return m_workspaceEdit->text().trimmed();
}

QString PreferencesDialog::userName() const
{
    return m_userNameEdit->text().trimmed();
}

QStringList PreferencesDialog::categories() const
{
    return m_categoriesEdit->toPlainText().split('\n', Qt::SkipEmptyParts);
}

BuiltLevelCacheSettings PreferencesDialog::builtLevelCacheSettings() const
{
    BuiltLevelCacheSettings value;
    value.enabled = m_cacheEnabledBox->isChecked();
    value.folder = m_cacheFolderEdit->text().trimmed();
    value.retentionDays = clampCacheRetentionDays(m_cacheDaysSpin->value());
    return value;
}

void PreferencesDialog::updateCacheControls()
{
    const bool enabled = m_cacheEnabledBox->isChecked();
    m_cacheFolderEdit->setEnabled(enabled);
    m_cacheBrowseButton->setEnabled(enabled);
    m_cacheDaysSpin->setEnabled(enabled);
}

void PreferencesDialog::browseForCacheFolder()
{
    const QString start = resolveBuiltLevelCacheFolder(m_cacheFolderEdit->text());
    const QString chosen =
        QFileDialog::getExistingDirectory(this, tr("Choose the built level cache folder"), start);
    if (!chosen.isEmpty()) {
        m_cacheFolderEdit->setText(chosen);
    }
}

void PreferencesDialog::browseForWorkspace()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, tr("Choose the annotation workspace folder"), m_workspaceEdit->text());
    if (!chosen.isEmpty()) {
        m_workspaceEdit->setText(chosen);
    }
}

} // namespace slideio::viewer::ui
