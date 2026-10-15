#pragma once
#include "profile_editor.h"
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QWidget>
#include <array>
class QFormLayout;
class QProcess;
class QLabel;
class QPushButton;
class QTemporaryDir;

class EditQWDTT : public QWidget, public ProfileEditor {
    Q_OBJECT
  public:
    explicit EditQWDTT(QWidget *parent = nullptr);
    ~EditQWDTT() override;
    void onStart(std::shared_ptr<Configs::Profile> ent) override;
    bool onEnd() override;

  private:
    std::shared_ptr<Configs::Profile> ent;
    QComboBox *mode;
    QLineEdit *password;
    QComboBox *hashSource;
    QSpinBox *autoHashCount;
    QFormLayout *form;
    std::array<QLineEdit *, 4> hashes;
    std::array<QWidget *, 4> hashRows;
    std::array<QLabel *, 4> hashStatus;
    QPushButton *checkHashes;
    QProcess *hashProcess = nullptr;
    std::unique_ptr<QTemporaryDir> hashCheckDirectory;
    QByteArray hashCheckOutput;
    QString hashCheckFailure;
    QList<int> checkingSlots;
    bool hashCheckStopped = false;
    void stopHashCheck();
    QSpinBox *rawPort;
    QComboBox *workers;
    void updateWorkers(int requested = -1);
    QComboBox *transport;
    QComboBox *obfs;
    QComboBox *authorization;
    QComboBox *captcha;
    QComboBox *dns;
    QLineEdit *device;
};
