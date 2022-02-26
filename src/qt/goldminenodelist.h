#ifndef GOLDMINENODELIST_H
#define GOLDMINENODELIST_H

#include "primitives/transaction.h"
#include "platformstyle.h"
#include "sync.h"
#include "util.h"

#include <QMenu>
#include <QTimer>
#include <QWidget>

#define MY_GOLDMINENODELIST_UPDATE_SECONDS                 60
#define GOLDMINENODELIST_UPDATE_SECONDS                    15
#define GOLDMINENODELIST_FILTER_COOLDOWN_SECONDS            3

namespace Ui {
    class GoldminenodeList;
}

class ClientModel;
class WalletModel;

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Goldminenode Manager page widget */
class GoldminenodeList : public QWidget
{
    Q_OBJECT

public:
    explicit GoldminenodeList(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~GoldminenodeList();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);
    void ShowQRCode(std::string strAlias);
    void StartAlias(std::string strAlias);
    void StartAll(std::string strCommand = "start-all");

private:
    QMenu *contextMenu;
    int64_t nTimeFilterUpdated;
    bool fFilterUpdated;

public Q_SLOTS:
    void updateMyGoldminenodeInfo(QString strAlias, QString strAddr, const COutPoint& outpoint);
    void updateMyNodeList(bool fForce = false);
    void updateNodeList();

Q_SIGNALS:
    void doubleClicked(const QModelIndex&);

private:
    QTimer *timer;
    Ui::GoldminenodeList *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;

    // Protects tableWidgetGoldminenodes
    CCriticalSection cs_mnlist;

    // Protects tableWidgetMyGoldminenodes
    CCriticalSection cs_mymnlist;

    QString strCurrentFilter;

private Q_SLOTS:
    void showContextMenu(const QPoint &);
    void on_filterLineEdit_textChanged(const QString &strFilterIn);
    void on_QRButton_clicked();
    void on_startButton_clicked();
    void on_startAllButton_clicked();
    void on_startMissingButton_clicked();
    void on_tableWidgetMyGoldminenodes_itemSelectionChanged();
    void on_UpdateButton_clicked();
};
#endif // GOLDMINENODELIST_H
