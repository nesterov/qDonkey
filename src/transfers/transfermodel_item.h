#ifndef TRANSFERMODEL_ITEM_H
#define TRANSFERMODEL_ITEM_H

#include <QObject>
#include <QDateTime>
#include <QIcon>
#include <QColor>

#include "qtlibed2k/qed2khandle.h"
#include "qtlibed2k/qed2ksession.h"

class TransferModelItem : public QObject {
    Q_OBJECT
public:
    enum {
         FilterRole = Qt::UserRole + 2
    };

    enum State {
        STATE_DOWNLOADING,
        STATE_STALLED_DL,
        STATE_STALLED_UP,
        STATE_SEEDING,
        STATE_PAUSED_UP,
        STATE_PAUSED_DL,
        STATE_CHECKING,
        STATE_INVALID
    };

    enum Column {
        TM_NAME,
        TM_SIZE,
        TM_TYPE,
        TM_PROGRESS,
        TM_STATUS,
        TM_SEEDS,
        TM_PEERS,
        TM_DLSPEED,
        TM_UPSPEED,
        TM_HASH,
        TM_ETA,
        TM_RATIO,
        TM_ADD_DATE,
        TM_AMOUNT_DOWNLOADED,
        TM_AMOUNT_LEFT,
        TM_TIME_ELAPSED,
        TM_END
    };

public:
    TransferModelItem(const QED2KHandle& h, const QString& status);
    inline int columnCount() const { return TM_END; }
    QVariant data(int column, int role = Qt::DisplayRole) const;
    bool setData(int column, const QVariant &value, int role = Qt::DisplayRole);
    inline QString hash() const { return m_hash; }
    QED2KHandle handle() const { return m_handle; }
    void setHandle(QED2KHandle h) { m_handle = h; }

private:
    State state() const;
    const QString m_status;
private:
    QED2KHandle m_handle;
    QDateTime m_addedTime;
    QDateTime m_seedTime;
    QString m_hash;
    FileType m_ft;
};

#endif
