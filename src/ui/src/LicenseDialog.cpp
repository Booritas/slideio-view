#include "slideio/viewer/ui/LicenseDialog.h"

#include "slideio/viewer/ui/AboutInfo.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace slideio::viewer::ui
{

namespace
{

// The About dialog's palette, so the two read as one family in a dark
// application.
const char* const kDialogStyle =
    "QDialog { background: #2D2D2D; }"
    "QLabel { color: #CCCCCC; }"
    "QTabWidget::pane { border: 1px solid #555555; background: #252525; }"
    "QTabBar::tab { background: #333333; color: #CCCCCC; padding: 6px 14px; border: 1px solid #555555;"
    " border-bottom: none; }"
    "QTabBar::tab:selected { background: #252525; color: #FFFFFF; }"
    "QPlainTextEdit, QTextBrowser { background: #252525; color: #CCCCCC; border: none; }"
    "QPushButton { background: #3C3C3C; color: #CCCCCC; border: 1px solid #555555; padding: 5px 14px; }"
    "QPushButton:hover { background: #4A4A4A; }"
    "QPushButton:pressed { background: #4A90D9; color: #FFFFFF; }";

} // namespace

struct LicenseDialog::Impl
{
};

LicenseDialog::LicenseDialog(QWidget* parent)
    : QDialog(parent)
    , m_impl(std::make_unique<Impl>())
{
    setWindowTitle(QStringLiteral("License"));
    setStyleSheet(QString::fromLatin1(kDialogStyle));
    resize(640, 520);

    auto* layout = new QVBoxLayout(this);

    // Outside the tab widget, so it is on screen whichever tab is chosen.
    auto* notice = new QLabel(QString::fromLatin1(kNotForClinicalUseNotice));
    notice->setWordWrap(true);
    notice->setStyleSheet(QStringLiteral(
        "color: #FFD27F; font-weight: bold; padding: 6px; border: 1px solid #8A6D2B; background: #3A3220;"));
    layout->addWidget(notice);

    auto* tabs = new QTabWidget;

    auto* license = new QPlainTextEdit(licenseText());
    license->setReadOnly(true);
    license->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    license->setLineWrapMode(QPlainTextEdit::NoWrap);
    tabs->addTab(license, QStringLiteral("SlideIO &Viewer"));

    // Only the versions depend on the running process; the list itself does not.
    auto* notices = new QTextBrowser;
    notices->setOpenExternalLinks(true);
    notices->setHtml(formatThirdPartyHtml(thirdPartyComponents(collectAboutInfo(GpuInfo{}))));
    tabs->addTab(notices, QStringLiteral("&Third-Party Software"));

    layout->addWidget(tabs, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

LicenseDialog::~LicenseDialog() = default;

} // namespace slideio::viewer::ui
