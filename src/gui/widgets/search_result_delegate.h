#pragma once

#include <QStyledItemDelegate>
#include <QString>

namespace bookhub::gui {

// Custom data roles for the search results model.
namespace SearchRole {
    inline constexpr int BookId      = Qt::UserRole + 0;
    inline constexpr int Title       = Qt::UserRole + 1;
    inline constexpr int Author      = Qt::UserRole + 2;
    inline constexpr int PublishYear = Qt::UserRole + 3;
    inline constexpr int Languages   = Qt::UserRole + 4;  // QStringList
    inline constexpr int Sources     = Qt::UserRole + 5;  // QStringList
    inline constexpr int Formats     = Qt::UserRole + 6;  // QStringList
    inline constexpr int InLibrary   = Qt::UserRole + 7;  // bool
} // namespace SearchRole

// ---------------------------------------------------------------------------
// SearchResultDelegate — 72 px tall list rows for the Search screen.
//
// Renders: cover placeholder, title, author + year, language/source badges,
// format tags, and a "+ Library" / "✓ In Library" action button.
//
// Button clicks are forwarded as signals so SearchScreen can act on them
// without the delegate owning any domain logic.
// ---------------------------------------------------------------------------

class SearchResultDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit SearchResultDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

    bool editorEvent(QEvent *event,
                     QAbstractItemModel *model,
                     const QStyleOptionViewItem &option,
                     const QModelIndex &index) override;

signals:
    void addToLibraryRequested(const QString &bookId);
    void detailsRequested(const QString &bookId);

private:
    static QRect libraryButtonRect(const QRect &rowRect);
};

} // namespace bookhub::gui
