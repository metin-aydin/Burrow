// Windows side of the in-app updater (ADR 0010): Authenticode publisher check and the silent install.

#include "app/Updater.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

#include <windows.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

QString Updater::signerSubject(const QString &path)
{
    const std::wstring file = QDir::toNativeSeparators(path).toStdWString();
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof fileInfo;
    fileInfo.pcwszFilePath = file.c_str();
    WINTRUST_DATA data{};
    data.cbStruct = sizeof data;
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &fileInfo;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    QString subject;
    if (status == ERROR_SUCCESS) {
        if (CRYPT_PROVIDER_DATA *prov = WTHelperProvDataFromStateData(data.hWVTStateData)) {
            if (CRYPT_PROVIDER_SGNR *signer = WTHelperGetProvSignerFromChain(prov, 0, FALSE, 0)) {
                if (CRYPT_PROVIDER_CERT *cert = WTHelperGetProvCertFromChain(signer, 0)) {
                    wchar_t name[512] = {};
                    if (CertGetNameStringW(cert->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 512) > 1)
                        subject = QString::fromWCharArray(name);
                }
            }
        }
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    return subject;
}

QString Updater::installWindows()
{
    // A signed Burrow only accepts an installer from the same publisher. An unsigned one (the first
    // Windows releases) relies on the manifest's SHA-256, checked when the file arrived.
    const QString publisher = signerSubject(QCoreApplication::applicationFilePath());
    if (!publisher.isEmpty() && signerSubject(m_downloadPath) != publisher)
        return tr("The update's signature doesn't match this app; it was not installed.");

    // Private, randomly named staging copy, so nothing else swaps the file before it runs.
    QTemporaryDir stagingDir(QDir::tempPath() + QStringLiteral("/burrow-update-XXXXXX"));
    if (!stagingDir.isValid())
        return tr("Couldn't prepare the update.");
    stagingDir.setAutoRemove(false);
    const QString setup = QDir(stagingDir.path()).filePath(m_assetName);
    if (!QFile::copy(m_downloadPath, setup)) {
        QDir(stagingDir.path()).removeRecursively();
        return tr("Couldn't prepare the update.");
    }
    if (!publisher.isEmpty() && signerSubject(setup) != publisher) {
        QDir(stagingDir.path()).removeRecursively();
        return tr("The update's signature doesn't match this app; it was not installed.");
    }

    // The installer waits for this process to exit (/WAITPID), installs silently into the folder it
    // recorded at first install and starts Burrow again (/RELAUNCH); see tools/windows/burrow.nsi.
    QProcess p;
    p.setProgram(setup);
    p.setArguments({QStringLiteral("/S"), QStringLiteral("/RELAUNCH"),
                    QStringLiteral("/WAITPID=%1").arg(QCoreApplication::applicationPid())});
    if (!p.startDetached())
        return tr("Couldn't start the update.");
    return {};
}
