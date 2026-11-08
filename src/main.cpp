#include "AppFont.h"
#include "MainWindow.h"
#include "Style.h"
#include "Theme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("sift");
    QGuiApplication::setDesktopFileName("sift"); // matches sift.desktop, so the shell shows the right icon

    // Installed icon first; when running straight from the build folder, use the one in data/.
    QIcon icon = QIcon::fromTheme("sift");
    if (icon.isNull())
        icon = QIcon(QCoreApplication::applicationDirPath() + "/../data/sift.svg");
    QApplication::setWindowIcon(icon);

    AppFont::restoreSaved();
    ThemeManager::instance().restoreSaved();
    Style::install();

    MainWindow window;
    window.show();

    return app.exec();
}
