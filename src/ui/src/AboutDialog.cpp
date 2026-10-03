#include "slideio/viewer/ui/AboutDialog.h"

#include "slideio/viewer/ui/AboutInfo.h"

#include <QClipboard>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

namespace slideio::viewer::ui
{

namespace
{

// Matches the panels, which style themselves rather than relying on a global
// palette. Without this the dialog would come up in the platform's light theme
// in the middle of a dark application.
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

// The year and holder shown under the application name. Taken from the licence
// rather than repeated here, so that updating LICENSE updates the dialog.
QString copyrightLine(const QString& license)
{
    const QStringList lines = license.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        if (line.startsWith(QStringLiteral("Copyright"))) {
            return line.trimmed();
        }
    }
    return QString();
}

QPlainTextEdit* makeTextTab(const QString& text, const QFont& font)
{
    auto* view = new QPlainTextEdit(text);
    view->setReadOnly(true);
    view->setFont(font);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    return view;
}

QString thirdPartyHtml(const QList<ThirdPartyComponent>& components)
{
    QString html = QStringLiteral("<body style='color:#CCCCCC;'>");
    html += QStringLiteral("<p>SlideIO Viewer is distributed with the following components:</p><ul>");
    for (const ThirdPartyComponent& component : components) {
        html += QStringLiteral("<li><b>%1</b> %2 &mdash; %3<br/>"
                               "<a style='color:#6FA8E0;' href='%4'>%4</a></li>")
                    .arg(component.name.toHtmlEscaped(), component.version.toHtmlEscaped(),
                         component.license.toHtmlEscaped(), component.url.toHtmlEscaped());
    }
    html += QStringLiteral("</ul><p>SlideIO statically links further third-party libraries "
                           "(OpenCV, DCMTK, libtiff, GDAL and others), each under its own "
                           "licence.</p></body>");
    return html;
}

} // namespace

struct AboutDialog::Impl
{
    AboutInfo info;
    QPushButton* copyButton = nullptr;
};

AboutDialog::AboutDialog(const GpuInfo& gpu, QWidget* parent)
    : QDialog(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->info = collectAboutInfo(gpu);

    setWindowTitle(QStringLiteral("About SlideIO Viewer"));
    setStyleSheet(QString::fromLatin1(kDialogStyle));
    resize(640, 480);

    auto* layout = new QVBoxLayout(this);

    // Header: icon, application name, version, copyright.
    auto* header = new QHBoxLayout;
    const QPixmap icon = QIcon(QStringLiteral(":/icons/app.png")).pixmap(48, 48);
    if (!icon.isNull()) {
        auto* iconLabel = new QLabel;
        iconLabel->setPixmap(icon);
        iconLabel->setFixedSize(48, 48);
        header->addWidget(iconLabel);
        header->addSpacing(12);
    }

    auto* titleColumn = new QVBoxLayout;
    auto* nameLabel = new QLabel(QStringLiteral("SlideIO Viewer"));
    QFont nameFont = nameLabel->font();
    nameFont.setPointSize(nameFont.pointSize() + 5);
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);
    titleColumn->addWidget(nameLabel);

    auto* versionLabel = new QLabel(QStringLiteral("Version %1 (%2)")
                                        .arg(m_impl->info.appVersion, m_impl->info.gitRevision));
    versionLabel->setStyleSheet(QStringLiteral("color: #999999;"));
    titleColumn->addWidget(versionLabel);

    const QString license = licenseText();
    const QString copyright = copyrightLine(license);
    if (!copyright.isEmpty()) {
        auto* copyrightLabel = new QLabel(copyright);
        copyrightLabel->setStyleSheet(QStringLiteral("color: #999999;"));
        titleColumn->addWidget(copyrightLabel);
    }

    header->addLayout(titleColumn);
    header->addStretch();
    layout->addLayout(header);

    // Tabs. Diagnostics are laid out in aligned columns, so they need the
    // fixed-width font; the notices are prose with links.
    const QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    auto* tabs = new QTabWidget;
    tabs->addTab(makeTextTab(formatVersionInfo(m_impl->info), fixedFont), QStringLiteral("&Version"));
    tabs->addTab(makeTextTab(formatSystemInfo(m_impl->info), fixedFont), QStringLiteral("&System"));

    auto* notices = new QTextBrowser;
    notices->setOpenExternalLinks(true);
    notices->setHtml(thirdPartyHtml(thirdPartyComponents(m_impl->info)));
    tabs->addTab(notices, QStringLiteral("&Third-Party"));

    tabs->addTab(makeTextTab(license, fixedFont), QStringLiteral("&License"));
    layout->addWidget(tabs, 1);

    // Buttons.
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    m_impl->copyButton = buttons->addButton(QStringLiteral("&Copy to Clipboard"),
                                            QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_impl->copyButton, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(formatAboutReport(m_impl->info));

        // Nothing else visibly happens on a copy, so say that it worked and
        // put the label back afterwards.
        m_impl->copyButton->setText(QStringLiteral("Copied"));
        m_impl->copyButton->setEnabled(false);
        QTimer::singleShot(1500, this, [this]() {
            m_impl->copyButton->setText(QStringLiteral("&Copy to Clipboard"));
            m_impl->copyButton->setEnabled(true);
        });
    });
}

AboutDialog::~AboutDialog() = default;

} // namespace slideio::viewer::ui
