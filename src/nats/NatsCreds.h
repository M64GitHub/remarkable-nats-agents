#pragma once

#include <QByteArray>
#include <QString>

// Parses a NATS `.creds` file (a NATS USER JWT + a USER NKEY SEED) and signs the
// server `nonce` for decentralized JWT/NKEY auth (NGS / Synadia Cloud).
//
// The seed is an Ed25519 private key — a secret. It is held in memory only and
// MUST NOT be logged. Signing uses OpenSSL libcrypto (EVP_PKEY_ED25519).
class NatsCreds
{
public:
    // Load + parse. Returns true only if both a JWT and a 32-byte seed were found.
    bool loadFromFile(const QString &path);

    bool isValid() const { return !m_jwt.isEmpty() && m_seed.size() == 32; }
    QString jwt() const { return m_jwt; }

    // Ed25519-sign the raw `nonce` bytes; returns the signature base64url-encoded
    // without padding (what the NATS CONNECT `sig` field expects). Empty on error.
    QByteArray signNonce(const QByteArray &nonce) const;

private:
    static QString extractBlock(const QString &content, const QString &label);
    static QByteArray decodeSeed(const QString &seed);   // base32 -> 32-byte ed25519 seed

    QString m_jwt;
    QByteArray m_seed;   // 32-byte Ed25519 seed (secret)
};
