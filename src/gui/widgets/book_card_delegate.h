#pragma once

#include <QStyledItemDelegate>
#include <QString>

namespace bookhub::gui {

// ---------------------------------------------------------------------------
// BookCardDelegate — custom list-view delegate for library book cards.
//
// List mode (default): each row is BookCardRowHeight px tall and renders a
//   thumbnail, title/author, language+source badges, status label, and three
//   action buttons (Details / Download or Audiobook / Remove).
//
// Grid mode: each cell is BookCardGridW × BookCardGridH px.
//
// Custom data roles (used by the model to pass structured data):
//   Qt::UserRole + 0  = book_id         (QString)
//   Qt::UserRole + 1  = title           (QString)
//   Qt::UserRole + 2  = author          (QString)
//   Qt::UserRole + 3  = language        (QString)
//   Qt::UserRole + 4  = source_name     (QString)
//   Qt::UserRole + 5  = status          (QString)
//   Qt::UserRole + 6  = library_item_id (int)
//   Qt::UserRole + 7  = edition_id      (int)
// ---------------------------------------------------------------------------

// Role constants — defined here so model and delegate stay in sync.
namespace LibraryRole {
    inline constexpr int BookId         = Qt::UserRole + 0;
    inline constexpr int Title          = Qt::UserRole + 1;
    inline constexpr int Author         = Qt::UserRole + 2;
    inline constexpr int Language       = Qt::UserRole + 3;
    inline constexpr int SourceName     = Qt::UserRole + 4;
    inline constexpr int Status         = Qt::UserRole + 5;
    inline constexpr int LibraryItemId  = Qt::UserRole + 6;
    inline constexpr int EditionId      = Qt::UserRole + 7;
} // namespace LibraryRole

enum class ViewMode { List, Grid };

class BookCardDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit BookCardDelegate(QObject *parent = nullptr);

    void setViewMode(ViewMode mode);

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

    // editorEvent is used to handle button clicks within the delegate cell.
    bool editorEvent(QEvent *event,
                     QAbstractItemModel *model,
                     const QStyleOptionViewItem &option,
                     const QModelIndex &index) override;

signals:
    void detailsRequested(int libraryItemId, const QString &bookId);
    void downloadRequested(int libraryItemId, const QString &bookId);
    void removeRequested(int libraryItemId, const QString &bookId);

private:
    void paintListRow(QPainter *painter,
                      const QStyleOptionViewItem &option,
                      const QModelIndex &index) const;

    void paintGridCard(QPainter *painter,
                       const QStyleOptionViewItem &option,
                       const QModelIndex &index) const;

    // Returns display text and colour for a given status value.
    static std::pair<QString, QString> statusDisplay(const QString &status);

    // Returns the QRect for each of the three action buttons within a list row.
    static QRect detailsButtonRect(const QRect &rowRect);
    static QRect actionButtonRect(const QRect &rowRect);
    static QRect removeButtonRect(const QRect &rowRect);

    ViewMode m_viewMode{ViewMode::List};
};

} // namespace bookhub::gui
