#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include <QDir>
#include <QFont>
#include <QStyleHints>
#include <QTextStream>
#include <QTimer>
#include <csignal>
#include <cstdio>

#include <QFile>
#include <QFileInfo>

#include "nats/NatsClient.h"
#include "agents/AgentProtocol.h"
#include "agents/AppController.h"
#include "notes/NoteStore.h"
#include "rm/RmParser.h"
#include "rm/RmRenderer.h"

namespace {

// Async-signal-safe quit: the handler only flips a flag; a polling timer (set up
// in main) does the actual app.quit() on the Qt thread. Lets SIGINT/SIGTERM/SIGHUP
// (e.g. closing the ssh session) shut the app down cleanly so the launcher's
// `trap ... EXIT` restores xochitl. The in-app Exit button is the primary path.
volatile std::sig_atomic_t g_quitRequested = 0;
void requestQuit(int) { g_quitRequested = 1; }

// Shared by the headless modes so they can target NGS: `AGENT_CHAT_TLS=1` and
// `AGENT_CHAT_CREDS=<path to .creds>` (~ expanded).
bool smokeTls() { return qEnvironmentVariable("AGENT_CHAT_TLS") == QLatin1String("1"); }
QString smokeCreds()
{
    QString c = qEnvironmentVariable("AGENT_CHAT_CREDS");
    if (c.startsWith(QStringLiteral("~/")))
        c = QDir::homePath() + c.mid(1);
    return c;
}

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

    // Optional attachments: AGENT_CHAT_ATTACH=path1,path2 (~ expanded).
    QList<AgentProtocol::Attachment> atts;
    const QString attachEnv = qEnvironmentVariable("AGENT_CHAT_ATTACH");
    if (!attachEnv.isEmpty()) {
        int idx = 1;
        const QStringList paths = attachEnv.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &raw : paths) {
            QString path = raw.trimmed();
            if (path.startsWith(QStringLiteral("~/")))
                path = QDir::homePath() + path.mid(1);
            QFile f(path);
            if (f.open(QIODevice::ReadOnly)) {
                // Keep the real extension (png/pdf) so the agent treats it correctly.
                QString fn = QFileInfo(path).fileName();
                if (fn.isEmpty())
                    fn = QStringLiteral("page-%1").arg(idx);
                ++idx;
                atts.append({fn, f.readAll()});
            } else {
                out << "[smoke] cannot read attachment: " << path << "\n";
            }
        }
        out << "[smoke] " << atts.size() << " attachment(s)\n"; out.flush();
    }

    QObject::connect(nats, &INatsConnection::connected, &app, [&]() {
        out << "[smoke] connected; sending prompt\n"; out.flush();
        proto->sendPrompt(subject, promptText, atts);
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

    const int timeoutMs = qEnvironmentVariable("AGENT_CHAT_SMOKE_TIMEOUT_MS", "15000").toInt();
    QTimer::singleShot(timeoutMs, &app, []() {
        std::fprintf(stderr, "[smoke] timed out\n");
        QCoreApplication::exit(2);
    });

    nats->connectToServer(host, port, smokeTls(), smokeCreds());
    return app.exec();
}

// Headless $SRV discovery + heartbeat probe. Enabled by AGENT_CHAT_DISCOVER=1.
// Connects, watches heartbeats, runs discovery, prints the roster + any beats,
// then exits. Lets us verify M2 against a live server with no display.
int runDiscover(QGuiApplication &app)
{
    QTextStream out(stdout);
    const QString host = qEnvironmentVariable("AGENT_CHAT_SMOKE_HOST", "127.0.0.1");
    const quint16 port = static_cast<quint16>(qEnvironmentVariable("AGENT_CHAT_SMOKE_PORT", "4222").toUInt());

    auto *nats = new NatsClient(&app);
    auto *proto = new AgentProtocol(nats, &app);

    QObject::connect(nats, &INatsConnection::connected, &app, [&]() {
        out << "[discover] connected; watching heartbeats + discovering\n"; out.flush();
        proto->startHeartbeatWatch();
        proto->discover(1500);
    });
    QObject::connect(proto, &AgentProtocol::agentsDiscovered, &app,
                     [&](const QVector<AgentProtocol::DiscoveredAgent> &agents) {
        out << "[discover] found " << agents.size() << " agent(s):\n";
        for (const auto &a : agents) {
            out << "  - " << a.name << "  [" << a.agent << "/" << a.owner << "]"
                << "  subject=" << a.subject << "  id=" << a.instanceId
                << "  proto=" << a.protocolVersion
                << "  attachments_ok=" << (a.attachmentsOk ? "true" : "false") << "\n";
        }
        out.flush();
    });
    QObject::connect(proto, &AgentProtocol::heartbeat, &app, [&](const QString &id, int s) {
        out << "[discover] heartbeat id=" << id << " interval_s=" << s << "\n"; out.flush();
    });

    QTimer::singleShot(4000, &app, []() { QCoreApplication::exit(0); });
    nats->connectToServer(host, port, smokeTls(), smokeCreds());
    return app.exec();
}

// Headless self-test for the multi-conversation ChatModel (AGENT_CHAT_TEST=chat):
// verifies per-agent history is retained across switches and that a streaming
// reply routes to the right conversation. No NATS, no QML.
int runChatTest()
{
    QTextStream out(stdout);
    ChatModel m;
    bool ok = true;
    auto check = [&](const char *name, bool cond) {
        out << (cond ? "  ok  " : " FAIL ") << name << "\n";
        ok = ok && cond;
    };

    m.setConversation("agentA");
    m.appendUser("hello");
    m.appendAgentPending("r1");
    m.appendDelta("r1", "hi ");
    m.appendDelta("r1", "there");
    m.setDone("r1");
    check("A has 2 rows", m.rowCount() == 2);
    check("A agent text concatenated",
          m.data(m.index(1), ChatModel::TextRole).toString() == "hi there");

    m.setConversation("agentB");
    check("B starts empty", m.rowCount() == 0);
    m.appendUser("yo");
    // A reply for A arrives while B is on screen — must update A, not B.
    m.appendDelta("r1", "!");
    check("B still 1 row after A reply", m.rowCount() == 1);

    m.setConversation("agentA");
    check("A preserved on return (2 rows)", m.rowCount() == 2);
    check("A reply updated off-screen",
          m.data(m.index(1), ChatModel::TextRole).toString() == "hi there!");

    out << (ok ? "RESULT: PASS\n" : "RESULT: FAIL\n");
    return ok ? 0 : 1;
}

// Headless self-test for the note store (AGENT_CHAT_TEST=notes): lists notebooks
// from $AGENT_CHAT_XOCHITL and checks each page thumbnail exists.
int runNotesTest()
{
    QTextStream out(stdout);
    NoteStore store;
    QString root = qEnvironmentVariable("AGENT_CHAT_XOCHITL");
    if (root.isEmpty())
        root = QStringLiteral("/home/root/.local/share/remarkable/xochitl");
    if (root.startsWith(QStringLiteral("~/")))
        root = QDir::homePath() + root.mid(1);
    store.setRootPath(root);
    out << "root: " << root << "\n" << "notebooks: " << store.count() << "\n";
    bool ok = store.count() > 0;
    for (int i = 0; i < store.count(); ++i) {
        const NoteStore::Note *n = store.at(i);
        out << "  - \"" << n->name << "\"  folder='" << n->folder
            << "'  pages=" << n->pages.size() << "\n";
        for (const NoteStore::Page &p : n->pages) {
            // v2: a page is usable if it has a renderable .rm or a thumbnail fallback.
            const bool hasRm = !p.rm.isEmpty() && QFile::exists(p.rm);
            const bool hasThumb = !p.thumbnail.isEmpty() && QFile::exists(p.thumbnail);
            ok = ok && (hasRm || hasThumb);
            out << "      " << (hasRm ? "[rm] " : (hasThumb ? "[thumb] " : "[MISSING] "))
                << (hasRm ? p.rm : p.thumbnail) << "\n";
        }
    }
    out << (ok ? "RESULT: PASS\n" : "RESULT: FAIL\n");
    return ok ? 0 : 1;
}

// Headless self-test for the in-app .rm renderer (AGENT_CHAT_TEST=render): parse
// one or more `.rm` files, print stats (blocks/strokes/points/bbox/tool+color
// histograms, and the leftover-byte count that proves byte-exact framing), and
// save a render. Knobs via env:
//   AGENT_CHAT_RENDER_IN   comma-separated .rm paths (required; order = page order)
//   AGENT_CHAT_RENDER_OUT  output file (default /tmp/rm-render.png). A `.pdf`
//                          extension emits one multi-page PDF; otherwise the FIRST
//                          page is saved as PNG.
//   AGENT_CHAT_RENDER_ROT  rotation degrees (default 0; landscape stays upright)
//   AGENT_CHAT_RENDER_SCALE / _PEN / _UNIFORM  geometry + width calibration
int runRenderTest()
{
    QTextStream out(stdout);
    const QString inEnv = qEnvironmentVariable("AGENT_CHAT_RENDER_IN");
    if (inEnv.isEmpty()) {
        out << "set AGENT_CHAT_RENDER_IN=<file.rm>[,<file2.rm>...]\nRESULT: FAIL\n";
        return 1;
    }
    QStringList inputs;
    for (const QString &raw : inEnv.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        QString in = raw.trimmed();
        if (in.startsWith(QStringLiteral("~/")))
            in = QDir::homePath() + in.mid(1);
        inputs << in;
    }

    rm::RenderOptions opt;
    opt.rotation = qEnvironmentVariable("AGENT_CHAT_RENDER_ROT", "0").toInt();
    opt.scale = qEnvironmentVariable("AGENT_CHAT_RENDER_SCALE", "1.0").toDouble();
    opt.penScale = qEnvironmentVariable("AGENT_CHAT_RENDER_PEN", "1.0").toDouble();
    opt.uniformWidth = qEnvironmentVariable("AGENT_CHAT_RENDER_UNIFORM", "6.0").toDouble();
    opt.textFontPx = qEnvironmentVariable("AGENT_CHAT_RENDER_TEXTPX", "64.0").toDouble();
    opt.textLineHeight = qEnvironmentVariable("AGENT_CHAT_RENDER_TEXTLINEH", "70.0").toDouble();
    opt.drawText = qEnvironmentVariable("AGENT_CHAT_RENDER_NOTEXT") != QLatin1String("1");
    const QString outPath = qEnvironmentVariable("AGENT_CHAT_RENDER_OUT",
                                                 "/tmp/rm-render.png");
    const bool wantPdf = outPath.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive);

    std::vector<rm::Page> pages;
    bool allOk = true;
    for (const QString &in : inputs) {
        QFile f(in);
        if (!f.open(QIODevice::ReadOnly)) {
            out << "cannot open " << in << "\n";
            allOk = false;
            continue;
        }
        const QByteArray data = f.readAll();
        rm::Page page;
        rm::ParseStats st;
        QString err;
        const bool ok = rm::RmParser::parse(data, page, &st, &err);
        allOk = allOk && ok && st.headerOk;

        out << "file: " << in << "  (" << data.size() << " bytes)\n";
        out << "  header: " << (st.headerOk ? "ok" : "BAD") << " v" << st.version
            << "  blocks: " << st.blocks << "  sceneLineItems: " << st.sceneLineItems
            << " (value-less: " << st.valuelessItems << ")\n";
        out << "  strokes: " << st.strokes << "  points: " << st.points
            << "  leftover: " << qulonglong(st.leftover)
            << (st.leftover == 0 ? " (byte-exact)" : " (NOT byte-exact)") << "\n";
        if (page.hasContent)
            out << "  bbox: x[" << page.minX << ", " << page.maxX << "]  y["
                << page.minY << ", " << page.maxY << "]  ("
                << (page.maxX - page.minX) << " × " << (page.maxY - page.minY) << ")\n";
        if (page.hasText)
            out << "  typed text: " << page.text.size() << " chars @ ("
                << page.textX << ", " << page.textY << ") width=" << page.textWidth
                << "\n    \"" << page.text.left(200) << "\"\n";
        if (!ok)
            out << "  parse error: " << err << "\n";
        pages.push_back(std::move(page));
    }

    bool saved = false;
    if (wantPdf) {
        int emitted = 0;
        saved = rm::RmRenderer::renderToPdf(pages, outPath, opt, &emitted);
        out << "rendered PDF: " << emitted << " page(s) -> " << outPath
            << (saved ? "  [ok]" : "  [NO OUTPUT]") << "\n";
    } else {
        // PNG is single-page: render the first page with any content (strokes/text).
        const rm::Page *first = nullptr;
        for (const rm::Page &pg : pages)
            if (pg.hasContent || pg.hasText) { first = &pg; break; }
        if (first) {
            const QImage img = rm::RmRenderer::renderToImage(*first, opt);
            saved = img.save(outPath, "PNG");
            out << "rendered PNG: " << img.width() << "×" << img.height()
                << " px -> " << outPath << (saved ? "  [ok]" : "  [SAVE FAILED]") << "\n";
        } else {
            out << "no strokes to render\n";
        }
    }

    const bool pass = allOk && saved;
    out << (pass ? "RESULT: PASS\n" : "RESULT: FAIL\n");
    return pass ? 0 : 1;
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

    // E-paper refresh tuning (helps typing latency):
    //  - a steady (non-blinking) text cursor avoids a repaint twice a second;
    //  - non-anti-aliased text keeps glyph regions 2-colour so the panel can use
    //    its fast monochrome waveform instead of the slow grayscale one.
    app.styleHints()->setCursorFlashTime(0);
    QFont appFont = app.font();
    appFont.setStyleStrategy(QFont::NoAntialias);
    app.setFont(appFont);

    const QString smoke = qEnvironmentVariable("AGENT_CHAT_SMOKE");
    if (!smoke.isEmpty())
        return runSmoke(app, smoke);
    if (!qEnvironmentVariable("AGENT_CHAT_DISCOVER").isEmpty())
        return runDiscover(app);
    if (qEnvironmentVariable("AGENT_CHAT_TEST") == QLatin1String("chat"))
        return runChatTest();
    if (qEnvironmentVariable("AGENT_CHAT_TEST") == QLatin1String("notes"))
        return runNotesTest();
    if (qEnvironmentVariable("AGENT_CHAT_TEST") == QLatin1String("render"))
        return runRenderTest();

    NatsClient nats;
    AgentProtocol protocol(&nats);
    AppController controller(&protocol);
    controller.loadRoster();

    // Quit cleanly on signals (see requestQuit). A 200ms poll only reads a flag,
    // so it triggers no repaints on the e-paper panel.
    std::signal(SIGINT, requestQuit);
    std::signal(SIGTERM, requestQuit);
    std::signal(SIGHUP, requestQuit);
    auto *quitPoll = new QTimer(&app);
    quitPoll->start(200);
    QObject::connect(quitPoll, &QTimer::timeout, &app, [&app]() {
        if (g_quitRequested)
            app.quit();
    });

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
