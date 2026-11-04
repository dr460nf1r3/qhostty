#include "WebShortcuts.h"

#include <QAction>
#include <QMenu>
#include <QString>

#if defined(QHOSTTY_HAS_KIO)
#include <KConfigGroup>
#include <KSharedConfig>
#include <KUriFilter>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QIcon>
#include <QUrl>
#endif

namespace qhostty {

#if defined(QHOSTTY_HAS_KIO)
namespace {
bool webShortcutsEnabled() {
  const KSharedConfig::Ptr config =
      KSharedConfig::openConfig(QStringLiteral("kuriikwsfilterrc"));
  return config->group(QStringLiteral("General"))
      .readEntry("EnableWebShortcuts", true);
}

QUrl searchUrl(const QString& query) {
  KUriFilterData filter(query);
  // Only search providers, so that the text is never run as a command or
  // opened as a path.
  if (KUriFilter::self()->filterSearchUri(filter,
                                          KUriFilter::WebShortcutFilter)) {
    return filter.uri();
  }
  return {};
}
}  // namespace

QMenu* addWebShortcuts(QMenu* menu, QAction* before, const QString& text) {
  const QString query = text.simplified();
  if (query.isEmpty() || !webShortcutsEnabled()) {
    return nullptr;
  }

  KUriFilterData filter(query);
  filter.setSearchFilteringOptions(
      KUriFilterData::RetrievePreferredSearchProvidersOnly);
  if (!KUriFilter::self()->filterSearchUri(filter,
                                           KUriFilter::NormalTextFilter)) {
    return nullptr;
  }
  const QStringList providers = filter.preferredSearchProviders();
  if (providers.isEmpty()) {
    return nullptr;
  }

  QString shown = query.size() > 21 ? query.left(20) + u'…' : query;
  shown.replace(u'&', QStringLiteral("&&"));
  auto* submenu = new QMenu(
      QCoreApplication::translate("WebShortcuts", "Search for '%1' with")
          .arg(shown),
      menu);
  for (const QString& provider : providers) {
    QAction* action = submenu->addAction(
        QIcon::fromTheme(filter.iconNameForPreferredSearchProvider(provider)),
        provider);
    const QString providerQuery =
        filter.queryForPreferredSearchProvider(provider);
    QObject::connect(action, &QAction::triggered, submenu, [providerQuery]() {
      if (const QUrl url = searchUrl(providerQuery); url.isValid()) {
        QDesktopServices::openUrl(url);
      }
    });
  }
  menu->insertMenu(before, submenu);
  return submenu;
}
#else
QMenu* addWebShortcuts(QMenu*, QAction*, const QString&) {
  return nullptr;
}
#endif

}  // namespace qhostty
