#pragma once

#include <QWidget>

class AppSettings;

namespace Ui { class SettingGeminiPage; }

class SettingGeminiPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingGeminiPage(AppSettings *settings, QWidget *parent = nullptr);
    ~SettingGeminiPage();

    void loadSettings();

private:
    void connectAutoSave();

    Ui::SettingGeminiPage *ui;
    AppSettings *m_settings;
};
