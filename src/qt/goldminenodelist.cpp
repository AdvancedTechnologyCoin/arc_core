#include "goldminenodelist.h"
#include "ui_goldminenodelist.h"

#include "activegoldminenode.h"
#include "clientmodel.h"
#include "clientversion.h"
#include "init.h"
#include "guiutil.h"
#include "goldminenode-sync.h"
#include "goldminenodeconfig.h"
#include "goldminenodeman.h"
#include "qrdialog.h"
#include "sync.h"
#include "wallet/wallet.h"
#include "walletmodel.h"

#include <QTimer>
#include <QMessageBox>

int GetOffsetFromUtc()
{
#if QT_VERSION < 0x050200
    const QDateTime dateTime1 = QDateTime::currentDateTime();
    const QDateTime dateTime2 = QDateTime(dateTime1.date(), dateTime1.time(), Qt::UTC);
    return dateTime1.secsTo(dateTime2);
#else
    return QDateTime::currentDateTime().offsetFromUtc();
#endif
}

GoldminenodeList::GoldminenodeList(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::GoldminenodeList),
    clientModel(0),
    walletModel(0)
{
    ui->setupUi(this);

    ui->startButton->setEnabled(false);

    int columnAliasWidth = 100;
    int columnAddressWidth = 200;
    int columnProtocolWidth = 60;
    int columnStatusWidth = 80;
    int columnActiveWidth = 130;
    int columnLastSeenWidth = 130;

    ui->tableWidgetMyGoldminenodes->setColumnWidth(0, columnAliasWidth);
    ui->tableWidgetMyGoldminenodes->setColumnWidth(1, columnAddressWidth);
    ui->tableWidgetMyGoldminenodes->setColumnWidth(2, columnProtocolWidth);
    ui->tableWidgetMyGoldminenodes->setColumnWidth(3, columnStatusWidth);
    ui->tableWidgetMyGoldminenodes->setColumnWidth(4, columnActiveWidth);
    ui->tableWidgetMyGoldminenodes->setColumnWidth(5, columnLastSeenWidth);

    ui->tableWidgetGoldminenodes->setColumnWidth(0, columnAddressWidth);
    ui->tableWidgetGoldminenodes->setColumnWidth(1, columnProtocolWidth);
    ui->tableWidgetGoldminenodes->setColumnWidth(2, columnStatusWidth);
    ui->tableWidgetGoldminenodes->setColumnWidth(3, columnActiveWidth);
    ui->tableWidgetGoldminenodes->setColumnWidth(4, columnLastSeenWidth);

    ui->tableWidgetMyGoldminenodes->setContextMenuPolicy(Qt::CustomContextMenu);

    QAction *startAliasAction = new QAction(tr("Start alias"), this);
    contextMenu = new QMenu();
    contextMenu->addAction(startAliasAction);
    connect(ui->tableWidgetMyGoldminenodes, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
    connect(ui->tableWidgetMyGoldminenodes, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(on_QRButton_clicked()));
    connect(startAliasAction, SIGNAL(triggered()), this, SLOT(on_startButton_clicked()));

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateNodeList()));
    connect(timer, SIGNAL(timeout()), this, SLOT(updateMyNodeList()));
    timer->start(1000);

    fFilterUpdated = false;
    nTimeFilterUpdated = GetTime();
    updateNodeList();
}

GoldminenodeList::~GoldminenodeList()
{
    delete ui;
}

void GoldminenodeList::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model) {
        // try to update list when goldminenode count changes
        connect(clientModel, SIGNAL(strGoldminenodesChanged(QString)), this, SLOT(updateNodeList()));
    }
}

void GoldminenodeList::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
}

void GoldminenodeList::showContextMenu(const QPoint &point)
{
    QTableWidgetItem *item = ui->tableWidgetMyGoldminenodes->itemAt(point);
    if(item) contextMenu->exec(QCursor::pos());
}

void GoldminenodeList::StartAlias(std::string strAlias)
{
    std::string strStatusHtml;
    strStatusHtml += "<center>Alias: " + strAlias;

    for (const auto& mne : goldminenodeConfig.getEntries()) {
        if(mne.getAlias() == strAlias) {
            std::string strError;
            CGoldminenodeBroadcast mnb;

            bool fSuccess = CGoldminenodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

            int nDoS;
            if (fSuccess && !mnodeman.CheckMnbAndUpdateGoldminenodeList(NULL, mnb, nDoS, *g_connman)) {
                strError = "Failed to verify MNB";
                fSuccess = false;
            }

            if(fSuccess) {
                strStatusHtml += "<br>Successfully started goldminenode.";
                mnodeman.NotifyGoldminenodeUpdates(*g_connman);
            } else {
                strStatusHtml += "<br>Failed to start goldminenode.<br>Error: " + strError;
            }
            break;
        }
    }
    strStatusHtml += "</center>";

    QMessageBox msg;
    msg.setText(QString::fromStdString(strStatusHtml));
    msg.exec();

    updateMyNodeList(true);
}

void GoldminenodeList::StartAll(std::string strCommand)
{
    int nCountSuccessful = 0;
    int nCountFailed = 0;
    std::string strFailedHtml;

    for (const auto& mne : goldminenodeConfig.getEntries()) {
        std::string strError;
        CGoldminenodeBroadcast mnb;

        int32_t nOutputIndex = 0;
        if(!ParseInt32(mne.getOutputIndex(), &nOutputIndex)) {
            continue;
        }

        COutPoint outpoint = COutPoint(uint256S(mne.getTxHash()), nOutputIndex);

        if(strCommand == "start-missing" && mnodeman.Has(outpoint)) continue;

        bool fSuccess = CGoldminenodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

        int nDoS;
        if (fSuccess && !mnodeman.CheckMnbAndUpdateGoldminenodeList(NULL, mnb, nDoS, *g_connman)) {
            strError = "Failed to verify MNB";
            fSuccess = false;
        }

        if(fSuccess) {
            nCountSuccessful++;
            mnodeman.NotifyGoldminenodeUpdates(*g_connman);
        } else {
            nCountFailed++;
            strFailedHtml += "\nFailed to start " + mne.getAlias() + ". Error: " + strError;
        }
    }

    std::string returnObj;
    returnObj = strprintf("Successfully started %d goldminenodes, failed to start %d, total %d", nCountSuccessful, nCountFailed, nCountFailed + nCountSuccessful);
    if (nCountFailed > 0) {
        returnObj += strFailedHtml;
    }

    QMessageBox msg;
    msg.setText(QString::fromStdString(returnObj));
    msg.exec();

    updateMyNodeList(true);
}

void GoldminenodeList::updateMyGoldminenodeInfo(QString strAlias, QString strAddr, const COutPoint& outpoint)
{
    bool fOldRowFound = false;
    int nNewRow = 0;

    for(int i = 0; i < ui->tableWidgetMyGoldminenodes->rowCount(); i++) {
        if(ui->tableWidgetMyGoldminenodes->item(i, 0)->text() == strAlias) {
            fOldRowFound = true;
            nNewRow = i;
            break;
        }
    }

    if(nNewRow == 0 && !fOldRowFound) {
        nNewRow = ui->tableWidgetMyGoldminenodes->rowCount();
        ui->tableWidgetMyGoldminenodes->insertRow(nNewRow);
    }

    goldminenode_info_t infoMn;
    bool fFound = mnodeman.GetGoldminenodeInfo(outpoint, infoMn);

    QTableWidgetItem *aliasItem = new QTableWidgetItem(strAlias);
    QTableWidgetItem *addrItem = new QTableWidgetItem(fFound ? QString::fromStdString(infoMn.addr.ToString()) : strAddr);
    QTableWidgetItem *protocolItem = new QTableWidgetItem(QString::number(fFound ? infoMn.nProtocolVersion : -1));
    QTableWidgetItem *statusItem = new QTableWidgetItem(QString::fromStdString(fFound ? CGoldminenode::StateToString(infoMn.nActiveState) : "MISSING"));
    QTableWidgetItem *activeSecondsItem = new QTableWidgetItem(QString::fromStdString(DurationToDHMS(fFound ? (infoMn.nTimeLastPing - infoMn.sigTime) : 0)));
    QTableWidgetItem *lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M",
                                                                                                   fFound ? infoMn.nTimeLastPing + GetOffsetFromUtc() : 0)));
    QTableWidgetItem *pubkeyItem = new QTableWidgetItem(QString::fromStdString(fFound ? CBitcoinAddress(infoMn.pubKeyCollateralAddress.GetID()).ToString() : ""));

    ui->tableWidgetMyGoldminenodes->setItem(nNewRow, 0, aliasItem);
    ui->tableWidgetMyGoldminenodes->setItem(nNewRow, 1, addrItem);
    ui->tableWidgetMyGoldminenodes->setItem(nNewRow, 2, protocolItem);
    ui->tableWidgetMyGoldminenodes->setItem(nNewRow, 3, statusItem);
    ui->tableWidgetMyGoldminenodes->setItem(nNewRow, 4, activeSecondsItem);
    ui->tableWidgetMyGoldminenodes->setItem(nNewRow, 5, lastSeenItem);
    ui->tableWidgetMyGoldminenodes->setItem(nNewRow, 6, pubkeyItem);
}

void GoldminenodeList::updateMyNodeList(bool fForce)
{
    TRY_LOCK(cs_mymnlist, fLockAcquired);
    if(!fLockAcquired) {
        return;
    }
    static int64_t nTimeMyListUpdated = 0;

    // automatically update my goldminenode list only once in MY_GOLDMINENODELIST_UPDATE_SECONDS seconds,
    // this update still can be triggered manually at any time via button click
    int64_t nSecondsTillUpdate = nTimeMyListUpdated + MY_GOLDMINENODELIST_UPDATE_SECONDS - GetTime();
    ui->secondsLabel->setText(QString::number(nSecondsTillUpdate));

    if(nSecondsTillUpdate > 0 && !fForce) return;
    nTimeMyListUpdated = GetTime();

    // Find selected row
    QItemSelectionModel* selectionModel = ui->tableWidgetMyGoldminenodes->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();
    int nSelectedRow = selected.count() ? selected.at(0).row() : 0;

    ui->tableWidgetMyGoldminenodes->setSortingEnabled(false);
    for (const auto& mne : goldminenodeConfig.getEntries()) {
        int32_t nOutputIndex = 0;
        if(!ParseInt32(mne.getOutputIndex(), &nOutputIndex)) {
            continue;
        }

        updateMyGoldminenodeInfo(QString::fromStdString(mne.getAlias()), QString::fromStdString(mne.getIp()), COutPoint(uint256S(mne.getTxHash()), nOutputIndex));
    }
    ui->tableWidgetMyGoldminenodes->selectRow(nSelectedRow);
    ui->tableWidgetMyGoldminenodes->setSortingEnabled(true);

    // reset "timer"
    ui->secondsLabel->setText("0");
}

void GoldminenodeList::updateNodeList()
{
    TRY_LOCK(cs_mnlist, fLockAcquired);
    if(!fLockAcquired) {
        return;
    }

    static int64_t nTimeListUpdated = GetTime();

    // to prevent high cpu usage update only once in GOLDMINENODELIST_UPDATE_SECONDS seconds
    // or GOLDMINENODELIST_FILTER_COOLDOWN_SECONDS seconds after filter was last changed
    int64_t nSecondsToWait = fFilterUpdated
                            ? nTimeFilterUpdated - GetTime() + GOLDMINENODELIST_FILTER_COOLDOWN_SECONDS
                            : nTimeListUpdated - GetTime() + GOLDMINENODELIST_UPDATE_SECONDS;

    if(fFilterUpdated) ui->countLabel->setText(QString::fromStdString(strprintf("Please wait... %d", nSecondsToWait)));
    if(nSecondsToWait > 0) return;

    nTimeListUpdated = GetTime();
    fFilterUpdated = false;

    QString strToFilter;
    ui->countLabel->setText("Updating...");
    ui->tableWidgetGoldminenodes->setSortingEnabled(false);
    ui->tableWidgetGoldminenodes->clearContents();
    ui->tableWidgetGoldminenodes->setRowCount(0);
    std::map<COutPoint, CGoldminenode> mapGoldminenodes = mnodeman.GetFullGoldminenodeMap();
    int offsetFromUtc = GetOffsetFromUtc();

    for (const auto& mnpair : mapGoldminenodes)
    {
        CGoldminenode mn = mnpair.second;
        // populate list
        // Address, Protocol, Status, Active Seconds, Last Seen, Pub Key
        QTableWidgetItem *addressItem = new QTableWidgetItem(QString::fromStdString(mn.addr.ToString()));
        QTableWidgetItem *protocolItem = new QTableWidgetItem(QString::number(mn.nProtocolVersion));
        QTableWidgetItem *statusItem = new QTableWidgetItem(QString::fromStdString(mn.GetStatus()));
        QTableWidgetItem *activeSecondsItem = new QTableWidgetItem(QString::fromStdString(DurationToDHMS(mn.lastPing.sigTime - mn.sigTime)));
        QTableWidgetItem *lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M", mn.lastPing.sigTime + offsetFromUtc)));
        QTableWidgetItem *pubkeyItem = new QTableWidgetItem(QString::fromStdString(CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString()));

        if (strCurrentFilter != "")
        {
            strToFilter =   addressItem->text() + " " +
                            protocolItem->text() + " " +
                            statusItem->text() + " " +
                            activeSecondsItem->text() + " " +
                            lastSeenItem->text() + " " +
                            pubkeyItem->text();
            if (!strToFilter.contains(strCurrentFilter)) continue;
        }

        ui->tableWidgetGoldminenodes->insertRow(0);
        ui->tableWidgetGoldminenodes->setItem(0, 0, addressItem);
        ui->tableWidgetGoldminenodes->setItem(0, 1, protocolItem);
        ui->tableWidgetGoldminenodes->setItem(0, 2, statusItem);
        ui->tableWidgetGoldminenodes->setItem(0, 3, activeSecondsItem);
        ui->tableWidgetGoldminenodes->setItem(0, 4, lastSeenItem);
        ui->tableWidgetGoldminenodes->setItem(0, 5, pubkeyItem);
    }

    ui->countLabel->setText(QString::number(ui->tableWidgetGoldminenodes->rowCount()));
    ui->tableWidgetGoldminenodes->setSortingEnabled(true);
}

void GoldminenodeList::on_filterLineEdit_textChanged(const QString &strFilterIn)
{
    strCurrentFilter = strFilterIn;
    nTimeFilterUpdated = GetTime();
    fFilterUpdated = true;
    ui->countLabel->setText(QString::fromStdString(strprintf("Please wait... %d", GOLDMINENODELIST_FILTER_COOLDOWN_SECONDS)));
}

void GoldminenodeList::on_startButton_clicked()
{
    std::string strAlias;
    {
        LOCK(cs_mymnlist);
        // Find selected node alias
        QItemSelectionModel* selectionModel = ui->tableWidgetMyGoldminenodes->selectionModel();
        QModelIndexList selected = selectionModel->selectedRows();

        if(selected.count() == 0) return;

        QModelIndex index = selected.at(0);
        int nSelectedRow = index.row();
        strAlias = ui->tableWidgetMyGoldminenodes->item(nSelectedRow, 0)->text().toStdString();
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm goldminenode start"),
        tr("Are you sure you want to start goldminenode %1?").arg(QString::fromStdString(strAlias)),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if(retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if(encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForMixingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if(!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAlias(strAlias);
        return;
    }

    StartAlias(strAlias);
}

void GoldminenodeList::on_startAllButton_clicked()
{
    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm all goldminenodes start"),
        tr("Are you sure you want to start ALL goldminenodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if(retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if(encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForMixingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if(!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll();
        return;
    }

    StartAll();
}

void GoldminenodeList::on_startMissingButton_clicked()
{

    if(!goldminenodeSync.IsGoldminenodeListSynced()) {
        QMessageBox::critical(this, tr("Command is not available right now"),
            tr("You can't use this command until goldminenode list is synced"));
        return;
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this,
        tr("Confirm missing goldminenodes start"),
        tr("Are you sure you want to start MISSING goldminenodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if(retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if(encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForMixingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if(!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll("start-missing");
        return;
    }

    StartAll("start-missing");
}

void GoldminenodeList::on_tableWidgetMyGoldminenodes_itemSelectionChanged()
{
    if(ui->tableWidgetMyGoldminenodes->selectedItems().count() > 0) {
        ui->startButton->setEnabled(true);
    }
}

void GoldminenodeList::on_UpdateButton_clicked()
{
    updateMyNodeList(true);
}

void GoldminenodeList::on_QRButton_clicked()
{
    std::string strAlias;
    {
        LOCK(cs_mymnlist);
        // Find selected node alias
        QItemSelectionModel* selectionModel = ui->tableWidgetMyGoldminenodes->selectionModel();
        QModelIndexList selected = selectionModel->selectedRows();

        if(selected.count() == 0) return;

        QModelIndex index = selected.at(0);
        int nSelectedRow = index.row();
        strAlias = ui->tableWidgetMyGoldminenodes->item(nSelectedRow, 0)->text().toStdString();
    }

    ShowQRCode(strAlias);
}

void GoldminenodeList::ShowQRCode(std::string strAlias) {

    if(!walletModel || !walletModel->getOptionsModel())
        return;

    // Get private key for this alias
    std::string strMNPrivKey = "";
    std::string strCollateral = "";
    std::string strIP = "";
    CGoldminenode mn;
    bool fFound = false;

    for (const auto& mne : goldminenodeConfig.getEntries()) {
        if (strAlias != mne.getAlias()) {
            continue;
        }
        else {
            strMNPrivKey = mne.getPrivKey();
            strCollateral = mne.getTxHash() + "-" + mne.getOutputIndex();
            strIP = mne.getIp();
            fFound = mnodeman.Get(COutPoint(uint256S(mne.getTxHash()), atoi(mne.getOutputIndex())), mn);
            break;
        }
    }

    // Title of popup window
    QString strWindowtitle = tr("Additional information for Goldminenode %1").arg(QString::fromStdString(strAlias));

    // Title above QR-Code
    QString strQRCodeTitle = tr("Goldminenode Private Key");

    // Create dialog text as HTML
    QString strHTML = "<html><font face='verdana, arial, helvetica, sans-serif'>";
    strHTML += "<b>" + tr("Alias") +            ": </b>" + GUIUtil::HtmlEscape(strAlias) + "<br>";
    strHTML += "<b>" + tr("Private Key") +      ": </b>" + GUIUtil::HtmlEscape(strMNPrivKey) + "<br>";
    strHTML += "<b>" + tr("Collateral") +       ": </b>" + GUIUtil::HtmlEscape(strCollateral) + "<br>";
    strHTML += "<b>" + tr("IP") +               ": </b>" + GUIUtil::HtmlEscape(strIP) + "<br>";
    if (fFound) {
        strHTML += "<b>" + tr("Protocol") +     ": </b>" + QString::number(mn.nProtocolVersion) + "<br>";
        strHTML += "<b>" + tr("Version") +      ": </b>" + (mn.lastPing.nDaemonVersion > DEFAULT_DAEMON_VERSION ? GUIUtil::HtmlEscape(FormatVersion(mn.lastPing.nDaemonVersion)) : tr("Unknown")) + "<br>";
        strHTML += "<b>" + tr("Sentinel") +     ": </b>" + (mn.lastPing.nSentinelVersion > DEFAULT_SENTINEL_VERSION ? GUIUtil::HtmlEscape(SafeIntVersionToString(mn.lastPing.nSentinelVersion)) : tr("Unknown")) + "<br>";
        strHTML += "<b>" + tr("Status") +       ": </b>" + GUIUtil::HtmlEscape(CGoldminenode::StateToString(mn.nActiveState)) + "<br>";
        strHTML += "<b>" + tr("Payee") +        ": </b>" + GUIUtil::HtmlEscape(CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString()) + "<br>";
        strHTML += "<b>" + tr("Active") +       ": </b>" + GUIUtil::HtmlEscape(DurationToDHMS(mn.lastPing.sigTime - mn.sigTime)) + "<br>";
        strHTML += "<b>" + tr("Last Seen") +    ": </b>" + GUIUtil::HtmlEscape(DateTimeStrFormat("%Y-%m-%d %H:%M", mn.lastPing.sigTime + GetOffsetFromUtc())) + "<br>";
    }

    // Open QR dialog
    QRDialog *dialog = new QRDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModel(walletModel->getOptionsModel());
    dialog->setInfo(strWindowtitle, QString::fromStdString(strMNPrivKey), strHTML, strQRCodeTitle);
    dialog->show();
}
