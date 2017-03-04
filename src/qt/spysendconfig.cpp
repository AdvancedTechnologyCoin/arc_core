#include "spysendconfig.h"
#include "ui_spysendconfig.h"

#include "bitcoinunits.h"
#include "spysend.h"
#include "guiconstants.h"
#include "optionsmodel.h"
#include "walletmodel.h"

#include <QMessageBox>
#include <QPushButton>
#include <QKeyEvent>
#include <QSettings>

SpySendConfig::SpySendConfig(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SpySendConfig),
    model(0)
{
    ui->setupUi(this);

    connect(ui->buttonBasic, SIGNAL(clicked()), this, SLOT(clickBasic()));
    connect(ui->buttonHigh, SIGNAL(clicked()), this, SLOT(clickHigh()));
    connect(ui->buttonMax, SIGNAL(clicked()), this, SLOT(clickMax()));
}

SpySendConfig::~SpySendConfig()
{
    delete ui;
}

void SpySendConfig::setModel(WalletModel *model)
{
    this->model = model;
}

void SpySendConfig::clickBasic()
{
    configure(true, 1000, 2);

    QString strAmount(BitcoinUnits::formatWithUnit(
        model->getOptionsModel()->getDisplayUnit(), 1000 * COIN));
    QMessageBox::information(this, tr("SpySend Configuration"),
        tr(
            "SpySend was successfully set to basic (%1 and 2 rounds). You can change this at any time by opening Arctic's configuration screen."
        ).arg(strAmount)
    );

    close();
}

void SpySendConfig::clickHigh()
{
    configure(true, 1000, 8);

    QString strAmount(BitcoinUnits::formatWithUnit(
        model->getOptionsModel()->getDisplayUnit(), 1000 * COIN));
    QMessageBox::information(this, tr("SpySend Configuration"),
        tr(
            "SpySend was successfully set to high (%1 and 8 rounds). You can change this at any time by opening Arctic's configuration screen."
        ).arg(strAmount)
    );

    close();
}

void SpySendConfig::clickMax()
{
    configure(true, 1000, 16);

    QString strAmount(BitcoinUnits::formatWithUnit(
        model->getOptionsModel()->getDisplayUnit(), 1000 * COIN));
    QMessageBox::information(this, tr("SpySend Configuration"),
        tr(
            "SpySend was successfully set to maximum (%1 and 16 rounds). You can change this at any time by opening Arctic's configuration screen."
        ).arg(strAmount)
    );

    close();
}

void SpySendConfig::configure(bool enabled, int coins, int rounds) {

    QSettings settings;

    settings.setValue("nSpySendRounds", rounds);
    settings.setValue("nSpySendAmount", coins);

    nSpySendRounds = rounds;
    nSpySendAmount = coins;
}
