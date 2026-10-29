#include "pages/parkingmappage.h"

#include <QApplication>
#include <QMetaObject>
#include <QPushButton>
#include <QTemporaryDir>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 2;

    ParkingMapPage page(directory.filePath(QStringLiteral("parking_map_layout.json")));
    QPushButton *undoButton = page.findChild<QPushButton *>(QStringLiteral("undoLayoutButton"));
    QPushButton *namesButton = page.findChild<QPushButton *>(QStringLiteral("editChannelNamesButton"));
    if (!undoButton || !namesButton) return 3;
    if (namesButton->isEnabled()) return 4;
    if (!QMetaObject::invokeMethod(&page, "setEditMode", Qt::DirectConnection,
                                   Q_ARG(bool, true))) return 5;
    if (namesButton->isEnabled()) return 6;
    if (page.hasUnsavedLayoutChanges()) return 7;
    return 0;
}
