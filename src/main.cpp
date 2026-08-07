#include <QApplication>
#include <QMenu>
#include <QStyle>
#include <QSystemTrayIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Set the QSettings identity before anything reads settings (Decision 11),
    // so the on-disk location is correct from the very first launch.
    QCoreApplication::setOrganizationName("com.isaacwiebe");
    QCoreApplication::setApplicationName("ProPager");

    // Menu-bar / tray app: closing or hiding a window must not quit the process.
    app.setQuitOnLastWindowClosed(false);

    // Placeholder tray icon (real assets are Task 8). A concrete icon is needed
    // or the tray item may not render on some platforms.
    const QIcon icon = app.style()->standardIcon(QStyle::SP_ComputerIcon);
    QSystemTrayIcon tray(icon);
    tray.setToolTip("ProPager");

    QMenu menu;
    QAction *quitAction = menu.addAction("Quit");
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);
    tray.setContextMenu(&menu);

    tray.setVisible(true);

    // Start minimized to tray: no main window is shown in this task.
    return app.exec();
}
