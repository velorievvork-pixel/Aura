// client/src/main.cpp — точка входа Qt-клиента Aura.
//
//   AURA_SERVER_URL=ws://127.0.0.1:9000 ./aura-client
#include <QDirIterator>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "appstore.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Aura"));
    QGuiApplication::setApplicationName(QStringLiteral("Aura"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/aura.png")));

    // Manrope из ресурсов — шрифт макетов на любой системе.
    QDirIterator fonts(QStringLiteral(":/fonts"), {QStringLiteral("*.ttf")});
    while (fonts.hasNext()) QFontDatabase::addApplicationFont(fonts.next());

    // Свой дизайн полностью на QML (графит + стекло), стандартный стиль не нужен.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    aura::AppStore store;
    // AURA_SERVER_URL — для разработки; иначе адрес из настроек (его меняют на
    // экране входа и в настройках), по умолчанию ws://127.0.0.1:9000.
    if (qEnvironmentVariableIsSet("AURA_SERVER_URL")) {
        store.setServerUrl(QUrl(qEnvironmentVariable("AURA_SERVER_URL")));
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &store);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule(QStringLiteral("Aura"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) return -1;

    store.start();
    return QGuiApplication::exec();
}
