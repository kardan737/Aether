#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "core/AppCore.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    // Инициализируем ядро приложения (создаст потоки БД и Сети)
    AppCore appCore;
    appCore.initialize();

    QQmlApplicationEngine engine;
    
    // Прокидываем appCore внутрь QML
    engine.rootContext()->setContextProperty("appCore", &appCore);
    
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("Aether", "Main");

    return app.exec();
}