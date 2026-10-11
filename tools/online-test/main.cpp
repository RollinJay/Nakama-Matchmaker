/* SPDX-License-Identifier: MIT
 * netplay-online-test: one player of a quick match, without an emulator. It signs in,
 * finds a match through the matchmaker, opens the direct connections to every other
 * player, measures the round trips, exchanges one chat line and exits. Several copies
 * against one server test the whole path that an emulator takes before its game starts. */
#include "MatchSession.h"
#include "PeerLinks.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <cstdio>

using namespace NetplayOnline;

int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	QCommandLineParser parser;
	parser.addHelpOption();
	parser.addOption({ "host", "Nakama server host", "host", "127.0.0.1" });
	parser.addOption({ "port", "Nakama server port", "port", "7350" });
	parser.addOption({ "key", "Nakama server key", "key", "defaultkey" });
	parser.addOption({ "device", "device id (one account per id)", "id" });
	parser.addOption({ "name", "player name", "name", "tester" });
	parser.addOption({ "game", "game identity to match on", "identity", "test-identity" });
	parser.addOption({ "kind", "game kind", "kind", "mgba" });
	parser.addOption({ "players", "players per match", "n", "2" });
	parser.addOption({ "stun", "STUN server host:port", "server" });
	parser.addOption({ "timeout", "seconds before giving up", "s", "60" });
	parser.process(app);

	MatchSession::QuickMatch q;
	q.server.host = parser.value("host");
	q.server.port = parser.value("port").toInt();
	q.server.serverKey = parser.value("key");
	q.deviceId = parser.value("device");
	q.name = parser.value("name");
	q.identity = parser.value("game");
	q.kind = parser.value("kind");
	q.players = parser.value("players").toInt();
	if (parser.isSet("stun")) {
		q.stunServers.append(parser.value("stun"));
	}
	if (q.deviceId.isEmpty()) {
		fprintf(stderr, "--device is required\n");
		return 1;
	}

	MatchSession session;
	QThread* worker = nullptr;
	std::atomic<bool> stop(false);
	int chats = 0;
	int exitCode = 1;
	QElapsedTimer clock;
	clock.start();

	auto finish = [&](int code) {
		exitCode = code;
		stop = true;
		if (worker) {
			worker->wait();
		}
		session.leave();
		QTimer::singleShot(200, &app, &QCoreApplication::quit);
	};

	QObject::connect(&session, &MatchSession::failed, [&](const QString& error) {
		fprintf(stdout, "%s: FAILED: %s\n", qPrintable(q.name), qPrintable(error));
		finish(1);
	});
	QObject::connect(&session, &MatchSession::linking, [&]() {
		PeerLinks* links = session.links();
		fprintf(stdout, "%s: player %d of %d in the match, opening %d connections\n", qPrintable(q.name), session.self() + 1,
		        session.players().size(), links->peers());
		fflush(stdout);
		worker = QThread::create([&, links]() {
			QString error;
			if (!links->start(0, session.stunServers(), &error)) {
				QMetaObject::invokeMethod(&session, [&, error]() { session.setLinked(false, error); });
				return;
			}
			bool reported = false;
			while (!stop) {
				links->poll();
				if (!reported && links->allConnected()) {
					reported = true;
					QMetaObject::invokeMethod(&session, [&]() { session.setLinked(true); });
				}
				if (!reported && links->failed()) {
					reported = true;
					QString e = links->error();
					QMetaObject::invokeMethod(&session, [&, e]() { session.setLinked(false, e); });
				}
				QThread::msleep(5);
			}
			links->close();
		});
		worker->start();
	});
	QObject::connect(&session, &MatchSession::stateChanged, [&](MatchSession::State state) {
		if (state != MatchSession::State::Ready) {
			return;
		}
		fprintf(stdout, "%s: all connections open after %.1fs\n", qPrintable(q.name), clock.elapsed() / 1000.0);
		fflush(stdout);
		// measure round trips for 3 seconds, then chat
		QTimer::singleShot(3000, [&]() {
			PeerLinks* links = session.links();
			for (int i = 0; i < links->peers(); ++i) {
				fprintf(stdout, "%s: peer %s at %s, round trip %d ms\n", qPrintable(q.name), qPrintable(links->peerId(i).left(8)),
				        qPrintable(links->peerAddress(i)), links->roundTripMs(i));
			}
			fflush(stdout);
			session.sendChat(QStringLiteral("hello from %1").arg(q.name));
		});
	});
	QObject::connect(&session, &MatchSession::chatReceived, [&](const QString& name, const QString& text) {
		fprintf(stdout, "%s: chat <%s> %s\n", qPrintable(q.name), qPrintable(name), qPrintable(text));
		fflush(stdout);
		if (++chats == session.players().size()) {   // ours and every other player's
			fprintf(stdout, "%s: OK\n", qPrintable(q.name));
			fflush(stdout);
			QTimer::singleShot(1000, [&]() { finish(0); });
		}
	});
	QTimer::singleShot(parser.value("timeout").toInt() * 1000, [&]() {
		fprintf(stdout, "%s: TIMEOUT in state %d\n", qPrintable(q.name), int(session.state()));
		finish(1);
	});

	session.startQuickMatch(q);
	app.exec();
	return exitCode;
}
