#pragma once
#include <QWidget>

class AppSettings;

namespace Ui { class SettingOpenAIPage; }

class SettingOpenAIPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingOpenAIPage(AppSettings *settings, QWidget *parent = nullptr);
    ~SettingOpenAIPage();

    void loadSettings();

private:
    void connectAutoSave();
    Ui::SettingOpenAIPage *ui;
    AppSettings *m_settings;
};
