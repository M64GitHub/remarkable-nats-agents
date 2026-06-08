#include "nats/NatsCreds.h"

#include <QFile>

#include <openssl/evp.h>

namespace {

// RFC 4648 base32 decode (uppercase, no padding) — the NKEY seed encoding.
QByteArray base32Decode(const QString &s)
{
    static const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    int rev[256];
    for (int i = 0; i < 256; ++i)
        rev[i] = -1;
    for (int i = 0; i < 32; ++i)
        rev[static_cast<unsigned char>(alphabet[i])] = i;

    QByteArray out;
    quint32 buffer = 0;
    int bits = 0;
    for (const QChar qc : s) {
        const char ch = qc.toUpper().toLatin1();
        if (ch == '=' || ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t')
            continue;
        const int v = rev[static_cast<unsigned char>(ch)];
        if (v < 0)
            return {};   // not valid base32
        buffer = (buffer << 5) | static_cast<quint32>(v);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out.append(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

}  // namespace

QString NatsCreds::extractBlock(const QString &content, const QString &label)
{
    const QString begin = QStringLiteral("-----BEGIN %1-----").arg(label);
    const QString end = QStringLiteral("-----END %1-----").arg(label);
    const int b = content.indexOf(begin);
    if (b < 0)
        return {};
    const int from = b + begin.length();
    const int e = content.indexOf(end, from);
    if (e < 0)
        return {};
    // Join the non-empty trimmed lines between the markers (the token is base64url
    // / base32 text with no internal whitespace, so concatenation is safe).
    QString out;
    const QString block = content.mid(from, e - from);
    const QList<QString> lines = block.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString t = line.trimmed();
        if (!t.isEmpty())
            out += t;
    }
    return out;
}

QByteArray NatsCreds::decodeSeed(const QString &seed)
{
    // Decoded seed layout: [prefix0, prefix1, 32-byte ed25519 seed, crc16(2)].
    const QByteArray raw = base32Decode(seed);
    if (raw.size() < 34)
        return {};
    return raw.mid(2, 32);
}

bool NatsCreds::loadFromFile(const QString &path)
{
    m_jwt.clear();
    m_seed.clear();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QString content = QString::fromUtf8(f.readAll());
    m_jwt = extractBlock(content, QStringLiteral("NATS USER JWT"));
    m_seed = decodeSeed(extractBlock(content, QStringLiteral("USER NKEY SEED")));
    return isValid();
}

QByteArray NatsCreds::signNonce(const QByteArray &nonce) const
{
    if (m_seed.size() != 32)
        return {};

    EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519, nullptr,
        reinterpret_cast<const unsigned char *>(m_seed.constData()), 32);
    if (!pkey)
        return {};

    QByteArray sig;
    if (EVP_MD_CTX *ctx = EVP_MD_CTX_new()) {
        // Ed25519 is a one-shot sign (no separate digest): pass null md.
        if (EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, pkey) == 1) {
            const auto *msg = reinterpret_cast<const unsigned char *>(nonce.constData());
            const size_t msgLen = static_cast<size_t>(nonce.size());
            size_t sigLen = 0;
            if (EVP_DigestSign(ctx, nullptr, &sigLen, msg, msgLen) == 1) {
                sig.resize(static_cast<int>(sigLen));
                if (EVP_DigestSign(ctx, reinterpret_cast<unsigned char *>(sig.data()),
                                   &sigLen, msg, msgLen) == 1)
                    sig.resize(static_cast<int>(sigLen));
                else
                    sig.clear();
            }
        }
        EVP_MD_CTX_free(ctx);
    }
    EVP_PKEY_free(pkey);

    if (sig.isEmpty())
        return {};
    return sig.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
