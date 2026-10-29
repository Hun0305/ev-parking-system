#ifndef EVENTSPAGE_H
#define EVENTSPAGE_H

#include "models/monitoringevent.h"

#include <QWidget>

class QTableWidget;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

class EventsPage : public QWidget
{
    Q_OBJECT

public:
    explicit EventsPage(QWidget *parent = nullptr);
    void appendEvent(const MonitoringEvent &event);

signals:
    void exportResult(bool success, const QString &message);
    void eventEvidenceRequested(const QString &eventId);

private slots:
    void applyFilters();
    void exportCsv();
    void resetFilters();

private:
    void addFilterOption(QComboBox *comboBox, const QString &value);

    QTableWidget *m_eventTable = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_zoneFilter = nullptr;
    QComboBox *m_eventTypeFilter = nullptr;
    QComboBox *m_statusFilter = nullptr;
    QPushButton *m_resetFilterButton = nullptr;
    QLabel *m_filterResultLabel = nullptr;
};

#endif
