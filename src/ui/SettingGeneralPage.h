#pragma once
#include <QWidget>

class AppSettings;

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
    Ui::SettingGeneralPage *ui;
    AppSettings *m_settings;
};
