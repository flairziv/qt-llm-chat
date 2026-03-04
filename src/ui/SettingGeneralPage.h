#pragma once
#include <QWidget>

class AppSettings;
class ElaLineEdit;

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

    Ui::SettingGeneralPage *ui;
    AppSettings *m_settings;
    ElaLineEdit *m_pixmapPathEdit = nullptr;
    ElaLineEdit *m_moviePathEdit = nullptr;
};
