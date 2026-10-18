#pragma once

#include <memory>
#include <string>
#include <vector>

class QSettings;

namespace slideio::viewer::infra
{

/// The user's annotation category list, persisted in QSettings under
/// "annotations/categories".
///
/// This deliberately has NO core interface, unlike
/// QSettingsColorProfileOverrideStore. That one exists because core reaches it.
/// Nothing in core needs the category list -- core needs only the pure
/// normalizeCategoryList -- and the consumers are ui panels, which already link
/// infra. An interface here would be inversion with nothing to invert.
class QSettingsAnnotationCategoryStore
{
public:
    /// FR-ANN-13's defaults, answered when nothing has ever been stored.
    /// "Unclassified" is not among them: it is the absence of a value.
    [[nodiscard]] static std::vector<std::string> defaultCategories();

    /// The application's own settings, as QSettings resolves them from the
    /// organisation and application names.
    QSettingsAnnotationCategoryStore();

    /// An explicit INI file. Tests use this so they never touch the user's
    /// real configuration.
    explicit QSettingsAnnotationCategoryStore(const std::string& iniFilePath);

    ~QSettingsAnnotationCategoryStore();

    /// The stored list, or the defaults when nothing has ever been stored. An
    /// explicitly emptied list is returned empty: clearing it was a choice.
    [[nodiscard]] std::vector<std::string> categories() const;

    /// Stores `categories` after core::normalizeCategoryList.
    void setCategories(const std::vector<std::string>& categories);

private:
    std::unique_ptr<QSettings> m_settings;
};

} // namespace slideio::viewer::infra
