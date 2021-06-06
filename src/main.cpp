#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTextCodec>
#include <QThread>
#include <QtDebug>

#include "config.h"
#include "errordialoghandler.h"
#include "mixxx.h"
#include "mixxxapplication.h"
#include "sources/soundsourceproxy.h"
#include "util/cmdlineargs.h"
#include "util/console.h"
#include "util/logging.h"
#include "util/versionstore.h"

#ifdef Q_OS_LINUX
#include <X11/Xlib.h>
#endif

namespace {

// Exit codes
constexpr int kFatalErrorOnStartupExitCode = 1;
constexpr int kParseCmdlineArgsErrorExitCode = 2;

constexpr char kScaleFactorEnvVar[] = "QT_SCALE_FACTOR";
const QString kstrScaleFactor = QStringLiteral("ScaleFactor");

int runMixxx(MixxxApplication* app, const CmdlineArgs& args) {
    MixxxMainWindow mainWindow(app, args);
    // If startup produced a fatal error, then don't even start the
    // Qt event loop.
    if (ErrorDialogHandler::instance()->checkError()) {
        mainWindow.finalize();
        return kFatalErrorOnStartupExitCode;
    } else {
        qDebug() << "Displaying main window";
        mainWindow.show();

        qDebug() << "Running Mixxx";
        return app->exec();
    }
}

void adjustScaleFactor(const CmdlineArgs& args) {
    if (qEnvironmentVariableIsSet(kScaleFactorEnvVar)) {
        bool ok;
        const double f = qgetenv(kScaleFactorEnvVar).toDouble(&ok);
        if (ok && f > 0) {
            // The environment variable overrides the preferences option
            qDebug() << "Using" << kScaleFactorEnvVar << f;
            return;
        }
    }
    // We cannot use ConfigObject, because it depends on MixxxApplication
    // but the scale factor is read during it's constructor.
    // QHighDpiScaling can not be used afterwards because it is private.
    auto cfgFile = QFile(QDir(args.getSettingsPath()).filePath(MIXXX_SETTINGS_FILE));
    if (cfgFile.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream in(&cfgFile);
        QString scaleFactor;
        QString line = in.readLine();
        while (!line.isNull()) {
            if (line.startsWith(kstrScaleFactor)) {
                scaleFactor = line.mid(kstrScaleFactor.size() + 1);
                break;
            }
            line = in.readLine();
        }
        if (!scaleFactor.isEmpty()) {
            qDebug() << "Using preferences ScaleFactor" << scaleFactor;
            qputenv(kScaleFactorEnvVar, scaleFactor.toLocal8Bit());
        }
    }
}

} // anonymous namespace

int main(int argc, char * argv[]) {
    Console console;

#ifdef Q_OS_LINUX
    XInitThreads();
#endif

    // These need to be set early on (not sure how early) in order to trigger
    // logic in the OS X appstore support patch from QTBUG-16549.
    QCoreApplication::setOrganizationDomain("mixxx.org");

    // This needs to be set before initializing the QApplication.
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    // Setting the organization name results in a QDesktopStorage::DataLocation
    // of "$HOME/Library/Application Support/Mixxx/Mixxx" on OS X. Leave the
    // organization name blank.
    //QCoreApplication::setOrganizationName("Mixxx");

    QCoreApplication::setApplicationName(VersionStore::applicationName());
    QCoreApplication::setApplicationVersion(VersionStore::version());

    // Construct a list of strings based on the command line arguments
    CmdlineArgs& args = CmdlineArgs::Instance();
    if (!args.Parse(argc, argv)) {
        args.printUsage();
        return kParseCmdlineArgsErrorExitCode;
    }

    // If you change this here, you also need to change it in
    // ErrorDialogHandler::errorDialog(). TODO(XXX): Remove this hack.
    QThread::currentThread()->setObjectName("Main");

    // Create the ErrorDialogHandler in the main thread, otherwise it will be
    // created in the thread of the first caller to instance(), which may not be
    // the main thread. Bug #1748636.
    ErrorDialogHandler::instance();

#ifdef __APPLE__
    Sandbox::checkSandboxed();
    if (!args.getSettingsPathSet()) {
        args.setSettingsPath(Sandbox::migrateOldSettings());
    }
#endif

    adjustScaleFactor(args);

    MixxxApplication app(argc, argv);

#ifdef __APPLE__
    QDir dir(QApplication::applicationDirPath());
    // Set the search path for Qt plugins to be in the bundle's PlugIns
    // directory, but only if we think the mixxx binary is in a bundle.
    if (dir.path().contains(".app/")) {
        // If in a bundle, applicationDirPath() returns something formatted
        // like: .../Mixxx.app/Contents/MacOS
        dir.cdUp();
        dir.cd("PlugIns");
        qDebug() << "Setting Qt plugin search path to:" << dir.absolutePath();
        // asantoni: For some reason we need to do setLibraryPaths() and not
        // addLibraryPath(). The latter causes weird problems once the binary
        // is bundled (happened with 1.7.2 when Brian packaged it up).
        QApplication::setLibraryPaths(QStringList(dir.absolutePath()));
    }
#endif

    // When the last window is closed, terminate the Qt event loop.
    QObject::connect(&app, &MixxxApplication::lastWindowClosed, &app, &MixxxApplication::quit);

    int exitCode = runMixxx(&app, args);

    qDebug() << "Mixxx shutdown complete with code" << exitCode;

    mixxx::Logging::shutdown();

    return exitCode;
}
