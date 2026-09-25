#include "eventspage.h"

#include "widgets/pagehelp.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QColor>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

EventsPage::EventsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addLayout(createPageHeader(
        this, QStringLiteral("Events"),
        QStringLiteral("Search, filter, and export monitoring history")));

    auto *filterPanel = new QFrame(this);
    filterPanel->setObjectName(QStringLiteral("eventFilterPanel"));
    filterPanel->setStyleSheet(QStringLiteral(
        "QFrame#eventFilterPanel { background:#f8fafb; border:1px solid #c7d0d8; "
        "border-radius:6px; }"));
    auto *filterPanelLayout = new QVBoxLayout(filterPanel);
    filterPanelLayout->setContentsMargins(10, 9, 10, 9);
    filterPanelLayout->setSpacing(8);

    auto *searchLayout = new QHBoxLayout;
    searchLayout->setSpacing(8);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("eventSearchEdit"));
    m_searchEdit->setPlaceholderText(
        QStringLiteral("Search time, zone, event, message, or status"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMinimumWidth(260);
    searchLayout->addWidget(m_searchEdit, 1);

    auto *exportButton = new QPushButton(QStringLiteral("Export CSV"), filterPanel);
    exportButton->setObjectName(QStringLiteral("eventExportButton"));
    exportButton->setToolTip(
        QStringLiteral("Export the complete event log, regardless of filters"));
    searchLayout->addWidget(exportButton);
    filterPanelLayout->addLayout(searchLayout);

    auto *filterLayout = new QHBoxLayout;
    filterLayout->setSpacing(8);

    m_zoneFilter = new QComboBox(this);
    m_zoneFilter->setObjectName(QStringLiteral("eventZoneFilter"));
    m_zoneFilter->addItem(QStringLiteral("All zones"), QString());
    m_zoneFilter->setMinimumWidth(120);
    filterLayout->addWidget(m_zoneFilter);

    m_eventTypeFilter = new QComboBox(this);
    m_eventTypeFilter->setObjectName(QStringLiteral("eventTypeFilter"));
    m_eventTypeFilter->addItem(QStringLiteral("All events"), QString());
    m_eventTypeFilter->setMinimumWidth(170);
    filterLayout->addWidget(m_eventTypeFilter);

    m_statusFilter = new QComboBox(this);
    m_statusFilter->setObjectName(QStringLiteral("eventStatusFilter"));
    m_statusFilter->addItem(QStringLiteral("All statuses"), QString());
    addFilterOption(m_statusFilter, QStringLiteral("OPEN"));
    addFilterOption(m_statusFilter, QStringLiteral("ACKED"));
    addFilterOption(m_statusFilter, QStringLiteral("CLEARED"));
    m_statusFilter->setMinimumWidth(130);
    filterLayout->addWidget(m_statusFilter);

    m_resetFilterButton = new QPushButton(QStringLiteral("Reset filters"), this);
    m_resetFilterButton->setObjectName(QStringLiteral("eventFilterResetButton"));
    m_resetFilterButton->setEnabled(false);
    filterLayout->addWidget(m_resetFilterButton);
    filterLayout->addStretch();
    m_filterResultLabel = new QLabel(QStringLiteral("Showing 0 of 0 events"), filterPanel);
    m_filterResultLabel->setObjectName(QStringLiteral("eventFilterResultLabel"));
    m_filterResultLabel->setStyleSheet(QStringLiteral(
        "color:#607d8b;font-size:11px;font-weight:700;"));
    filterLayout->addWidget(m_filterResultLabel);
    filterPanelLayout->addLayout(filterLayout);
    createPageHelpButton(
        this, this,
        {QStringLiteral("events"), QStringLiteral("Events"),
         QStringLiteral("Events 사용 안내"),
         QStringLiteral("수신한 정규화 이벤트를 검색·필터링하고 전체 이력을 내보냅니다."),
         QStringLiteral(
             "<b>1. 이벤트 검색</b><br>검색창은 시간, Zone, Event, Message, Status 전체 열을 대상으로 합니다.<br><br>"
             "<b>2. 조건 필터링</b><br>Zone, 이벤트 종류, OPEN·ACKED·CLEARED 상태 필터는 검색어와 함께 적용됩니다.<br><br>"
             "<b>3. 증거 화면 이동</b><br>주차 슬롯 이벤트를 선택한 뒤 <i>Open Evidence</i>를 누르거나 행을 더블클릭하면 해당 이벤트의 증거 화면으로 이동합니다.<br><br>"
             "<b>4. 화면 상태</b><br>이벤트가 없거나 서버/API를 사용할 수 없을 때 목록 위 안내에서 현재 상태를 확인합니다.<br><br>"
             "<b>5. CSV 저장</b><br><i>Export CSV</i>를 누르면 현재 필터와 관계없이 전체 이벤트 로그를 저장합니다."),
         QStringLiteral(
             "※ 화재 채널이나 SYSTEM처럼 주차 슬롯으로 연결되지 않는 이벤트는 Evidence로 이동하지 않습니다.\n"
             "   Reset filters는 검색어와 모든 필터를 초기화합니다.")});
    layout->addWidget(filterPanel);

    m_stateLabel = new QLabel(this);
    m_stateLabel->setObjectName(QStringLiteral("eventViewStateLabel"));
    m_stateLabel->setWordWrap(true);
    layout->addWidget(m_stateLabel);

    m_eventTable = new QTableWidget(0, 5, this);
    m_eventTable->setObjectName(QStringLiteral("eventLogTable"));
    m_eventTable->setHorizontalHeaderLabels({QStringLiteral("Time"), QStringLiteral("Zone"), QStringLiteral("Event"), QStringLiteral("Message"), QStringLiteral("Status")});
    auto *header = m_eventTable->horizontalHeader();
    header->setSectionsClickable(true);
    header->setSortIndicatorShown(true);
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(2, QHeaderView::Interactive);
    header->setSectionResizeMode(3, QHeaderView::Stretch);
    header->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_eventTable->setColumnWidth(2, 180);
    m_eventTable->verticalHeader()->setVisible(false);
    m_eventTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_eventTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_eventTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_eventTable->setAlternatingRowColors(true);
    m_eventTable->setSortingEnabled(true);
    header->setSortIndicator(0, Qt::DescendingOrder);
    m_eventTable->setStyleSheet(QStringLiteral(
        "QTableWidget { background:#f8fafb; alternate-background-color:#eef3f6; "
        "border:1px solid #c7d0d8; gridline-color:#d7e0e6; }"
        "QTableWidget::item { padding:5px 7px; }"
        "QTableWidget::item:selected { background:#dceaf5; color:#10212c; }"));
    layout->addWidget(m_eventTable, 1);
    auto *buttonLayout = new QHBoxLayout;
    m_selectionLabel = new QLabel(
        QStringLiteral("Select a parking event to review its evidence."), this);
    m_selectionLabel->setObjectName(QStringLiteral("eventSelectionHintLabel"));
    m_selectionLabel->setWordWrap(true);
    m_selectionLabel->setStyleSheet(QStringLiteral("color:#607d8b;font-size:11px;"));
    buttonLayout->addWidget(m_selectionLabel, 1);
    m_openEvidenceButton = new QPushButton(QStringLiteral("Open Evidence"), this);
    m_openEvidenceButton->setObjectName(QStringLiteral("eventOpenEvidenceButton"));
    m_openEvidenceButton->setEnabled(false);
    m_openEvidenceButton->setStyleSheet(QStringLiteral(
        "QPushButton { background:#263238;color:white;border:1px solid #455a64;"
        "border-radius:5px;padding:6px 12px;font-weight:800; }"
        "QPushButton:hover { background:#37474f;border-color:#fb8c00; }"
        "QPushButton:disabled { background:#eceff1;color:#90a4ae;border-color:#cfd8dc; }"));
    buttonLayout->addWidget(m_openEvidenceButton);
    layout->addLayout(buttonLayout);

    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &EventsPage::applyFilters);
    connect(m_zoneFilter, &QComboBox::currentIndexChanged,
            this, &EventsPage::applyFilters);
    connect(m_eventTypeFilter, &QComboBox::currentIndexChanged,
            this, &EventsPage::applyFilters);
    connect(m_statusFilter, &QComboBox::currentIndexChanged,
            this, &EventsPage::applyFilters);
    connect(m_resetFilterButton, &QPushButton::clicked,
            this, &EventsPage::resetFilters);
    connect(exportButton, &QPushButton::clicked, this, &EventsPage::exportCsv);
    connect(m_eventTable, &QTableWidget::itemSelectionChanged,
            this, &EventsPage::updateEvidenceAction);
    connect(m_openEvidenceButton, &QPushButton::clicked, this, [this]() {
        requestEvidenceForRow(m_eventTable->currentRow());
    });
    connect(m_eventTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) { requestEvidenceForRow(row); });
    updateStatePresentation();
}

void EventsPage::appendEvent(const MonitoringEvent &event)
{
    const bool sortingEnabled = m_eventTable->isSortingEnabled();
    m_eventTable->setSortingEnabled(false);
    const int row = m_eventTable->rowCount();
    m_eventTable->insertRow(row);
    QTableWidgetItem *eventAnchor = nullptr;
    const QStringList values = {
        monitoringEventTimeText(event), event.sourceId, event.eventType,
        event.message, monitoringEventStatusText(event)};
    for (int column = 0; column < values.size(); ++column) {
        auto *item = new QTableWidgetItem(values.at(column));
        item->setData(Qt::UserRole, event.id);
        item->setData(Qt::UserRole + 1, event.evidenceSlotId);
        if (column == 0) eventAnchor = item;
        if (column == 4) {
            const QString status = values.at(column).trimmed().toUpper();
            const bool fireOpen = event.eventType.contains(
                QStringLiteral("FIRE"), Qt::CaseInsensitive)
                && status == QStringLiteral("OPEN");
            const QString background = fireOpen ? QStringLiteral("#ffebee")
                : status == QStringLiteral("OPEN") ? QStringLiteral("#fff3e0")
                : status == QStringLiteral("ACKED") ? QStringLiteral("#e3f2fd")
                : status == QStringLiteral("CLEARED") || status == QStringLiteral("DONE")
                    ? QStringLiteral("#e8f5e9") : QStringLiteral("#eceff1");
            const QString foreground = fireOpen ? QStringLiteral("#b71c1c")
                : status == QStringLiteral("OPEN") ? QStringLiteral("#e65100")
                : status == QStringLiteral("ACKED") ? QStringLiteral("#0d47a1")
                : status == QStringLiteral("CLEARED") || status == QStringLiteral("DONE")
                    ? QStringLiteral("#1b5e20") : QStringLiteral("#455a64");
            item->setBackground(QColor(background));
            item->setForeground(QColor(foreground));
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
            item->setTextAlignment(Qt::AlignCenter);
        }
        m_eventTable->setItem(row, column, item);
    }
    m_eventTable->setSortingEnabled(sortingEnabled);
    addFilterOption(m_zoneFilter, event.sourceId);
    addFilterOption(m_eventTypeFilter, event.eventType);
    addFilterOption(m_statusFilter, values.at(4));
    applyFilters();
    if (eventAnchor && !m_eventTable->isRowHidden(eventAnchor->row())) {
        m_eventTable->scrollToItem(eventAnchor);
    }
    updateStatePresentation();
}

void EventsPage::setServerState(const QString &status, const QString &detail)
{
    m_serverStatus = status.trimmed().toUpper();
    m_serverDetail = detail.trimmed();
    updateStatePresentation();
}

void EventsPage::applyFilters()
{
    const QString searchText = m_searchEdit->text().trimmed();
    const QString zone = m_zoneFilter->currentData().toString();
    const QString eventType = m_eventTypeFilter->currentData().toString();
    const QString status = m_statusFilter->currentData().toString();
    int visibleCount = 0;

    for (int row = 0; row < m_eventTable->rowCount(); ++row) {
        const auto columnText = [this, row](int column) {
            const QTableWidgetItem *item = m_eventTable->item(row, column);
            return item ? item->text().trimmed() : QString();
        };

        bool matches = zone.isEmpty()
            || columnText(1).compare(zone, Qt::CaseInsensitive) == 0;
        matches = matches && (eventType.isEmpty()
            || columnText(2).compare(eventType, Qt::CaseInsensitive) == 0);
        matches = matches && (status.isEmpty()
            || columnText(4).compare(status, Qt::CaseInsensitive) == 0);

        if (matches && !searchText.isEmpty()) {
            matches = false;
            for (int column = 0; column < m_eventTable->columnCount(); ++column) {
                if (columnText(column).contains(searchText, Qt::CaseInsensitive)) {
                    matches = true;
                    break;
                }
            }
        }

        m_eventTable->setRowHidden(row, !matches);
        if (matches) ++visibleCount;
    }

    m_filterResultLabel->setText(
        QStringLiteral("Showing %1 of %2 events")
            .arg(visibleCount)
            .arg(m_eventTable->rowCount()));
    m_resetFilterButton->setEnabled(
        !searchText.isEmpty() || !zone.isEmpty()
        || !eventType.isEmpty() || !status.isEmpty());
    updateEvidenceAction();
}

void EventsPage::resetFilters()
{
    const QSignalBlocker searchBlocker(m_searchEdit);
    const QSignalBlocker zoneBlocker(m_zoneFilter);
    const QSignalBlocker eventBlocker(m_eventTypeFilter);
    const QSignalBlocker statusBlocker(m_statusFilter);
    m_searchEdit->clear();
    m_zoneFilter->setCurrentIndex(0);
    m_eventTypeFilter->setCurrentIndex(0);
    m_statusFilter->setCurrentIndex(0);
    applyFilters();
}

void EventsPage::addFilterOption(QComboBox *comboBox, const QString &value)
{
    const QString displayValue = value.trimmed();
    if (displayValue.isEmpty()) return;

    for (int index = 1; index < comboBox->count(); ++index) {
        if (comboBox->itemData(index).toString().compare(
                displayValue, Qt::CaseInsensitive) == 0) {
            return;
        }
    }
    comboBox->addItem(displayValue, displayValue);
}

void EventsPage::requestEvidenceForRow(int row)
{
    const QTableWidgetItem *eventItem = row >= 0 ? m_eventTable->item(row, 0) : nullptr;
    const QString eventId = eventItem
        ? eventItem->data(Qt::UserRole).toString().trimmed() : QString();
    const QString evidenceSlotId = eventItem
        ? eventItem->data(Qt::UserRole + 1).toString().trimmed() : QString();
    if (!eventId.isEmpty() && !evidenceSlotId.isEmpty()) {
        emit eventEvidenceRequested(eventId);
    }
}

void EventsPage::updateEvidenceAction()
{
    if (!m_eventTable || !m_openEvidenceButton || !m_selectionLabel) return;
    const int row = m_eventTable->currentRow();
    const QTableWidgetItem *eventItem = row >= 0 ? m_eventTable->item(row, 0) : nullptr;
    const QString eventId = eventItem
        ? eventItem->data(Qt::UserRole).toString().trimmed() : QString();
    const QString evidenceSlotId = eventItem
        ? eventItem->data(Qt::UserRole + 1).toString().trimmed() : QString();
    const bool rowVisible = row >= 0 && !m_eventTable->isRowHidden(row);
    const bool canOpen = rowVisible && !eventId.isEmpty() && !evidenceSlotId.isEmpty();
    m_openEvidenceButton->setEnabled(canOpen);
    if (row < 0) {
        m_selectionLabel->setText(
            QStringLiteral("Select a parking event to review its evidence."));
    } else if (!rowVisible) {
        m_selectionLabel->setText(
            QStringLiteral("The selected event is hidden by the current filters."));
    } else if (canOpen) {
        m_selectionLabel->setText(
            QStringLiteral("%1 selected · Evidence is available for %2")
                .arg(eventId, evidenceSlotId));
    } else {
        m_selectionLabel->setText(
            QStringLiteral("%1 selected · This event has no parking-slot evidence.")
                .arg(eventId.isEmpty() ? QStringLiteral("Event") : eventId));
    }
}

void EventsPage::updateStatePresentation()
{
    if (!m_stateLabel || !m_eventTable) return;
    const bool hasEvents = m_eventTable->rowCount() > 0;
    const bool connecting = m_serverStatus == QStringLiteral("CONNECTING")
        || m_serverStatus == QStringLiteral("RETRYING");
    const bool unavailable = m_serverStatus == QStringLiteral("ERROR")
        || m_serverStatus == QStringLiteral("DISCONNECTED");

    if (unavailable) {
        m_stateLabel->setText(hasEvents
            ? QStringLiteral("Server/API unavailable · Existing events remain available%1")
                  .arg(m_serverDetail.isEmpty() ? QString()
                                               : QStringLiteral(" · ") + m_serverDetail)
            : QStringLiteral("Server/API unavailable · No monitoring events to display%1")
                  .arg(m_serverDetail.isEmpty() ? QString()
                                               : QStringLiteral(" · ") + m_serverDetail));
        m_stateLabel->setStyleSheet(QStringLiteral(
            "background:#ffebee;color:#b71c1c;border:1px solid #ef9a9a;"
            "border-radius:6px;padding:8px;font-weight:700;"));
        m_stateLabel->show();
    } else if (connecting && !hasEvents) {
        m_stateLabel->setText(QStringLiteral(
            "Connecting to the server · Waiting for monitoring events"));
        m_stateLabel->setStyleSheet(QStringLiteral(
            "background:#fff3e0;color:#e65100;border:1px solid #ffcc80;"
            "border-radius:6px;padding:8px;font-weight:700;"));
        m_stateLabel->show();
    } else if (!hasEvents) {
        m_stateLabel->setText(QStringLiteral(
            "No monitoring events received yet. New events will appear here."));
        m_stateLabel->setStyleSheet(QStringLiteral(
            "background:#f8fafb;color:#607d8b;border:1px solid #cfd8dc;"
            "border-radius:6px;padding:8px;font-weight:700;"));
        m_stateLabel->show();
    } else {
        m_stateLabel->hide();
    }
}

void EventsPage::exportCsv()
{
    const QString defaultName = QStringLiteral("parking_event_log_")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))
        + QStringLiteral(".csv");
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export Event Log CSV"), defaultName, QStringLiteral("CSV Files (*.csv)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit exportResult(false, QStringLiteral("Could not open CSV file: ") + path);
        return;
    }
    auto escape = [](QString value) {
        value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + value + QLatin1Char('"');
    };
    file.write("\xEF\xBB\xBF");
    QStringList headers;
    for (int column = 0; column < m_eventTable->columnCount(); ++column) {
        headers << escape(m_eventTable->horizontalHeaderItem(column)->text());
    }
    file.write(headers.join(QLatin1Char(',')).toUtf8());
    file.write("\n");
    for (int row = 0; row < m_eventTable->rowCount(); ++row) {
        QStringList values;
        for (int column = 0; column < m_eventTable->columnCount(); ++column) {
            const QTableWidgetItem *item = m_eventTable->item(row, column);
            values << escape(item ? item->text() : QString());
        }
        file.write(values.join(QLatin1Char(',')).toUtf8());
        file.write("\n");
    }
    emit exportResult(true, QStringLiteral("Event log CSV exported: ") + path);
}
