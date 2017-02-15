#ifndef SPYSENDCONFIG_H
#define SPYSENDCONFIG_H

#include <QDialog>

namespace Ui {
    class SpysendConfig;
}
class WalletModel;

/** Multifunctional dialog to ask for passphrases. Used for encryption, unlocking, and changing the passphrase.
 */
class SpysendConfig : public QDialog
{
    Q_OBJECT

public:

    SpysendConfig(QWidget *parent = 0);
    ~SpysendConfig();

    void setModel(WalletModel *model);


private:
    Ui::SpysendConfig *ui;
    WalletModel *model;
    void configure(bool enabled, int coins, int rounds);

private Q_SLOTS:

    void clickBasic();
    void clickHigh();
    void clickMax();
};

#endif // SPYSENDCONFIG_H
