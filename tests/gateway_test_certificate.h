#pragma once

#include <QFile>
#include <QProcess>
#include <QStandardPaths>

namespace VaporView::Test
{
inline QString gatewayOpenSslExecutable()
{
    return qEnvironmentVariable("VAPORVIEW_TEST_OPENSSL", QStandardPaths::findExecutable(QStringLiteral("openssl")));
}

inline QString generateGatewayTestCertificate(const QString& openssl, const QString& configFile,
                                              const QString& certificateFile, const QString& privateKeyFile)
{
    // Some OpenSSL distributions require a configuration without shipping the default file.
    QFile config(configFile);
    if (!config.open(QIODevice::WriteOnly)) return config.errorString();
    const QByteArray configContents =
        "[req]\ndistinguished_name=dn\nprompt=no\nx509_extensions=cert\n"
        "[dn]\nCN=localhost\n"
        "[cert]\nbasicConstraints=critical,CA:TRUE\nkeyUsage=critical,digitalSignature,keyEncipherment,keyCertSign\n"
        "extendedKeyUsage=serverAuth\nsubjectAltName=DNS:localhost,IP:127.0.0.1,IP:::1\n";
    if (config.write(configContents) != configContents.size() || !config.flush()) return config.errorString();
    config.close();

    QProcess certificate;
    certificate.start(openssl, {"req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
        "-config", configFile, "-out", certificateFile, "-keyout", privateKeyFile});
    if (certificate.waitForFinished(10000) && certificate.exitStatus() == QProcess::NormalExit &&
        certificate.exitCode() == 0) return {};

    const auto processError = certificate.errorString();
    if (certificate.state() != QProcess::NotRunning)
    {
        certificate.kill();
        certificate.waitForFinished();
    }
    return QStringLiteral("OpenSSL exited with code %1 (%2): %3")
        .arg(certificate.exitCode()).arg(processError, QString::fromLocal8Bit(certificate.readAllStandardError()));
}
}  // namespace VaporView::Test
