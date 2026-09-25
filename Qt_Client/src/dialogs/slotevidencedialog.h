#ifndef SLOTEVIDENCEDIALOG_H
#define SLOTEVIDENCEDIALOG_H

#include "api/parkingmodels.h"
#include "models/parkingstate.h"

#include <QDialog>
#include <QList>

class ImageLoader;

class SlotEvidenceDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SlotEvidenceDialog(const QString &slotId, SlotState state,
                                const QString &plateNumber,
                                const QList<ParkingImageResource> &images,
                                ImageLoader *imageLoader,
                                QWidget *parent = nullptr);
};

#endif
