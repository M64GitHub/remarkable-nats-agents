#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include <QTextStream>
#include <QTimer>
#include <cstdio>

#include "nats/NatsClient.h"
#include "agents/AgentProtocol.h"
#include "agents/AppController.h"

namespace {

// Headless end-to-end smoke test of the transport + protocol stack, with no QML.
// Enabled by setting AGENT_CHAT_SMOKE=<prompt text>. Connects, sends one prompt
// to AGENT_CHAT_SMOKE_SUBJECT (default the local echo subject), prints the
// streamed reply, and exits 0 on completion / 1 on error / 2 on timeout. Lets us
// verify the wire on a host with no display (and serves as a CI check).
int runSmoke(QGuiApplication &app, const QString &promptText)
{
    QTextStream out(stdout);
    const QString subject = qEnvironmentVariable("AGENT_CHAT_SMOKE_SUBJECT",
                                                 "agents.prompt.echo.local.test");
    const QString host = qEnvironmentVariable("AGENT_CHAT_SMOKE_HOST", "127.0.0.1");
    const quint16 port = static_cast<quint16>(qEnvironmentVariable("AGENT_CHAT_SMOKE_PORT", "4222").toUInt());

    auto *nats = new NatsClient(&app);
    auto *proto = new AgentProtocol(nats, &app);

    QObject::connect(nats, &INatsConnection::connected, &app, [&]() {
        out << "[smoke] connected; sending prompt\n"; out.flush();
        proto->sendPrompt(subject, promptText);
    });
    QObject::connect(nats, &INatsConnection::errorOccurred, &app, [&](const QString &m) {
        out << "[smoke] connection error: " << m << "\n"; out.flush();
    });
    QObject::connect(proto, &AgentProtocol::promptAck, &app, [&](const QString &) {
        out << "[smoke] ack\n"; out.flush();
    });
    QObject::connect(proto, &AgentProtocol::promptResponse, &app, [&](const QString &, const QString &d) {
        out << "[smoke] response chunk: " << d << "\n"; out.flush();
    });
    QObject::connect(proto, &AgentProtocol::promptComplete, &app, [&](const QString &) {
        out << "[smoke] complete\n"; out.flush();
        QCoreApplication::exit(0);
    });
    QObject::connect(proto, &AgentProtocol::promptError, &app, [&](const QString &, int code, const QString &m) {
        out << "[smoke] error " << code << ": " << m << "\n"; out.flush();
        QCoreApplication::exit(1);
    });

    QTimer::singleShot(10000, &app, []() {
        std::fprintf(stderr, "[smoke] timed out\n");
        QCoreApplication::exit(2);
    });

    nats->connectToServer(host, port);
    return app.exec();
}

}  // namespace

// We own the object graph here (transport -> protocol -> controller) and hand the
// controller to QML as the single context property `App`. The QML layer never
// sees NATS directly. Swapping NatsClient for a future nats.zig transport is a
// one-line change here, because everything above it depends on INatsConnection.
int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("synadia");
    app.setApplicationName("agent-chat");

    const QString smoke = qEnvironmentVariable("AGENT_CHAT_SMOKE");
    if (!smoke.isEmpty())
        return runSmoke(app, smoke);

    NatsClient nats;
    AgentProtocol protocol(&nats);
    AppController controller(&protocol);
    controller.loadRoster();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("App", &controller);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("AgentChat", "Main");
    return app.exec();
}
