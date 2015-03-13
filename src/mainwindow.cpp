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

#include <QtGlobal>
#if defined(Q_WS_X11) && defined(QT_DBUS_LIB)
#include <QDBusConnection>
#include "notifications.h"
#endif

#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QMessageBox>
#include <QTimer>
#include <QDesktopServices>
#include <QStatusBar>
#include <QClipboard>
#include <QCloseEvent>
#include <QShortcut>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QDockWidget>
#include <QKeyEvent>
#include <QInputDialog>
#include <QSortFilterProxyModel>
#include <QMenu>

#include <libed2k/log.hpp>

#include "mainwindow.h"
#include "misc.h"

#include "preferences.h"
#include "qinisettings.h"
#include "status_bar.h"
#include "geoipmanager.h"
#include "iconprovider.h"

#ifdef Q_WS_MAC
#include "qmacapplication.h"
void qt_mac_set_dock_menu(QMenu *menu);
#endif

#include "sessionapplication.h"
#include "powermanagement.h"
#include "res.h"

using namespace libtorrent;

#define TIME_TRAY_BALLOON 5000
#define PREVENT_SUSPEND_INTERVAL 60000

/*****************************************************
 *                                                   *
 *                       GUI                         *
 *                                                   *
 *****************************************************/

// Constructor
MainWindow::MainWindow(QWidget *parent, QStringList torrentCmdLine)
    :
      QMainWindow(parent),
      m_posInitialized(false),
      force_exit(false),
      icon_TrayConn(res::trayConnected()),
      icon_TrayDisconn(res::trayDisconnected()),
      icon_CurTray(icon_TrayDisconn)
{
    setupUi(this);
    QCoreApplication::instance()->installEventFilter(this);

    //QApplication::setOverrideCursor(Qt::WaitCursor);
    m_last_file_error = QDateTime::currentDateTime().addSecs(-1); // imagine last file error event was 1 seconds in past

#ifdef Q_WS_WIN
    m_nTaskbarButtonCreated = RegisterWindowMessage(L"TaskbarButtonCreated");
#else
    m_nTaskbarButtonCreated = 0;
#endif

    Preferences pref;
    pref.migrate();
    setWindowTitle(QString::fromUtf8(PRODUCT_NAME));
    displaySpeedInTitle = pref.speedInTitleBar();

    // Clean exit on log out
    connect(static_cast<SessionApplication*>(qApp), SIGNAL(sessionIsShuttingDown()), this, SLOT(deleteSession()));

    this->setWindowIcon(QIcon(res::favicon()));

    actionOpen->setIcon(QIcon(res::add()));
    actionDelete->setIcon(IconProvider::instance()->getIcon("list-remove"));
    actionExit->setIcon(IconProvider::instance()->getIcon("application-exit"));
    actionPause->setIcon(QIcon(res::pause()));
    actionPause_All->setIcon(QIcon(res::pause()));
    actionStart->setIcon(QIcon(res::play()));
    actionStart_All->setIcon(QIcon(res::play()));

    // subscribe actions to main window toolbuttons
    connectBtn->setDefaultAction(actionConnect);
    transfersBtn->setDefaultAction(actionTransfers);
    searchBtn->setDefaultAction(actionSearch);
    settingsBtn->setDefaultAction(actionOptions);

    //startTransfersBtn->setDefaultAction(actionStart_All);
    //pauseTransfersBtn->setDefaultAction(actionPause_All);

    QMenu *startAllMenu = new QMenu(this);
    startAllMenu->addAction(actionStart_All);
    actionStart->setMenu(startAllMenu);
    QMenu *pauseAllMenu = new QMenu(this);
    pauseAllMenu->addAction(actionPause_All);
    actionPause->setMenu(pauseAllMenu);

#ifdef Q_WS_MAC
    connect(static_cast<QMacApplication*>(qApp), SIGNAL(newFileOpenMacEvent(QString)), this, SLOT(processParams(QString)));
#endif

    //transferList = new TransferListWidget(transfersView, Session::instance());
    //transferList->getSourceModel()->populate();

    //connect(actionDelete, SIGNAL(triggered()), transferList, SLOT(deleteSelectedTorrents()));
    //connect(transferList, SIGNAL(sigOpenTorrent()), SLOT(on_actionOpen_triggered()));
    //peersList = new PeerListWidget(connectionsView);

    statusBar = new status_bar(this, QMainWindow::statusBar());

    fileMenu = new QMenu(this);
    fileMenu->setObjectName(QString::fromUtf8("fileMenu"));
    fileMenu->setTitle(tr("Files"));
    //connect(searchTable, SIGNAL(customContextMenuRequested(QPoint)), SLOT(handleQuickSearchMenu(QPoint)));

    fileDownload = new QAction(this);
    fileDownload->setObjectName(QString::fromUtf8("fileDownload"));
    fileDownload->setText(tr("Download"));
    fileDownload->setIcon(QIcon(res::download()));
    fileMenu->addAction(fileDownload);
    connect(fileDownload, SIGNAL(triggered()), this, SLOT(downloadFile()));

    //connect(peersList, SIGNAL(sendMessage(const QString&, const libed2k::net_identifier&)), this, SLOT(startChat(const QString&, const libed2k::net_identifier&)));
    //connect(peersList, SIGNAL(addFriend(const QString&, const libed2k::net_identifier&)), this, SLOT(addFriend(const QString&, const libed2k::net_identifier&)));
    //connect(statusBar, SIGNAL(stopMessageNotification()), this, SLOT(stopMessageFlickering()));


    allDownloadsBtn_2->setChecked(true);

    m_pwr = new PowerManagement(this);


    // Configure session according to options
    loadPreferences(false);

    // Start connection checking timer
    guiUpdater = new QTimer(this);
    connect(guiUpdater, SIGNAL(timeout()), this, SLOT(updateGUI()));
    guiUpdater->start(2000);

    // Accept drag 'n drops
    //setAcceptDrops(true);
    createKeyboardShortcuts();

#ifdef Q_WS_MAC
    setUnifiedTitleAndToolBarOnMac(true);
#endif

    // View settings



    // Auto shutdown actions
/*    QActionGroup * autoShutdownGroup = new QActionGroup(this);
    autoShutdownGroup->setExclusive(true);
    autoShutdownGroup->addAction(actionAutoShutdown_Disabled);
    autoShutdownGroup->addAction(actionAutoExit_mule);
    autoShutdownGroup->addAction(actionAutoShutdown_system);
    autoShutdownGroup->addAction(actionAutoSuspend_system);

#if !defined(Q_WS_X11) || defined(QT_DBUS_LIB)
    actionAutoShutdown_system->setChecked(pref.shutdownWhenDownloadsComplete());
    actionAutoSuspend_system->setChecked(pref.suspendWhenDownloadsComplete());
#else
    actionAutoShutdown_system->setDisabled(true);
    actionAutoSuspend_system->setDisabled(true);
#endif

    actionAutoExit_mule->setChecked(pref.shutdownqBTWhenDownloadsComplete());

    if (!autoShutdownGroup->checkedAction())
        actionAutoShutdown_Disabled->setChecked(true);
*/
    // Load Window state and sizes
    readSettings();

    if (!ui_locked)
    {
        if (pref.startMinimized() && systrayIcon)
            showMinimized();
        else
        {
            show();
            activateWindow();
            raise();
        }
    }

    // Start watching the executable for updates
    executable_watcher = new QFileSystemWatcher();
    connect(executable_watcher, SIGNAL(fileChanged(QString)), this, SLOT(notifyOfUpdate(QString)));
    executable_watcher->addPath(qApp->applicationFilePath());


    //connect(Session::instance(), SIGNAL(beginLoadSharedFileSystem()), this, SLOT(beginLoadSharedFileSystem()));
    //connect(Session::instance(), SIGNAL(endLoadSharedFileSystem()), this, SLOT(endLoadSharedFileSystem()));

    // Resume unfinished torrents
    Session::instance()->startUpTransfers();

    // Add torrent given on command line
    processParams(torrentCmdLine);

    qDebug("GUI has been built");

#ifdef Q_WS_MAC
    qt_mac_set_dock_menu(getTrayIconMenu());
#endif

    // Make sure the Window is visible if we don't have a tray icon
    if (!systrayIcon && isHidden())
    {
        show();
        activateWindow();
        raise();
    }

    ed2kConnectionClosed(QED2KServerFingerprint(), "");

    connection_state = csDisconnected;    


    //connect(servers, SIGNAL(sigConnectPending(QED2KServerFingerprint)),
    //        this, SLOT(ed2kConnectionPending(QED2KServerFingerprint)));
    //connect(Session::instance(), SIGNAL(serverConnectionInitialized(QED2KServerFingerprint, quint32, quint32, quint32)),
    //      this, SLOT(ed2kConnectionInitialized(QED2KServerFingerprint, quint32, quint32, quint32)));
    //connect(Session::instance(), SIGNAL(serverStatus(QED2KServerFingerprint, int, int)),
    //      this, SLOT(ed2kServerStatus(QED2KServerFingerprint, int, int)));
   // connect(Session::instance(), SIGNAL(serverConnectionClosed(QED2KServerFingerprint, QString)),
    //      this, SLOT(ed2kConnectionClosed(QED2KServerFingerprint, QString)));
/*
    connect(Session::instance(), SIGNAL(newConsoleMessage(const QString&)), servers, SLOT(addHtmlLogMessage(const QString&)));
    connect(Session::instance(), SIGNAL(serverNameResolved(QED2KServerFingerprint,QString)), servers, SLOT(handleServerNameResolved(QED2KServerFingerprint,QString)));
    connect(Session::instance(), SIGNAL(serverConnectionInitialized(QED2KServerFingerprint,quint32,quint32,quint32)), servers, SLOT(handleServerConnectionInitialized(QED2KServerFingerprint,quint32,quint32,quint32)));
    connect(Session::instance(), SIGNAL(serverConnectionClosed(QED2KServerFingerprint,QString)), servers, SLOT(handleServerConnectionClosed(QED2KServerFingerprint,QString)));
    connect(Session::instance(), SIGNAL(serverStatus(QED2KServerFingerprint,int,int)), servers, SLOT(handleServerStatus(QED2KServerFingerprint,int,int)));
    connect(Session::instance(), SIGNAL(serverMessage(QED2KServerFingerprint,QString)), servers, SLOT(handleServerMessage(QED2KServerFingerprint,QString)));
    connect(Session::instance(), SIGNAL(serverIdentity(QED2KServerFingerprint,QString,QString)), servers, SLOT(handleServerIdentity(QED2KServerFingerprint,QString,QString)));
*/
    //Tray actions.
    //connect(actionToggleVisibility, SIGNAL(triggered()), this, SLOT(toggleVisibility()));
    //connect(actionStart_All, SIGNAL(triggered()), Session::instance(), SLOT(resumeAllTransfers()));
    //connect(actionPause_All, SIGNAL(triggered()), Session::instance(), SLOT(pauseAllTransfers()));

    Session::instance()->start();
}

void MainWindow::deleteSession()
{
    guiUpdater->stop();
    Session::drop();
    m_pwr->setActivityState(false);
    // Save window size, columns size
    writeSettings();
    // Accept exit
    qApp->exit();

}

// Destructor
MainWindow::~MainWindow()
{
    qDebug("GUI destruction");
    hide();

#ifdef Q_WS_MAC
    // Workaround to avoid bug http://bugreports.qt.nokia.com/browse/QTBUG-7305
    setUnifiedTitleAndToolBarOnMac(false);
#endif

    // Delete other GUI objects
    if(executable_watcher)
        delete executable_watcher;

    delete statusBar;
    delete guiUpdater;


    if(systrayCreator)
        delete systrayCreator;

    if(systrayIcon)
        delete systrayIcon;

    if(myTrayIconMenu)
        delete myTrayIconMenu;

    // Keyboard shortcuts
    delete switchTransferShortcut;
    delete hideShortcut;

    IconProvider::drop();

    // Delete Session::instance() object
    m_pwr->setActivityState(false);
    qDebug() << "Saving session filesystem";
    //Session::instance()->dropDirectoryTransfers();
    //Session::instance()->saveFileSystem();
    qDebug("Deleting Session::instance()");
    Session::drop();
    qDebug("Exiting GUI destructor...");
}

void MainWindow::tab_changed(int new_tab)
{
    Q_UNUSED(new_tab);
}

void MainWindow::writeSettings()
{
    Preferences settings;
    settings.beginGroup(QString::fromUtf8("MainWindow"));
    settings.setValue("geometry", saveGeometry());
    settings.endGroup();
    settings.setMigrationStage(false);
}

void MainWindow::readSettings()
{
    Preferences settings;
    settings.beginGroup(QString::fromUtf8("MainWindow"));

    if(settings.contains("geometry"))
    {
        if(restoreGeometry(settings.value("geometry").toByteArray()))
            m_posInitialized = true;
    }

    const QByteArray splitterState = settings.value("vsplitterState").toByteArray();
    settings.endGroup();
}

void MainWindow::balloonClicked()
{
    if(isHidden())
    {
        show();

        if(isMinimized())
        {
            showNormal();
        }

        raise();
        activateWindow();
    }
}

// called when a transfer has started
void MainWindow::addedTransfer(const QED2KHandle& h) const
{
    //if(TorrentPersistentData::getAddedDate(h.hash()).secsTo(QDateTime::currentDateTime()) <= 1 && !h.is_seed())
    //    showNotificationBaloon(tr("Download starting"), tr("%1 has started downloading.", "e.g: xxx.avi has started downloading.").arg(h.name()));
}

// called when a transfer has finished
void MainWindow::finishedTransfer(const QED2KHandle& h) const
{
    //if(!TorrentPersistentData::isSeed(h.hash()))
    //    showNotificationBaloon(tr("Download completion"), tr("%1 has finished downloading.", "e.g: xxx.avi has finished downloading.").arg(h.name()));
}

// Notification when disk is full and other disk errors
void MainWindow::fileError(const QED2KHandle& h, QString msg)
{
    QDateTime cdt = QDateTime::currentDateTime();

    if(m_last_file_error.secsTo(cdt) > 1)
    {
        showNotificationBaloon(tr("I/O Error"), tr("An I/O error occured for %1.\nReason: %2").arg(h.name()).arg(msg));
    }

    m_last_file_error = cdt;
}

void MainWindow::createKeyboardShortcuts()
{
    actionOpen->setShortcut(QKeySequence(QString::fromUtf8("Ctrl+O")));
    actionExit->setShortcut(QKeySequence(QString::fromUtf8("Ctrl+Q")));
    switchTransferShortcut = new QShortcut(QKeySequence(tr("Alt+1", "shortcut to switch to first tab")), this);
    connect(switchTransferShortcut, SIGNAL(activated()), this, SLOT(displayTransferTab()));
    hideShortcut = new QShortcut(QKeySequence(QString::fromUtf8("Esc")), this);
    connect(hideShortcut, SIGNAL(activated()), this, SLOT(hide()));

#ifdef Q_WS_MAC
    actionDelete->setShortcut(QKeySequence("Ctrl+Backspace"));
#else
    actionDelete->setShortcut(QKeySequence(QString::fromUtf8("Del")));
#endif
    actionStart->setShortcut(QKeySequence(QString::fromUtf8("Ctrl+S")));
    actionStart_All->setShortcut(QKeySequence(QString::fromUtf8("Ctrl+Shift+S")));
    actionPause->setShortcut(QKeySequence(QString::fromUtf8("Ctrl+P")));
    actionPause_All->setShortcut(QKeySequence(QString::fromUtf8("Ctrl+Shift+P")));
#ifdef Q_WS_MAC
    actionMinimize->setShortcut(QKeySequence(QString::fromUtf8("Ctrl+M")));
    addAction(actionMinimize);
#endif
}

// Keyboard shortcuts slots
void MainWindow::displayTransferTab() const
{
//  tabs->setCurrentWidget(transferList);
}


void MainWindow::handleDownloadFromUrlFailure(QString url, QString reason) const
{
    // Display a message box
    showNotificationBaloon(tr("Url download error"), tr("Couldn't download file at url: %1, reason: %2.").arg(url).arg(reason));
}

// Necessary if we want to close the window
// in one time if "close to systray" is enabled
void MainWindow::on_actionExit_triggered()
{
    force_exit = true;
    close();
}

void MainWindow::setTabText(int, QString) const
{
//  tabs->setTabText(index, text);
}


// Toggle Main window visibility
void MainWindow::toggleVisibility(QSystemTrayIcon::ActivationReason e)
{
    if(e == QSystemTrayIcon::Trigger || e == QSystemTrayIcon::DoubleClick)
    {
        if(isHidden())
        {
            show();

            if(isMinimized())
            {
                if(isMaximized())
                {
                    showMaximized();
                }
                else
                {
                    showNormal();
                }
            }

            raise();
            activateWindow();
        }
        else
        {
            hide();
        }
    }

    //actionToggleVisibility->setText(isVisible()? tr("Hide") : tr("Show"));
}

void MainWindow::showEvent(QShowEvent *e)
{
    qDebug("** Show Event **");

//  if(getCurrentTabWidget() == transferList) {
//    properties->loadDynamicData();
    //}

    e->accept();

    // Make sure the window is initially centered
    if(!m_posInitialized)
    {
        move(misc::screenCenter(this));
        m_posInitialized = true;
    }
}

// Called when we close the program
void MainWindow::closeEvent(QCloseEvent *e)
{
    Preferences pref;
    const bool goToSystrayOnExit = pref.closeToTray();

    if(!force_exit && systrayIcon && goToSystrayOnExit && !this->isHidden())
    {
        hide();
        e->accept();
        return;
    }

    SessionStatus status = Session::instance()->getSessionStatus();

    // has active transfers or sessions speed > 0 (we have incoming peers)
    if (pref.confirmOnExit() && (Session::instance()->hasActiveTransfers() || (status.payload_download_rate > 0) || (status.payload_upload_rate > 0)))
    {
        if (e->spontaneous() || force_exit)
        {
            if(!isVisible())
                show();

            QMessageBox confirmBox(QMessageBox::Question, tr("Exiting eMule0.60"), tr("Some files are currently transferring.\nAre you sure you want to quit eMule0.60?"), QMessageBox::NoButton, this);

            QPushButton *noBtn = confirmBox.addButton(tr("No"), QMessageBox::NoRole);
            QPushButton *yesBtn = confirmBox.addButton(tr("Yes"), QMessageBox::YesRole);
            QPushButton *alwaysBtn = confirmBox.addButton(tr("Always"), QMessageBox::YesRole);
            confirmBox.setDefaultButton(yesBtn);
            confirmBox.exec();

            if(!confirmBox.clickedButton() || confirmBox.clickedButton() == noBtn)
            {
                // Cancel exit
                e->ignore();
                force_exit = false;
                return;
            }

            if(confirmBox.clickedButton() == alwaysBtn)
            {
                // Remember choice
                Preferences().setConfirmOnExit(false);
            }
        }
    }

    hide();

    if(systrayIcon)
    {
        // Hide tray icon
        systrayIcon->hide();
    }

    // Save window size, columns size
    writeSettings();

    // Accept exit
    e->accept();
    qApp->exit();
}

bool MainWindow::event(QEvent * e)
{
    switch(e->type())
    {
        case QEvent::WindowStateChange:
        {
            qDebug("Window change event");

            //Now check to see if the window is minimised
            if(isMinimized())
            {
                qDebug("minimisation");

                if(systrayIcon && Preferences().minimizeToTray())
                {
                    qDebug("Minimize to Tray enabled, hiding!");
                    e->accept();
                    QTimer::singleShot(0, this, SLOT(hide()));
                    return true;
                }
            }

            break;
        }
#ifdef Q_WS_MAC
            case QEvent::ToolBarChange:
            {
                qDebug("MAC: Received a toolbar change event!");
                bool ret = QMainWindow::event(e);

                qDebug("MAC: new toolbar visibility is %d", !actionTop_tool_bar->isChecked());
                actionTop_tool_bar->toggle();
                Preferences().setToolbarDisplayed(actionTop_tool_bar->isChecked());
                return ret;
            }
#endif
        default:
            break;
    }

    return QMainWindow::event(e);
}

#ifdef Q_WS_WIN
bool MainWindow::winEvent(MSG * message, long * result)
{
    if (message->message == m_nTaskbarButtonCreated)
    {
        qDebug() << "initialize task bar";
        m_tbar->initialize();
        m_tbar->setState(winId(), taskbar_iface::S_NOPROGRESS);
        return true;
    }

    return false;
}
#endif

// Action executed when a file is dropped
/*void MainWindow::dropEvent(QDropEvent *event)
{
    event->acceptProposedAction();
    QStringList files;

    if(event->mimeData()->hasUrls())
    {
        const QList<QUrl> urls = event->mimeData()->urls();

        foreach (const QUrl &url, urls)
        {
            if (!url.isEmpty())
            {
                if (url.scheme().compare("file", Qt::CaseInsensitive) == 0)
                files << url.toLocalFile();
                else
                files << url.toString();
            }
        }
    }
    else
    {
        files = event->mimeData()->text().split(QString::fromUtf8("\n"));
    }

    // Add file to download list
    Preferences pref;
    const bool useTorrentAdditionDialog = pref.useAdditionDialog();

    foreach (QString file, files)
    {
        qDebug("Dropped file %s on download list", qPrintable(file));

        if (misc::isUrl(file))
        {
            Session::instance()->downloadFromUrl(file); // TODO - check it for using only in torrent
            continue;
        }

        // Bitcomet or Magnet link
        if (file.startsWith("bc://bt/", Qt::CaseInsensitive))
        {
            qDebug("Converting bc link to magnet link");
            file = misc::bcLinkToMagnet(file);
        }

        if (file.startsWith("magnet:", Qt::CaseInsensitive))
        {
            if (useTorrentAdditionDialog)
            {
                torrentAdditionDialog *dialog = new torrentAdditionDialog(this);
                dialog->showLoadMagnetURI(file);
            }
            else
            {
                Session::instance()->addLink(file); // TODO - check it fir using only in torrent
            }

            continue;
        }

        // Local file
        if (useTorrentAdditionDialog)
        {
            torrentAdditionDialog *dialog = new torrentAdditionDialog(this);

            if (file.startsWith("file:", Qt::CaseInsensitive))
            file = QUrl(file).toLocalFile();

            dialog->showLoad(file);
        }
        else
        {
            Session::instance()->addLink(file); // TODO - possibly it is torrent only
        }
    }
}*/

// Decode if we accept drag 'n drop or not
/*void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    foreach (const QString &mime, event->mimeData()->formats())
    {
        qDebug("mimeData: %s", mime.toLocal8Bit().data());
    }

    if(event->mimeData()->hasFormat(QString::fromUtf8("text/plain")) ||
       event->mimeData()->hasFormat(QString::fromUtf8("text/uri-list")))
    {
        event->acceptProposedAction();
    }
}*/

/*****************************************************
 *                                                   *
 *                     Torrent                       *
 *                                                   *
 *****************************************************/

void MainWindow::addTorrents(const QStringList &pathsList)
{

}

// Display a dialog to allow user to add
// torrents to download list
void MainWindow::on_actionOpen_triggered()
{
    Preferences settings;
    // Open File Open Dialog
    // Note: it is possible to select more than one file
    const QStringList pathsList = QFileDialog::getOpenFileNames(0, tr("Open Torrent Files"), settings.value(QString::fromUtf8("MainWindowLastDir"), QDir::homePath()).toString(), tr("Torrent Files") + QString::fromUtf8(" (*.torrent)"));

    if(!pathsList.empty())
    {
        addTorrents(pathsList);

        // Save last dir to remember it
        QStringList top_dir = pathsList.at(0).split(QDir::separator());
        top_dir.removeLast();
        settings.setValue(QString::fromUtf8("MainWindowLastDir"), top_dir.join(QDir::separator()));
    }
}


void MainWindow::selectWidget(Widgets wNum)
{
    actionTransfers->setChecked(false);
    actionSearch->setChecked(false);

    switch (wNum)
    {
        case wTransfer:  {
            //stackedWidget->setCurrentWidget(transfersPage);
            //actionTransfers->setChecked(true);
            break;
        }
        case wSearch:  {
            //stackedWidget->setCurrentWidget(search);
            //actionSearch->setChecked(true);
            break;
        }
        default: {
            Q_ASSERT(false);
            break;
        }
    }
}

void MainWindow::selectTransfersPage(TransfersPage page)
{

    /*setButtons(false);

    if (page != TransfersPages::Torrents)
    {
        torrentsPanel->hide();
        actionTorrents->setChecked(false);
        actionTransfers->setChecked(true);
    }

    switch (page)
    {
        case TransfersPages::All:
            allDownloadsBtn->setChecked(true);
            allDownloadsBtn_2->setChecked(true);
            transferList->showAll();
            break;
        case TransfersPages::Downloading:
            downloadingBtn->setChecked(true);
            downloadingBtn_2->setChecked(true);
            transferList->showDownloading();
            break;
        case TransfersPages::Completed:
            completedBtn->setChecked(true);
            completedBtn_2->setChecked(true);
            transferList->showCompleted();
            break;
        case TransfersPages::Waiting:
            waitingBtn->setChecked(true);
            waitingBtn_2->setChecked(true);
            transferList->showWaiting();
            break;
        case TransfersPages::Torrents:
            torrentsBtn->setChecked(true);
            torrentsBtn_2->setChecked(true);
            transferList->showTorrents();
            torrentsPanel->show();
            break;
    }
    */
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type())
    {
        case QEvent::KeyRelease:
        {
            QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
            int mask = Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier;

            if(keyEvent->modifiers() == mask)
            {
            }

            break;
        }
        case QEvent::DragEnter:
        {


            QDragEnterEvent *dragEnterEvent = static_cast<QDragEnterEvent*>(event);
            const QMimeData* mimeData = dragEnterEvent->mimeData();

            // check for our needed mime type, here a file or a list of files
            if (!mimeData->hasUrls())
                break;

            QList<QUrl> urlList = mimeData->urls();

            for (int i = 0; i < urlList.size(); ++i)
            {
                qDebug() << urlList.at(i).toString();
                if (urlList.at(i).toString().endsWith(".torrent"))
                {
                    dragEnterEvent->acceptProposedAction();
                    return true;
                }
            }

            break;
        }
        case QEvent::Drop:
        {
            QDropEvent *dropEvent = static_cast<QDropEvent*>(event);
            const QMimeData* mimeData = dropEvent->mimeData();

            // check for our needed mime type, here a file or a list of files
            if (!mimeData->hasUrls())
                break;

            QStringList pathList;
            QList<QUrl> urlList = mimeData->urls();

            // extract the local paths of the files
            for (int i = 0; i < urlList.size(); ++i)
            {
                pathList.append(urlList.at(i).toLocalFile());
            }

            addTorrents(pathList);
            return true;
        }
        default:
            break;
    }

    return QObject::eventFilter(watched, event);
}

/*
void MainWindow::on_actionConnect_triggered() {
    QMessageBox confirmBox(QMessageBox::Question, tr("Server connection"), tr("Do you want to break network connection?"), QMessageBox::NoButton, this);

    confirmBox.addButton(tr("No"), QMessageBox::NoRole);
    QPushButton *yesBtn = confirmBox.addButton(tr("Yes"), QMessageBox::YesRole);
    confirmBox.setDefaultButton(yesBtn);   

    switch (connection_state)
    {
        case csDisconnected:
        {            

            if (!servers->connectToServer())
            {
                actionConnect->setChecked(false);
                QMessageBox::warning(this, tr("Server connection"), tr("If you have servers please select one in the list"), QMessageBox::Ok);
            }

            break;
        }
        case csConnecting:
        {
            Session::instance()->stopServerConnection();
            break;
        }
        case csConnected:
        {
            confirmBox.exec();

            if(confirmBox.clickedButton() && confirmBox.clickedButton() == yesBtn)
            {
                Session::instance()->stopServerConnection();
            }

            break;
        }
        default:
            break;
    }
}
*/
// As program parameters, we can get paths or urls.
// This function parse the parameters and call
// the right addTorrent function, considering
// the parameter type.
void MainWindow::processParams(const QString& params_str)
{
    processParams(QStringList(params_str));
}

void MainWindow::processParams(const QStringList& params)
{
    Preferences pref;
    qDebug() << "process params: " << params;

    foreach (QString param, params)
    {
        param = param.trimmed();

        if (misc::isUrl(param))
        {
            //Session::instance()->downloadFromUrl(param);
        }
        else
        {
            if (param.startsWith("ed2k://", Qt::CaseInsensitive))
            {
                Session::instance()->addLink(param);
            }
            else if (param.endsWith(".emulecollection"))
            {
                //collection_save_dlg dialog(this, param);
                //dialog.exec();
            }

        }
    }
}

void MainWindow::addTorrent(QString path)
{
    //Session::instance()->addTorrent(path);
}

void MainWindow::optionsSaved()
{
    loadPreferences();
}

// Load program preferences
void MainWindow::loadPreferences(bool configure_session)
{
    /*
    Session::instance()->addConsoleMessage(tr("Options were saved successfully."));
    const Preferences pref;
    const bool newSystrayIntegration = pref.systrayIntegration();
    actionLock_mule->setVisible(newSystrayIntegration);

    if(newSystrayIntegration != (systrayIcon != 0))
    {
        if(newSystrayIntegration)
        {
            // create the trayicon
            if(!QSystemTrayIcon::isSystemTrayAvailable())
            {
                if(!configure_session)
                { // Program startup
                    systrayCreator = new QTimer(this);
                    connect(systrayCreator, SIGNAL(timeout()), this, SLOT(createSystrayDelayed()));
                    systrayCreator->setSingleShot(true);
                    systrayCreator->start(2000);
                    qDebug("Info: System tray is unavailable, trying again later.");
                }
                else
                {
                    qDebug("Warning: System tray is unavailable.");
                }
            }
            else
            {
                createTrayIcon();
            }
        }
        else
        {
            // Destroy trayicon
            delete systrayIcon;
            delete myTrayIconMenu;
        }
    }
     // Reload systray icon
    if(newSystrayIntegration && systrayIcon)
    {
        systrayIcon->setIcon(getSystrayIcon());
    }
     // General
    if(pref.isToolbarDisplayed())
    {
        toolBar->setVisible(true);
    }
    else
    {
        // Clear search filter before hiding the top toolbar
        search_filter->clear();
        toolBar->setVisible(false);
    }

    if(pref.preventFromSuspend())
    {
        preventTimer->start(PREVENT_SUSPEND_INTERVAL);
    }
    else
    {
        preventTimer->stop();
        m_pwr->setActivityState(false);
    }

    const uint new_refreshInterval = pref.getRefreshInterval();
    transferList->setRefreshInterval(new_refreshInterval);
    transfersView->setAlternatingRowColors(pref.useAlternatingRowColors());

     // Queueing System
    if(pref.isQueueingSystemEnabled())
    {
        if(!actionDecreasePriority->isVisible())
        {
            transferList->hidePriorityColumn(false);
            actionDecreasePriority->setVisible(true);
            actionIncreasePriority->setVisible(true);
        }
    }
    else
    {
        if(actionDecreasePriority->isVisible())
        {
            transferList->hidePriorityColumn(true);
            actionDecreasePriority->setVisible(false);
            actionIncreasePriority->setVisible(false);
        }
    }

    // Torrent properties
    // Icon provider
#if defined(Q_WS_X11)
    IconProvider::instance()->useSystemIconTheme(pref.useSystemIconTheme());
#endif

    if(configure_session)
        Session::instance()->configureSession();
*/
    qDebug("GUI settings loaded");
}

// Check connection status and display right icon
void MainWindow::updateGUI()
{
    SessionStatus status = Session::instance()->getSessionStatus();

    // update global informations
    if(systrayIcon)
    {
#if defined(Q_WS_X11) || defined(Q_WS_MAC)
        QString html = "<div style='background-color: #678db2; color: #fff;height: 18px; font-weight: bold; margin-bottom: 5px;'>";
        html += tr("eMule0.60");
        html += "</div>";
        html += "<div style='vertical-align: baseline; height: 18px;'>";
        html += "<img src=':iIcons/transfers/download.png'/>&nbsp;" +
        tr("DL speed: %1 KiB/s", "e.g: Download speed: 10 KiB/s")
        .arg(QString::number(status.payload_download_rate/1024., 'f', 1));
        html += "</div>";
        html += "<div style='vertical-align: baseline; height: 18px;'>";
        html += "<img src=':/icons/transfers/upload.png'/>&nbsp;" +
        tr("UP speed: %1 KiB/s", "e.g: Upload speed: 10 KiB/s")
        .arg(QString::number(status.payload_upload_rate/1024., 'f', 1));
        html += "</div>";
#else
        // OSes such as Windows do not support html here
        QString html = tr("DL speed: %1 KiB/s", "e.g: Download speed: 10 KiB/s").arg(QString::number(status.payload_download_rate / 1024., 'f', 1));
        html += "\n";
        html += tr("UP speed: %1 KiB/s", "e.g: Upload speed: 10 KiB/s").arg(QString::number(status.payload_upload_rate / 1024., 'f', 1));
#endif
        systrayIcon->setToolTip(html); // tray icon
    }

    if(displaySpeedInTitle)
    {
        setWindowTitle(tr("[D: %1/s, U: %2/s] eMule0.60 %3", "D = Download; U = Upload; %3 is eMule0.60 version").arg(misc::friendlyUnit(status.payload_download_rate)).arg(misc::friendlyUnit(status.payload_upload_rate)).arg(QString::fromUtf8(VERSION)));
    }

    statusBar->setUpDown(status.payload_upload_rate, status.payload_download_rate);
    //Session::instance()->playPendingMedia();
}

void MainWindow::showNotificationBaloon(QString title, QString msg) const
{
    if(!Preferences().useProgramNotification())
        return;

    // forward all notifications to the console
    addConsoleMessage(msg);

#if defined(Q_WS_X11) && defined(QT_DBUS_LIB)
    org::freedesktop::Notifications notifications("org.freedesktop.Notifications",
            "/org/freedesktop/Notifications",
            QDBusConnection::sessionBus());

    if (notifications.isValid())
    {
        QVariantMap hints;
        hints["desktop-entry"] = "eMule0.60";
        QDBusPendingReply<uint> reply = notifications.Notify("eMule0.60", 0, "eMule0.60", title,
                                                             msg, QStringList(), hints, -1);
        reply.waitForFinished();

        if (!reply.isError())
            return;
    }
#endif
    if(systrayIcon && QSystemTrayIcon::supportsMessages())
        systrayIcon->showMessage(title, msg, QSystemTrayIcon::Information, TIME_TRAY_BALLOON);
}

/*****************************************************
 *                                                   *
 *                      Utils                        *
 *                                                   *
 *****************************************************/

void MainWindow::downloadFromURLList(const QStringList& url_list)
{
    /*
    Preferences settings;
    const bool useTorrentAdditionDialog = settings.value(QString::fromUtf8("Preferences/Downloads/AdditionDialog"), true).toBool();

    foreach (QString url, url_list)
    {
        if (url.startsWith("bc://bt/", Qt::CaseInsensitive))
        {
            qDebug("Converting bc link to magnet link");
            url = misc::bcLinkToMagnet(url);
        }

        if (url.startsWith("magnet:", Qt::CaseInsensitive))
        {
            if (useTorrentAdditionDialog)
            {
                torrentAdditionDialog *dialog = new torrentAdditionDialog(this);
                dialog->showLoadMagnetURI(url);
            }
            else
            {
                Session::instance()->addLink(url);
            }
        }
        else
        {
            Session::instance()->downloadFromUrl(url);
        }
    }
    */
}

/*****************************************************
 *                                                   *
 *                     Options                       *
 *                                                   *
 *****************************************************/

void MainWindow::createSystrayDelayed()
{
    static int timeout = 20;
    if(QSystemTrayIcon::isSystemTrayAvailable())
    {
        // Ok, systray integration is now supported
        // Create systray icon
        createTrayIcon();
        delete systrayCreator;
    }
    else
    {
        if(timeout)
        {
            // Retry a bit later
            systrayCreator->start(2000);
            --timeout;
        }
        else
        {
            // Timed out, apparently system really does not
            // support systray icon
            delete systrayCreator;
            // Disable it in program preferences to
            // avoid trying at earch startup
            Preferences().setSystrayIntegration(false);
        }
    }
}

QMenu* MainWindow::getTrayIconMenu()
{
    if(myTrayIconMenu)
        return myTrayIconMenu;

    // Tray icon Menu
    myTrayIconMenu = new QMenu(this);
    //actionToggleVisibility->setText(isVisible()? tr("Hide") : tr("Show"));
    //myTrayIconMenu->addAction(actionToggleVisibility);
    //myTrayIconMenu->addSeparator();
    //myTrayIconMenu->addAction(actionOpen);

    /* disable useless actions
     myTrayIconMenu->addSeparator();
     const bool isAltBWEnabled = Preferences().isAltBandwidthEnabled();
     updateAltSpeedsBtn(isAltBWEnabled);
     actionUse_alternative_speed_limits->setChecked(isAltBWEnabled);
     myTrayIconMenu->addAction(actionUse_alternative_speed_limits);
     myTrayIconMenu->addAction(actionSet_global_download_limit);
     myTrayIconMenu->addAction(actionSet_global_upload_limit);
     myTrayIconMenu->addSeparator();
     myTrayIconMenu->addAction(actionStart_All);
     myTrayIconMenu->addAction(actionPause_All);
     myTrayIconMenu->addSeparator();
     */

    //myTrayIconMenu->addAction(actionExit);

    //if(ui_locked)
    //    myTrayIconMenu->setEnabled(false);

    return myTrayIconMenu;
}

void MainWindow::createTrayIcon()
{
    // Tray icon
    systrayIcon = new QSystemTrayIcon(getSystrayIcon(), this);

    systrayIcon->setContextMenu(getTrayIconMenu());
    connect(systrayIcon, SIGNAL(messageClicked()), this, SLOT(balloonClicked()));

    // End of Icon Menu
    connect(systrayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(toggleVisibility(QSystemTrayIcon::ActivationReason)));
    systrayIcon->show();
}

void MainWindow::setButtons(bool b)
{
    allDownloadsBtn_2->setChecked(b);
    downloadingBtn_2->setChecked(b);
    completedBtn_2->setChecked(b);
    waitingBtn_2->setChecked(b);
}

void MainWindow::on_actionTop_tool_bar_triggered()
{
    bool is_visible = static_cast<QAction*>(sender())->isChecked();
    toolBar->setVisible(is_visible);
    Preferences().setToolbarDisplayed(is_visible);
}

void MainWindow::on_actionSpeed_in_title_bar_triggered()
{
    displaySpeedInTitle = static_cast<QAction*>(sender())->isChecked();
    Preferences().showSpeedInTitleBar(displaySpeedInTitle);

    if(displaySpeedInTitle)
        updateGUI();
    else
        setWindowTitle(tr("eMule0.60 %1", "e.g: eMule0.60 v0.x").arg(QString::fromUtf8(VERSION)));
}

void MainWindow::showConnectionSettings()
{
    //options->showConnectionTab();
}

void MainWindow::minimizeWindow()
{
    setWindowState(windowState() ^ Qt::WindowMinimized);
}

void MainWindow::on_actionExecution_Logs_triggered(bool checked)
{
    if(checked)
    {
        //Q_ASSERT(!m_executionLog);
        //    m_executionLog = new ExecutionLog(tabs);
        //    int index_tab = tabs->addTab(m_executionLog, tr("Execution Log"));
        //    tabs->setTabIcon(index_tab, IconProvider::instance()->getIcon("view-calendar-journal"));
    }
    else
    {
        //if(m_executionLog)
        //    delete m_executionLog;
    }

    //Preferences().setExecutionLogEnabled(checked);
}

void MainWindow::on_actionAutoExit_mule_toggled(bool enabled)
{
    qDebug() << Q_FUNC_INFO << enabled;
    //Preferences().setShutdownqBTWhenDownloadsComplete(enabled);
}

void MainWindow::on_actionAutoSuspend_system_toggled(bool enabled)
{
    qDebug() << Q_FUNC_INFO << enabled;
    //Preferences().setSuspendWhenDownloadsComplete(enabled);
}

void MainWindow::on_actionAutoShutdown_system_toggled(bool enabled)
{
    qDebug() << Q_FUNC_INFO << enabled;
    //Preferences().setShutdownWhenDownloadsComplete(enabled);
}

QIcon MainWindow::getSystrayIcon() const
{
    /*
#if defined(Q_WS_X11)
    TrayIcon::Style style = Preferences().trayIconStyle();

    switch(style)
    {
        case TrayIcon::MONO_DARK:
            return QIcon(res::trayConnected());
        case TrayIcon::MONO_LIGHT:
            return QIcon(res::trayConnected());
        default:
            break;
    }
#endif
*/
    return icon_CurTray;
}

void MainWindow::addConsoleMessage(const QString& msg, QColor color /*=QApplication::palette().color(QPalette::WindowText)*/) const { qDebug() << msg; }

void MainWindow::ed2kConnectionInitialized(QED2KServerFingerprint sfp, quint32 client_id, quint32 tcp_flags, quint32 aux_port)
{
    statusBar->setConnected(true);
    actionConnect->setIcon(QIcon(res::toolbarConnected()));
    actionConnect->setText(tr("Connected"));
    actionConnect->setChecked(true);
    connection_state = csConnected;
    icon_CurTray = icon_TrayConn;

    if (systrayIcon)
        systrayIcon->setIcon(getSystrayIcon());

    QString log_msg("Client ID: ");
    QString id;
    id.setNum(client_id);
    log_msg += id;
    statusBar->setStatusMsg(log_msg);
}

void MainWindow::ed2kServerStatus(QED2KServerFingerprint sfp, int nFiles, int nUsers)
{
    statusBar->setServerInfo(nFiles, nUsers);
}

void MainWindow::ed2kConnectionPending(const QED2KServerFingerprint &sfp)
{
    m_connectingTo = sfp;
    actionConnect->setIcon(QIcon(res::toolbarConnecting()));
    actionConnect->setText(tr("Cancel"));
    actionConnect->setChecked(true);
}

void MainWindow::ed2kConnectionClosed(QED2KServerFingerprint sfp, QString strError)
{
    if (m_connectingTo == sfp)
    {
        actionConnect->setIcon(QIcon(res::toolbarDisconnected()));
        actionConnect->setText(tr("Disconnected"));
        actionConnect->setChecked(false);
    }

    connection_state = csDisconnected;
    statusBar->reset();
    icon_CurTray = icon_TrayDisconn;

    if (systrayIcon)
        systrayIcon->setIcon(getSystrayIcon());

    statusBar->setStatusMsg(strError);
}

void MainWindow::ed2kServerCountChanged(int count)
{
    if (connection_state == csDisconnected && count == 0)
    {
        actionConnect->setEnabled(false);
    }
    else
    {
        actionConnect->setEnabled(true);
    }
}

void MainWindow::on_actionOpenDownloadPath_triggered()
{
    Preferences pref;
    QDesktopServices::openUrl(QUrl::fromLocalFile(pref.getSavePathMule()));
}

#ifdef Q_WS_WIN
    Preferences pref;

    if (!pref.neverCheckFileAssoc() &&
        (!Preferences::isTorrentFileAssocSet() ||
                        !Preferences::isLinkAssocSet("Magnet") ||
                        !Preferences::isEmuleFileAssocSet() ||
                        !Preferences::isLinkAssocSet("ed2k")))
    {
        if (QMessageBox::question(0, tr("Torrent file association"),
                                tr("eMule0.60 is not the default application to open torrent files, Magnet links or eMule collections.\nDo you want to associate eMule0.60 to torrent files, Magnet links and eMule collections?"),
                                QMessageBox::Yes|QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes)
        {
            Preferences::setTorrentFileAssoc(true);
            Preferences::setLinkAssoc("Magnet", true);
            Preferences::setLinkAssoc("ed2k", true);
            Preferences::setEmuleFileAssoc(true);
            Preferences::setCommonAssocSection(true); // enable common section
        }
        else
        {
            pref.setNeverCheckFileAssoc();
        }
    }
#endif

void MainWindow::on_allDownloadsBtn_clicked(){selectTransfersPage(TransfersPages::All);}
void MainWindow::on_downloadingBtn_clicked(){selectTransfersPage(TransfersPages::Downloading);}
void MainWindow::on_completedBtn_clicked(){selectTransfersPage(TransfersPages::Completed);}
void MainWindow::on_waitingBtn_clicked(){ selectTransfersPage(TransfersPages::Waiting);}

void MainWindow::on_allDownloadsBtn_2_clicked(){
    //on_toTransfersBtn_clicked();
    //selectWidget(wTransfer);
    //selectTransfersPage(TransfersPages::All);
}

void MainWindow::on_downloadingBtn_2_clicked(){
    //on_toTransfersBtn_clicked();
    //selectTransfersPage(TransfersPages::Downloading);
    //selectWidget(wTransfer);
}

void MainWindow::on_completedBtn_2_clicked(){
    //on_toTransfersBtn_clicked();
    //selectTransfersPage(TransfersPages::Completed);
    //selectWidget(wTransfer);
}

void MainWindow::on_waitingBtn_2_clicked(){
    //on_toTransfersBtn_clicked();
    //selectTransfersPage(TransfersPages::Waiting);
    //selectWidget(wTransfer);
}

void MainWindow::on_torrentsBtn_2_clicked(){
    //on_toTransfersBtn_clicked();
    //selectTransfersPage(TransfersPages::Torrents);
    //selectWidget(wTorrents);
}

void MainWindow::on_uploadsBtn_clicked()
{
    //uploadsBtn->setChecked(true);
    //downloadsBtn->setChecked(false);
    //peersList->showDownload(false);
}

void MainWindow::on_downloadsBtn_clicked()
{
    //downloadsBtn->setChecked(true);
    //uploadsBtn->setChecked(false);
    //peersList->showDownload(true);
}

void MainWindow::on_addURLBtn_clicked()
{
    //transferList->addLinkDialog();
}