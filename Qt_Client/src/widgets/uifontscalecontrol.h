#ifndef UIFONTSCALECONTROL_H
#define UIFONTSCALECONTROL_H

#include <QWidget>

class QLabel;
class QToolButton;

class UiFontScaleControl : public QWidget
{
    Q_OBJECT

public:
    explicit UiFontScaleControl(QWidget *parent = nullptr);
    void setPercent(int percent);
    int percent() const { return m_percent; }

signals:
    void percentChangeRequested(int percent);

private:
    void updateControlState();

    QToolButton *m_decreaseButton = nullptr;
    QLabel *m_valueLabel = nullptr;
    QToolButton *m_increaseButton = nullptr;
    int m_percent = 100;
};

#endif
