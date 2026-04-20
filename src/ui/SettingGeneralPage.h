#pragma once
#include <QWidget>

class AppSettings;
class ElaLineEdit;
class ElaToggleSwitch;
class ElaComboBox;

namespace Ui { class SettingGeneralPage; }

class SettingGeneralPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingGeneralPage(AppSettings *settings, QWidget *parent = nullptr);
    ~SettingGeneralPage();

    void loadSettings();

signals:
    void settingsChanged();

private:
    void connectAutoSave();
    void setupBackgroundSection();
    void setupTTSSection();
    void setupRoleNameSection();

    Ui::SettingGeneralPage *ui;
    AppSettings *m_settings;
    ElaLineEdit *m_pixmapPathEdit = nullptr;
    ElaLineEdit *m_moviePathEdit = nullptr;
    ElaToggleSwitch *m_ttsEnabledSwitch = nullptr;
    ElaComboBox *m_ttsVoiceCombo = nullptr;
    ElaLineEdit *m_userNameEdit = nullptr;
    ElaLineEdit *m_assistantNameEdit = nullptr;
};
