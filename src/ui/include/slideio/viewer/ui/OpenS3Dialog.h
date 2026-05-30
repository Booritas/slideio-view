#pragma once

#include <QDialog>

#include <memory>
#include <string>

namespace slideio::viewer::ui
{

// Modal dialog for opening a whole-slide image hosted on AWS S3 via a presigned
// URL. The user pastes the URL and picks a driver explicitly (a presigned URL
// carries no usable file extension, so there is no auto-detect entry). On
// accept, the caller reads presignedUrl()/driverId() and forwards them to
// MainWindow::openSlide, which passes them straight to SlideIO — slideio reads
// http/https/s3 URIs natively.
class OpenS3Dialog : public QDialog
{
    Q_OBJECT

public:
    explicit OpenS3Dialog(QWidget* parent = nullptr);
    ~OpenS3Dialog() override;

    OpenS3Dialog(const OpenS3Dialog&) = delete;
    OpenS3Dialog& operator=(const OpenS3Dialog&) = delete;

    // Valid after exec() returns QDialog::Accepted.
    std::string presignedUrl() const;  // trimmed URL text
    std::string driverId() const;      // SlideIO driver id, e.g. "SVS"

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slideio::viewer::ui
