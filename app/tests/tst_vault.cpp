#include <QTemporaryDir>
#include <QtTest>

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
        QCOMPARE(s.openWithKey(key), VaultStore::Result::Ok);
        QCOMPARE(s.doc().devices.size(), 1);
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

private:
    QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(TestVault)
#include "tst_vault.moc"
