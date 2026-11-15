#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QtTest>
#include <QElapsedTimer>

#include "app/AppController.h"
#include "app/Updater.h"
#include "net/LocalSocket.h"
#include "rdp/KeyMapper.h"
#include <QUrl>
#include "rdp/RdpClipboard.h"
#include "vnc/VncSession.h"
#include "vnc/AppleAdaptive.h"
#include <QImage>
#include <QtEndian>
#include "net/NetCore.h"
#include "vault/VaultStore.h"

namespace {
// --- Apple Adaptive (0x3f3): the cases of zDesk's decoder tests, ported ---------------------
struct BitWriter {
    QByteArray bytes; quint64 window = 0; int held = 0;
    BitWriter &put(quint32 v, int n) { window = (window << n) | (v & ((quint64(1) << n) - 1)); held += n;
        while (held >= 8) { held -= 8; bytes.append(char((window >> held) & 0xff)); } return *this; }
    BitWriter &bits(const char *t) { for (; *t; ++t) if (*t == '0' || *t == '1') put(*t == '1', 1); return *this; }
    QByteArray done() { if (held > 0) put(0, 8 - held); return bytes; }
};
QByteArray adaptiveTables() { QByteArray b(1, char(2)); b.append(128, char(1)); return b; }
QByteArray adaptiveFull(BitWriter &cmd, BitWriter &data) {
    cmd.put(0x6d, 8); data.put(0x6d, 8);
    const QByteArray c = cmd.done(), d = data.done();
    const int off = 6 + c.size();
    QByteArray b; b.append(char(0)).append(char(3)).append(char(5)).append(char(off >> 16)).append(char(off >> 8)).append(char(off));
    return b + c + d;
}
BitWriter adaptiveOne(quint32 command) { BitWriter w; w.bits("0").put(command, 3).bits("0"); return w; }
quint32 rgbPx(int r, int g, int b) { return 0xff000000u | quint32(r) << 16 | quint32(g) << 8 | quint32(b); }
bool tileIs(const QImage &f, int x, int y, quint32 px) {
    for (int i = 0; i < 64; ++i) if (reinterpret_cast<const quint32 *>(f.constScanLine(y + i / 8))[x + i % 8] != px) return false;
    return true;
}
void adaptiveSetup(AppleAdaptive &d, QImage &f, int w, int h) {
    f = QImage(w, h, QImage::Format_RGB32); f.fill(0xff000000u); d.resize(w, h);
    bool drew; QString err; QVERIFY(d.decode(f, 0, 0, 0, 0, adaptiveTables(), &drew, &err)); QVERIFY(!drew);
}
} // namespace

class TestVault : public QObject
{
    Q_OBJECT
private slots:

    void adaptiveWhiteAndClipping()
    {
        AppleAdaptive d; QImage f; adaptiveSetup(d, f, 24, 16);
        BitWriter cmd; cmd.bits("0").put(0, 3).bits("1 0011").put(0, 3).bits("0");
        BitWriter data; bool drew; QString err;
        QVERIFY2(d.decode(f, 0, 0, 20, 12, adaptiveFull(cmd, data), &drew, &err), qPrintable(err));
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 24; ++x)
            QCOMPARE(reinterpret_cast<const quint32 *>(f.constScanLine(y))[x], (x < 20 && y < 12) ? 0xffffffffu : 0xff000000u);
    }
    void adaptiveMaskAndColours()
    {
        AppleAdaptive d; QImage f; adaptiveSetup(d, f, 32, 8);
        BitWriter cmd; cmd.bits("0").put(4, 3).bits("1 0010");
        BitWriter data;
        data.bits("0 0").put(76, 8).put(32, 6).put(32, 6);
        data.bits("1 0").put(76, 8).put(21, 6).put(63, 6).put(29, 8).put(63, 6).put(26, 6);
        data.put(0, 8); for (int i = 0; i < 8; ++i) data.put(0xf0, 8);
        data.bits("0 1").bits("1 1").put(0xff, 8);
        bool drew; QString err;
        QVERIFY2(d.decode(f, 0, 0, 32, 8, adaptiveFull(cmd, data), &drew, &err), qPrintable(err));
        QVERIFY(tileIs(f, 0, 0, rgbPx(76, 76, 76)));
        const quint32 *row = reinterpret_cast<const quint32 *>(f.constScanLine(0));
        QCOMPARE(row[8], row[11]); QCOMPARE(row[12], row[15]); QVERIFY(row[8] != row[12]);
        QVERIFY(tileIs(f, 16, 0, rgbPx(76, 76, 76)));
        QVERIFY(tileIs(f, 24, 0, row[8]));   // a row whose bit is set is the first colour
    }
    void adaptiveCopiesAndBlackWhite()
    {
        AppleAdaptive d; QImage f; adaptiveSetup(d, f, 16, 16);
        BitWriter cmd; cmd.bits("0").put(0, 3).bits("0").put(1, 3).bits("0").put(2, 3).bits("0").put(3, 3).bits("0");
        BitWriter data; data.put(0, 8); for (int i = 0; i < 8; ++i) data.put(0, 8);
        bool drew; QString err;
        QVERIFY2(d.decode(f, 0, 0, 16, 16, adaptiveFull(cmd, data), &drew, &err), qPrintable(err));
        QVERIFY(tileIs(f, 8, 0, 0xffffffffu)); QVERIFY(tileIs(f, 0, 8, 0xffffffffu)); QVERIFY(tileIs(f, 8, 8, 0xff000000u));
    }
    void adaptiveDctRefineAndCache()
    {
        AppleAdaptive d; QImage f; adaptiveSetup(d, f, 16, 8);
        auto flat = [](BitWriter &w) { w.bits("0 0 0").bits("00 00").bits("1110 000 0").bits("0 01 0"); };
        BitWriter cmd; cmd.bits("0").put(5, 3).bits("0").put(0, 3).bits("0");
        BitWriter data; flat(data);
        bool drew; QString err;
        QVERIFY2(d.decode(f, 0, 0, 16, 8, adaptiveFull(cmd, data), &drew, &err), qPrintable(err));
        QVERIFY(tileIs(f, 0, 0, rgbPx(127, 127, 127)));
        BitWriter ref; ref.bits("01").put(0, 6).bits("0 1010").bits("0 1010").put(0x6d, 8).put(0x76, 8).put(0x73, 8);
        QByteArray body; body.append(char(1)).append(char(14)).append(char(19)); body += ref.done();
        QVERIFY2(d.decode(f, 0, 0, 8, 8, body, &drew, &err), qPrintable(err));
        QVERIFY(tileIs(f, 0, 0, rgbPx(127, 127, 127))); QVERIFY(tileIs(f, 8, 0, 0xffffffffu));
        BitWriter later = adaptiveOne(7); BitWriter none;
        QVERIFY2(d.decode(f, 8, 0, 8, 8, adaptiveFull(later, none), &drew, &err), qPrintable(err));
        QVERIFY(tileIs(f, 8, 0, rgbPx(127, 127, 127)));
    }
    // Speed on a Retina-sized picture of DCT tiles with Y detail (logged, not asserted).
    void adaptiveSpeed()
    {
        const int W = 3456, H = 2234, tiles = ((W + 7) / 8) * ((H + 7) / 8);
        AppleAdaptive d; QImage f; adaptiveSetup(d, f, W, H);
        BitWriter cmd; cmd.bits("0").put(5, 3).bits("1").put(15, 4);
        const int v = tiles - 17;
        cmd.put(0x80 | (v & 0x7f), 8).put(0x80 | ((v >> 7) & 0x7f), 8).put(v >> 14, 8);
        BitWriter data;
        for (int i = 0; i < tiles; ++i)   // chroma DCs, Y DC, a few AC values, end of block
            data.bits("0 0 0").bits("00 00").bits("1110 000 0").bits("0 10").bits("0 11").bits("0 10").bits("0 01 0");
        const QByteArray body = adaptiveFull(cmd, data);
        bool drew; QString err;
        QElapsedTimer t; t.start();
        const int runs = 5;
        for (int i = 0; i < runs; ++i)
            QVERIFY2(d.decode(f, 0, 0, W, H, body, &drew, &err), qPrintable(err));
        qInfo("adaptive: %.1f ms per %dx%d full update (%d tiles)", double(t.nsecsElapsed()) / 1e6 / runs, W, H, tiles);
    }
    void adaptiveRefusesBadInput()
    {
        AppleAdaptive d; QImage f(8, 8, QImage::Format_RGB32); f.fill(0xff000000u); d.resize(8, 8);
        BitWriter c0 = adaptiveOne(0); BitWriter n0; const QByteArray body = adaptiveFull(c0, n0);
        bool drew; QString err;
        QVERIFY(!d.decode(f, 0, 0, 8, 8, body, &drew, &err));   // no tables yet
        QVERIFY(d.decode(f, 0, 0, 0, 0, adaptiveTables(), &drew, &err));
        QVERIFY(!d.decode(f, 0, 0, 16, 8, body, &drew, &err));  // outside
        QByteArray cut = body; cut[cut.size() - 1] = 0;
        QVERIFY(!d.decode(f, 0, 0, 8, 8, cut, &drew, &err));    // no end mark
        QVERIFY(d.decode(f, 0, 0, 8, 8, body, &drew, &err));
        QVERIFY(tileIs(f, 0, 0, 0xffffffffu));
    }
    // VNC password response (DES, key bits reversed); expected value from `openssl enc -des-ecb`.
    void vncAuthResponse()
    {
        QByteArray challenge;
        for (int i = 0; i < 16; ++i)
            challenge.append(char(i));
        QCOMPARE(VncSession::vncAuthResponse(challenge, QStringLiteral("testpass")).toHex(),
                 QByteArray("cb15988006ca0c81bcf3fdefab2c0c58"));
    }
    void vncKeysyms()
    {
        QCOMPARE(VncSession::keysymForScancode(0x1e), quint32('a'));
        QCOMPARE(VncSession::keysymForScancode(0x1c), quint32(0xff0d));
        QCOMPARE(VncSession::keysymForScancode(0x15b), quint32(0xffeb));
        QCOMPARE(VncSession::keysymForScancode(0x3b), quint32(0xffbe));
        QCOMPARE(VncSession::keysymForScancode(0x58), quint32(0xffc9));
    }
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        qputenv("BURROW_VAULT", m_dir.filePath("vault.json").toLocal8Bit());
    }

    void createSaveReopen()
    {
        VaultStore s;
        QVERIFY(!s.exists());
        QCOMPARE(s.create("correct horse battery"), VaultStore::Result::Ok);

        Model::Tunnel t;
        t.id = Model::newId();
        t.name = "Work VPN";
        t.gpHost = "vpn.example.com";
        t.gpUsername = "alice";
        t.gpSavePassword = false;
        t.gpPassword = "must-not-be-saved";
        t.otpEnabled = true;
        t.totp.secret = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";
        t.totp.algorithm = "SHA512";
        s.doc().tunnels.append(t);

        Model::Device d;
        d.id = Model::newId();
        d.name = "Office PC";
        d.host = "10.0.0.5";
        d.savePassword = true;
        d.password = "rdp-secret";
        d.tunnelId = t.id;
        d.os = "ubuntu";
        s.doc().devices.append(d);
        QCOMPARE(s.save(), VaultStore::Result::Ok);
        s.lock();
        QVERIFY(s.doc().devices.isEmpty());

        QFile f(s.path());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray raw = f.readAll();
        QVERIFY(!raw.contains("rdp-secret"));
        QVERIFY(!raw.contains("Office PC"));

        QCOMPARE(s.open("wrong password"), VaultStore::Result::WrongPassword);
        QCOMPARE(s.open("correct horse battery"), VaultStore::Result::Ok);
        QCOMPARE(s.doc().devices.size(), 1);
        QCOMPARE(s.doc().devices[0].password, QString("rdp-secret"));
        QCOMPARE(s.doc().devices[0].os, QString("ubuntu"));
        QCOMPARE(s.doc().tunnels[0].gpPassword, QString()); // savePassword off
        QCOMPARE(s.doc().tunnels[0].totp.algorithm, QString("SHA512"));
        QVERIFY(s.doc().tunnel(d.tunnelId));
    }

    void changePasswordAndKey()
    {
        VaultStore s;
        QCOMPARE(s.open("correct horse battery"), VaultStore::Result::Ok);
        const QByteArray key = s.exportKey();
        QVERIFY(!key.isEmpty());
        QVERIFY(s.verifyPassword("correct horse battery"));
        QVERIFY(!s.verifyPassword("nope"));
        QCOMPARE(s.changePassword("new password 123"), VaultStore::Result::Ok);
        s.lock();
        QCOMPARE(s.open("correct horse battery"), VaultStore::Result::WrongPassword);
        // Password change rotates the data key, so the old key is useless.
        QCOMPARE(s.openWithKey(key), VaultStore::Result::WrongPassword);
        QCOMPARE(s.open("new password 123"), VaultStore::Result::Ok);
        QCOMPARE(s.doc().devices.size(), 1);
        const QByteArray newKey = s.exportKey();
        s.lock();
        QCOMPARE(s.openWithKey(newKey), VaultStore::Result::Ok);
    }

    void totpHelpers()
    {
        const auto accts = NetCore::parseTotp(
            "otpauth://totp/ACME:alice?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ&algorithm=SHA512&digits=6&period=30");
        QCOMPARE(accts.size(), 1);
        QCOMPARE(accts[0].algorithm, QString("SHA512"));
        QCOMPARE(accts[0].account, QString("alice"));
        int left = 0;
        const QString code = NetCore::totpCode(accts[0], &left);
        QCOMPARE(code.size(), 6);
        QVERIFY(left > 0 && left <= 30);
        QVERIFY(NetCore::parseTotp("https://example.com").isEmpty());
    }

    void versionCompare()
    {
        QCOMPARE(Updater::compareVersions("1.10.0", "1.9.3"), 1);
        QCOMPARE(Updater::compareVersions("0.2.0", "0.2.0"), 0);
        QCOMPARE(Updater::compareVersions("v0.1.9", "0.2"), -1);
#if defined(Q_OS_MACOS)
        QVERIFY(Updater::platformKey().startsWith("mac-"));
#elif defined(Q_OS_WIN)
        QCOMPARE(Updater::platformKey(), QStringLiteral("windows-x64"));
#else
        QCOMPARE(Updater::platformKey(), QStringLiteral("linux-x64"));
#endif
    }

    void localSocketPair()
    {
        // The pair that carries RDP/VNC to netcore: bytes go both ways and a close reads as end of stream.
        int fds[2];
        QVERIFY(localsocket::pair(fds));
        QCOMPARE(localsocket::sendSome(fds[0], "ping", 4), 4L);
        QVERIFY(localsocket::waitReadable(fds[1], 2000) > 0);
        char buf[8] = {};
        QCOMPARE(localsocket::receive(fds[1], buf, sizeof buf), 4L);
        QCOMPARE(QByteArray(buf, 4), QByteArray("ping"));
        QCOMPARE(localsocket::sendSome(fds[1], "pong", 4), 4L);
        QVERIFY(localsocket::waitReadable(fds[0], 2000) > 0);
        QCOMPARE(localsocket::receive(fds[0], buf, sizeof buf), 4L);
        QCOMPARE(localsocket::waitReadable(fds[0], 50), 0);
        localsocket::close(fds[1]);
        QVERIFY(localsocket::waitReadable(fds[0], 2000) > 0);
        QCOMPARE(localsocket::receive(fds[0], buf, sizeof buf), 0L);
        // Writing to a closed peer fails instead of killing the process (SIGPIPE).
        long sent = 0;
        for (int i = 0; i < 100 && sent >= 0; ++i)
            sent = localsocket::sendSome(fds[0], "x", 1);
        QVERIFY(sent < 0);
        localsocket::close(fds[0]);
    }

    void updaterFlow()
    {
        // Fake release: manifest + asset in a temp dir, served over file://.
        const QByteArray payload = "fake zip bytes";
        const QString platform = Updater::platformKey();
        const QString assetName = Updater::assetName("9.9.9");
        QCOMPARE(assetName, QString("Burrow-9.9.9-" + platform
#ifdef Q_OS_WIN
                                    + "-setup.exe"
#else
                                    + ".zip"
#endif
                                    ));
        QFile asset(m_dir.filePath(assetName));
        QVERIFY(asset.open(QIODevice::WriteOnly));
        asset.write(payload);
        asset.close();
        auto writeManifest = [&](const QByteArray &sha) {
            QFile m(m_dir.filePath("latest.json"));
            QVERIFY(m.open(QIODevice::WriteOnly));
            m.write("{\"version\":\"9.9.9\",\"notes\":\"- New\",\"assets\":{\"" + platform.toUtf8()
                    + "\":{\"name\":\"" + assetName.toUtf8() + "\",\"sha256\":\"" + sha + "\"}}}");
        };
        writeManifest(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
        qputenv("BURROW_UPDATE_URL", QUrl::fromLocalFile(m_dir.filePath("latest.json")).toString().toUtf8());

        Updater u;
        u.checkNow();
        QTRY_COMPARE(u.state(), QString("available"));
        QCOMPARE(u.latestVersion(), QString("9.9.9"));
        u.download();
        QTRY_COMPARE(u.state(), QString("ready"));

        // A tampered asset is rejected.
        writeManifest(QByteArray(64, 'a'));
        Updater bad;
        bad.checkNow();
        QTRY_COMPARE(bad.state(), QString("available"));
        bad.download();
        QTRY_COMPARE(bad.state(), QString("error"));

        // A manifest that names an unexpected file (e.g. a path) is refused before anything is downloaded.
        {
            QFile m(m_dir.filePath("latest.json"));
            QVERIFY(m.open(QIODevice::WriteOnly));
            m.write("{\"version\":\"9.9.9\",\"assets\":{\"" + platform.toUtf8()
                    + "\":{\"name\":\"../../evil.zip\",\"sha256\":\"" + QByteArray(64, 'a') + "\"}}}");
        }
        Updater evil;
        evil.checkNow();
        QTRY_COMPARE(evil.state(), QString("error"));
        qunsetenv("BURROW_UPDATE_URL");
    }

    void keyMapper()
    {
        using E = KeyMapper::Event;
        const unsigned C = 0x2E, Q = 0x10, Cmd = KeyMapper::LWin, Ctrl = KeyMapper::LCtrl;

        // Shortcuts: Cmd+C -> Ctrl+C
        KeyMapper k(KeyMapper::Mode::Shortcuts);
        QVERIFY(k.map(Cmd, true).isEmpty());
        QCOMPARE(k.map(C, true), (QList<E>{{Ctrl, true}, {C, true}}));
        QCOMPARE(k.map(C, false), (QList<E>{{C, false}}));
        QCOMPARE(k.map(Cmd, false), (QList<E>{{Ctrl, false}}));
        // Cmd + non-shortcut key stays Win+key
        QVERIFY(k.map(Cmd, true).isEmpty());
        QCOMPARE(k.map(Q, true), (QList<E>{{Cmd, true}, {Q, true}}));
        k.map(Q, false);
        QCOMPARE(k.map(Cmd, false), (QList<E>{{Cmd, false}}));
        // Cmd tapped alone -> Win tap
        k.map(Cmd, true);
        QCOMPARE(k.map(Cmd, false), (QList<E>{{Cmd, true}, {Cmd, false}}));
        // Losing focus releases a substituted Ctrl
        k.map(Cmd, true);
        k.map(C, true);
        QCOMPARE(k.releaseAll(), (QList<E>{{Ctrl, false}}));

        KeyMapper swap(KeyMapper::Mode::Swap);
        QCOMPARE(swap.map(Cmd, true), (QList<E>{{Ctrl, true}}));
        QCOMPARE(swap.map(Ctrl, true), (QList<E>{{Cmd, true}}));

        KeyMapper off(KeyMapper::Mode::Off);
        QCOMPARE(off.map(Cmd, true), (QList<E>{{Cmd, true}}));
    }

    void clipboardFileList()
    {
        // Remote names: backslashes become folders; anything leaving the target folder is refused.
        QCOMPARE(RdpClipboard::safeRelativePath(QStringLiteral("dir\\a.txt")), QStringLiteral("dir/a.txt"));
        QCOMPARE(RdpClipboard::safeRelativePath(QStringLiteral("a.txt")), QStringLiteral("a.txt"));
        QVERIFY(RdpClipboard::safeRelativePath(QStringLiteral("..\\x")).isEmpty());
        QVERIFY(RdpClipboard::safeRelativePath(QStringLiteral("dir/../../x")).isEmpty());
        QVERIFY(RdpClipboard::safeRelativePath(QStringLiteral("/etc/passwd")).isEmpty());
        QVERIFY(RdpClipboard::safeRelativePath(QStringLiteral("C:\\x")).isEmpty());

        auto descriptor = [](const QString &name, quint32 flags, quint32 attrs, quint64 size) {
            QByteArray d(592, '\0');
            auto *p = reinterpret_cast<uchar *>(d.data());
            qToLittleEndian<quint32>(flags, p);
            qToLittleEndian<quint32>(attrs, p + 36);
            qToLittleEndian<quint32>(quint32(size >> 32), p + 64);
            qToLittleEndian<quint32>(quint32(size), p + 68);
            const std::u16string n = name.toStdU16String();
            for (size_t i = 0; i < n.size() && i < 259; ++i)
                qToLittleEndian<quint16>(n[i], p + 72 + i * 2);
            return d;
        };
        QByteArray list(4, '\0');
        qToLittleEndian<quint32>(2u, reinterpret_cast<uchar *>(list.data()));
        list += descriptor(QStringLiteral("Photos"), 0x04 | 0x40, 0x10, 0);
        list += descriptor(QStringLiteral("Photos\\big.raw"), 0x04 | 0x40, 0x20, 0x100000005ull);
        QList<RdpClipboard::RemoteFile> files;
        QVERIFY(RdpClipboard::parseFileList(list, &files));
        QCOMPARE(files.size(), 2);
        QVERIFY(files[0].dir);
        QCOMPARE(files[1].path, QStringLiteral("Photos/big.raw"));
        QVERIFY(files[1].hasSize);
        QCOMPARE(files[1].size, 0x100000005ull);
        // Truncated lists, absurd counts and unsafe names are rejected as a whole.
        QVERIFY(!RdpClipboard::parseFileList(list.left(600), &files));
        QByteArray huge(4, '\0');
        qToLittleEndian<quint32>(0xFFFFFFFFu, reinterpret_cast<uchar *>(huge.data()));
        QVERIFY(!RdpClipboard::parseFileList(huge, &files));
        QByteArray evil(4, '\0');
        qToLittleEndian<quint32>(1u, reinterpret_cast<uchar *>(evil.data()));
        evil += descriptor(QStringLiteral("..\\..\\.zshrc"), 0x40, 0, 3);
        QVERIFY(!RdpClipboard::parseFileList(evil, &files));

        // Local → remote: a copied folder is expanded parents first and survives the round trip.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QDir(tmp.path()).mkpath(QStringLiteral("Report/img"));
        auto write = [&](const QString &rel, const QByteArray &bytes) {
            QFile f(tmp.path() + QLatin1Char('/') + rel);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(bytes);
        };
        write(QStringLiteral("Report/img/a.png"), QByteArray(10, 'a'));
        write(QStringLiteral("Report/notes.txt"), QByteArray(3, 'n'));
        QList<RdpClipboard::LocalFile> local;
        QVERIFY(RdpClipboard::collectLocalFiles({ QUrl::fromLocalFile(tmp.path() + QStringLiteral("/Report")) }, &local));
        QCOMPARE(local.size(), 4);
        QCOMPARE(local[0].remoteName, QStringLiteral("Report"));
        QVERIFY(local[0].dir);
        QList<RdpClipboard::RemoteFile> back;
        QVERIFY(RdpClipboard::parseFileList(RdpClipboard::buildFileList(local), &back));
        QCOMPARE(back.size(), 4);
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(back[i].path, QString(local[i].remoteName).replace(QLatin1Char('\\'), QLatin1Char('/')));
            QCOMPARE(back[i].dir, local[i].dir);
            QCOMPARE(back[i].size, local[i].size);
        }
    }

    void clipboardDib()
    {
        QImage img(37, 21, QImage::Format_RGB32);   // odd sizes exercise row padding
        img.fill(QColor(10, 120, 200));
        img.setPixelColor(3, 4, QColor(250, 20, 30));
        const QByteArray dib = RdpClipboard::imageToDib(img);
        QVERIFY(dib.size() > 40);
        const QImage back = RdpClipboard::dibToImage(dib);
        QCOMPARE(back.size(), img.size());
        QCOMPARE(back.pixelColor(3, 4), QColor(250, 20, 30));
        QCOMPARE(back.pixelColor(0, 0), QColor(10, 120, 200));
        QVERIFY(RdpClipboard::dibToImage(QByteArray(10, 'x')).isNull());
        // Hostile headers are rejected instead of being handed to the decoder.
        QByteArray bad = dib;
        qToLittleEndian<quint32>(0xFFFFFFF0u, reinterpret_cast<uchar *>(bad.data()) + 32);   // huge colour count
        QVERIFY(RdpClipboard::dibToImage(bad).isNull());
        bad = dib;
        qToLittleEndian<quint32>(1u, reinterpret_cast<uchar *>(bad.data()) + 16);            // BI_RLE8
        QVERIFY(RdpClipboard::dibToImage(bad).isNull());
        bad = dib.left(60);                                                                    // truncated pixels
        QVERIFY(RdpClipboard::dibToImage(bad).isNull());
        QCOMPARE(RdpClipboard::modeFromString("to_local"), RdpClipboard::Mode::ToLocal);
    }

    void qrFromImage()
    {
        // Fixture: a QR code (with margin and an odd width) holding a demo otpauth link (RFC example key).
        const QImage img(QStringLiteral(BURROW_TEST_DATA "/qr-fixture.png"));
        QVERIFY(!img.isNull());
        AppController app;
        const QString text = app.decodeQrImage(QVariant::fromValue(img));
        QVERIFY(text.startsWith("otpauth://totp/Example:demo"));
        QVERIFY(app.decodeQrImage(QVariant::fromValue(QImage(50, 50, QImage::Format_RGB32))).isEmpty());
    }

private:
    QTemporaryDir m_dir;
};

QTEST_MAIN(TestVault)
#include "tst_vault.moc"
