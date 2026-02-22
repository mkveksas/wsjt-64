#include "DecodeHighlightingListView.hpp"

#include <QAction>
#include <QColorDialog>
#include <QStyledItemDelegate>

#include "models/DecodeHighlightingModel.hpp"
#include "MessageBox.hpp"

#include "moc_DecodeHighlightingListView.cpp"

namespace
{
QColor contrast_text_for (QColor const& background)
{
  // Perceived luminance (sRGB weights) for readable text over custom highlight fills.
  auto const luminance = (0.2126 * background.redF ())
                       + (0.7152 * background.greenF ())
                       + (0.0722 * background.blueF ());
  return luminance > 0.55 ? QColor {0, 0, 0} : QColor {255, 255, 255};
}

class DecodeHighlightingItemDelegate final
  : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

protected:
  void initStyleOption (QStyleOptionViewItem * option, QModelIndex const& index) const override
  {
    QStyledItemDelegate::initStyleOption (option, index);

    auto const foreground = index.data (Qt::ForegroundRole).value<QBrush> ();
    auto const background = index.data (Qt::BackgroundRole).value<QBrush> ();

    if (Qt::NoBrush == foreground.style () && Qt::NoBrush != background.style ())
      {
        auto const text = contrast_text_for (background.color ());
        option->palette.setColor (QPalette::Text, text);
        option->palette.setColor (QPalette::WindowText, text);
        option->palette.setColor (QPalette::HighlightedText, text);
      }
  }
};
}

DecodeHighlightingListView::DecodeHighlightingListView (QWidget * parent)
  : QListView {parent}
{
  setItemDelegate (new DecodeHighlightingItemDelegate {this});

  auto * fg_colour_action = new QAction {tr ("&Foreground color ..."), this};
  addAction (fg_colour_action);
  connect (fg_colour_action, &QAction::triggered, [this] (bool /*checked*/) {
      auto const& index = currentIndex ();
      auto colour = QColorDialog::getColor (model ()->data (index, Qt::ForegroundRole).value<QBrush> ().color ()
                                            , this
                                            , tr ("Choose %1 Foreground Color")
                                                .arg (model ()->data (index).toString ()));
      if (colour.isValid ())
        {
          model ()->setData (index, QBrush {colour}, Qt::ForegroundRole);
        }
    });

  auto * unset_fg_colour_action = new QAction {tr ("&Unset foreground color"), this};
  addAction (unset_fg_colour_action);
  connect (unset_fg_colour_action, &QAction::triggered, [this] (bool /*checked*/) {
      model ()->setData (currentIndex (), QBrush {}, Qt::ForegroundRole);
    });

  auto * bg_colour_action = new QAction {tr ("&Background color ..."), this};
  addAction (bg_colour_action);
  connect (bg_colour_action, &QAction::triggered, [this] (bool /*checked*/) {
      auto const& index = currentIndex ();
      auto colour = QColorDialog::getColor (model ()->data (index, Qt::BackgroundRole).value<QBrush> ().color ()
                                            , this
                                            , tr ("Choose %1 Background Color")
                                                .arg (model ()->data (index).toString ()));
      if (colour.isValid ())
        {
          model ()->setData (index, QBrush {colour}, Qt::BackgroundRole);
        }
    });

  auto * unset_bg_colour_action = new QAction {tr ("U&nset background color"), this};
  addAction (unset_bg_colour_action);
  connect (unset_bg_colour_action, &QAction::triggered, [this] (bool /*checked*/) {
      model ()->setData (currentIndex (), QBrush {}, Qt::BackgroundRole);
    });

  auto * defaults_action = new QAction {tr ("&Reset this item to defaults"), this};
  addAction (defaults_action);
  connect (defaults_action, &QAction::triggered, [this] (bool /*checked*/) {
      auto const& index = currentIndex ();
      model ()->setData (index, model ()->data (index, DecodeHighlightingModel::EnabledDefaultRole).toBool () ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
      model ()->setData (index, model ()->data (index, DecodeHighlightingModel::ForegroundDefaultRole), Qt::ForegroundRole);
      model ()->setData (index, model ()->data (index, DecodeHighlightingModel::BackgroundDefaultRole), Qt::BackgroundRole);
    });
}

QSize DecodeHighlightingListView::sizeHint () const
{
  auto item_height = sizeHintForRow (0);
  if (item_height >= 0)
    {
      // set the height hint to exactly the space required for all the
      // items
      return {width (), (model ()->rowCount () * (item_height + 2 * spacing ())) + 2 * frameWidth ()};
    }
  return QListView::sizeHint ();
}
