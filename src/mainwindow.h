/*
 * Bittorrent Client using Qt4 and libtorrent.
 * Copyright (C) 2006  Christophe Dumez
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 *
 * Contact : chris@qbittorrent.org
 */

#ifndef GUI_H
#define GUI_H

#include <QProcess>
#include <QSystemTrayIcon>
#include <QPointer>
#include <QSplashScreen>
#include <QMovie>

#include "qtlibed2k/qed2ksession.h"
#include "ui_mainwindow.h"

class downloadFromURL;
class options_imp;
class TransferListWidget;
class TransferListFiltersWidget;
class status_bar;
class consoleDlg;
class downloadFromURL;
class HidableTabWidget;
class LineEdit;
class ExecutionLog;
class PowerManagement;
class search_widget;
class messages_widget;
class AboutDlg;
class TestWgt;
class QuickSearchModel;
class QSortFilterProxyModel;
class PeerListWidget;
class TransferListWidget;


QT_BEGIN_NAMESPACE
class QCloseEvent;
class QFileSystemWatcher;
class QShortcut;
class QSplitter;
class QTabWidget;
class QTimer;
class QVBoxLayout;
QT_END_NAMESPACE

class MainWindow : public QMainWindow, private Ui::MainWindow
{
    Q_OBJECT
    friend class callback_wrapper;

public:
    enum Widgets {

        wTransfer = 0,
        wSearch,
        wPreferences
    };

    // Construct / Destruct
    MainWindow(QWidget *parent = 0, QStringList torrentCmdLine = QStringList());
    ~MainWindow();

    QMenu* getTrayIconMenu();

public slots:
    void showNotificationBaloon(QString title, QString msg) const;
    void downloadFromURLList(const QStringList& urls);
    void deleteSession();
    void addConsoleMessage(const QString& msg, QColor color = QApplication::palette().color(QPalette::WindowText)) const;

protected slots:
    void on_actionTransfers_triggered();
    void on_actionSearch_triggered();
    void on_actionConnect_triggered();
    void on_actionOptions_triggered();

    // processing server events
    void handleServerNameResolved(QString);
    void handleServerConnectionInitialized(quint32,quint32,quint32);
    void handleServerConnectionClosed(QString);
    void handleServerStatus(int,int);
    void handleServerMessage(QString);
    void handleServerIdentity(QString,QString);


    void toggleVisibility(QSystemTrayIcon::ActivationReason e = QSystemTrayIcon::Trigger);
    void balloonClicked();
    void writeSettings();
    void readSettings();
    void on_actionExit_triggered();
    void createTrayIcon();
    void fileError(const QED2KHandle& h, QString msg);
    void createSystrayDelayed();
    void minimizeWindow();

    // Keyboard shortcuts
    void createKeyboardShortcuts();
    void displayTransferTab() const;

    // Torrent actions
    void updateGUI();
    void loadPreferences(bool configure_session = true);
    void processParams(const QString& params);
    void processParams(const QStringList& params);
    void addTorrent(QString path);
    void addedTransfer(const QED2KHandle& h) const;
    void finishedTransfer(const QED2KHandle& h) const;

    // Options slots
    void optionsSaved();
protected:
    void closeEvent(QCloseEvent *);
    void showEvent(QShowEvent *);
    bool event(QEvent * event);

#ifdef Q_WS_WIN
    bool winEvent(MSG * message, long * result);
#endif

private:
    QIcon getSystrayIcon() const;

private:
    bool eventFilter(QObject *watched, QEvent *event);
    QFileSystemWatcher *executable_watcher;

    // GUI related
    bool m_posInitialized;
    QTimer *guiUpdater;
    //HidableTabWidget *tabs;
    status_bar* statusBar;
    QPointer<options_imp> options;
    QPointer<consoleDlg> console;
    QPointer<QSystemTrayIcon> systrayIcon;
    QPointer<QTimer> systrayCreator;
    QPointer<QMenu> myTrayIconMenu;

    bool displaySpeedInTitle;
    bool force_exit;

    // Keyboard shortcuts
    QShortcut *switchTransferShortcut;
    QShortcut *hideShortcut;

    // Execution Log
    QPointer<ExecutionLog> m_executionLog;

    // Power Management
    PowerManagement *m_pwr;
    QTimer *preventTimer;
    unsigned int m_nTaskbarButtonCreated;

    QIcon icon_disconnected;
    QIcon icon_connected;
    QIcon icon_connecting;

    QIcon icon_TrayConn;
    QIcon icon_TrayDisconn;
    QIcon& icon_CurTray;

    QDateTime m_last_file_error;

};

#endif
