#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QIcon>
#include <QDebug>

#include "bridge/PipelineBridge.hpp"
#include "bridge/PreviewProvider.hpp"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("AetherChrome");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("AetherChrome");

    // Style Basic — pas de dépendance QtWidgets
    QQuickStyle::setStyle("Basic");

    QQmlApplicationEngine engine;

    // Bridge C++ → QML
    auto* bridge   = new PipelineBridge(&app);
    auto* provider = new PreviewProvider();

    // Enregistrer le provider d'images
    engine.addImageProvider("preview", provider);

    // Exposer le bridge au QML comme propriété globale
    engine.rootContext()->setContextProperty("pipeline", bridge);

    // Link bridge and provider
    bridge->setPreviewProvider(provider);

    // Charger le QML principal
    using namespace Qt::StringLiterals;
    const QUrl url(u"qrc:/AetherChrome/app/ui/Main.qml"_s);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [url](const QUrl& objUrl) {
            if (url == objUrl) {
                qCritical() << "QML load failed:" << url;
                QCoreApplication::exit(-1);
            }
        },
        Qt::QueuedConnection);

    engine.load(url);

    // Si un ARW est passé en argument, le charger
    const auto args = QCoreApplication::arguments();
    if (args.size() > 1)
        bridge->loadFile(args[1]);

    return app.exec();
}
