#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QtTest>

#include "app/Updater.h"
#include "net/NetCore.h"
#include "vault/VaultStore.h"

class TestVault : public QObject
{
    Q_OBJECT
private slots:
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
        QVERIFY(Updater::platformKey().startsWith("mac-"));
    }

    void updaterFlow()
    {
        // Fake release: manifest + asset in a temp dir, served over file://.
        const QByteArray payload = "fake zip bytes";
        const QString platform = Updater::platformKey();
        QFile asset(m_dir.filePath("Burrow-9.9.9-" + platform + ".zip"));
        QVERIFY(asset.open(QIODevice::WriteOnly));
        asset.write(payload);
        asset.close();
        auto writeManifest = [&](const QByteArray &sha) {
            QFile m(m_dir.filePath("latest.json"));
            QVERIFY(m.open(QIODevice::WriteOnly));
            m.write("{\"version\":\"9.9.9\",\"notes\":\"- New\",\"assets\":{\"" + platform.toUtf8()
                    + "\":{\"name\":\"Burrow-9.9.9-" + platform.toUtf8() + ".zip\",\"sha256\":\"" + sha + "\"}}}");
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
        qunsetenv("BURROW_UPDATE_URL");
    }

private:
    QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(TestVault)
#include "tst_vault.moc"
