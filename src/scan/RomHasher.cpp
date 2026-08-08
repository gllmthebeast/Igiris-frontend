#include "scan/RomHasher.h"

#include <zlib.h>

#include <QFile>

namespace igiris::scan {

namespace {

constexpr qint64 kChunk = 64 * 1024;

// Signatures du format ZIP.
constexpr quint32 kEndOfCentralDir    = 0x06054b50;
constexpr quint32 kCentralFileHeader  = 0x02014b50;
constexpr quint32 kLocalFileHeader    = 0x04034b50;

QString formatCrc(quint32 crc)
{
    return QStringLiteral("%1").arg(crc, 8, 16, QLatin1Char('0')).toUpper();
}

quint16 readU16(const QByteArray &data, int offset)
{
    return static_cast<quint8>(data.at(offset))
         | (static_cast<quint16>(static_cast<quint8>(data.at(offset + 1))) << 8);
}

quint32 readU32(const QByteArray &data, int offset)
{
    return static_cast<quint8>(data.at(offset))
         | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 1))) << 8)
         | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 2))) << 16)
         | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 3))) << 24);
}

} // namespace

bool looksLikeSmcHeader(quint64 size)
{
    return size > 512 && (size % 1024) == 512;
}

HashResult crc32OfFile(const QString &path, qint64 skipBytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return HashResult::failure(
            QStringLiteral("lecture de %1 impossible : %2").arg(path, file.errorString()));

    if (skipBytes > 0) {
        if (skipBytes >= file.size())
            return HashResult::failure(
                QStringLiteral("%1 est plus court que son en-tête supposé (%2 octets)")
                    .arg(path)
                    .arg(skipBytes));
        if (!file.seek(skipBytes))
            return HashResult::failure(
                QStringLiteral("positionnement à %1 impossible dans %2").arg(skipBytes).arg(path));
    }

    uLong      crc = crc32(0L, nullptr, 0);
    QByteArray buffer;
    while (!file.atEnd()) {
        buffer = file.read(kChunk);
        if (buffer.isEmpty() && file.error() != QFileDevice::NoError)
            return HashResult::failure(
                QStringLiteral("lecture de %1 interrompue : %2").arg(path, file.errorString()));
        crc = crc32(crc, reinterpret_cast<const Bytef *>(buffer.constData()),
                    static_cast<uInt>(buffer.size()));
    }

    return HashResult{ formatCrc(static_cast<quint32>(crc)), true, QString() };
}

QList<ZipEntry> readZipEntries(const QString &path, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return QList<ZipEntry>{};
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("ouverture de %1 impossible : %2").arg(path, file.errorString()));

    // L'annuaire central se trouve en FIN de fichier, précédé d'un enregistrement EOCD
    // qu'on cherche à rebours. Un commentaire d'archive peut le repousser de 64 Ko au plus.
    const qint64 size     = file.size();
    const qint64 tailSize = qMin<qint64>(size, 65557);
    if (!file.seek(size - tailSize))
        return fail(QStringLiteral("%1 : fin de fichier illisible").arg(path));

    const QByteArray tail = file.read(tailSize);
    int              eocd = -1;
    for (int i = tail.size() - 22; i >= 0; --i) {
        if (readU32(tail, i) == kEndOfCentralDir) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0)
        return fail(QStringLiteral("%1 n'est pas une archive zip (EOCD absent)").arg(path));

    const quint16 entryCount     = readU16(tail, eocd + 10);
    const quint32 directorySize  = readU32(tail, eocd + 12);
    const quint32 directoryStart = readU32(tail, eocd + 16);

    // Zip64 : plutôt que de mal interpréter, on refuse explicitement. Une archive de ROM
    // de plus de 4 Go est assez improbable pour que le silence soit le vrai danger.
    if (entryCount == 0xFFFF || directorySize == 0xFFFFFFFF || directoryStart == 0xFFFFFFFF)
        return fail(QStringLiteral("%1 est une archive Zip64, non gérée").arg(path));

    if (!file.seek(directoryStart))
        return fail(QStringLiteral("%1 : annuaire central inatteignable").arg(path));

    const QByteArray directory = file.read(directorySize);
    QList<ZipEntry>  entries;
    int              offset = 0;

    for (int i = 0; i < entryCount; ++i) {
        if (offset + 46 > directory.size() || readU32(directory, offset) != kCentralFileHeader)
            return fail(QStringLiteral("%1 : annuaire central corrompu à l'entrée %2")
                            .arg(path)
                            .arg(i));

        ZipEntry entry;
        entry.method            = readU16(directory, offset + 10);
        entry.crc32             = readU32(directory, offset + 16);
        entry.compressedSize    = readU32(directory, offset + 20);
        entry.uncompressedSize  = readU32(directory, offset + 24);
        const quint16 nameLen   = readU16(directory, offset + 28);
        const quint16 extraLen  = readU16(directory, offset + 30);
        const quint16 commentLen = readU16(directory, offset + 32);
        entry.localHeaderOffset = readU32(directory, offset + 42);
        entry.name = QString::fromUtf8(directory.mid(offset + 46, nameLen));

        if (!entry.name.endsWith(QLatin1Char('/'))) // les dossiers ne nous intéressent pas
            entries.append(entry);

        offset += 46 + nameLen + extraLen + commentLen;
    }

    if (error)
        error->clear();
    return entries;
}

HashResult crc32OfZipEntry(const QString &path, const ZipEntry &entry, qint64 skipBytes)
{
    if (skipBytes <= 0)
        return HashResult{ formatCrc(entry.crc32), true, QString() };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return HashResult::failure(
            QStringLiteral("ouverture de %1 impossible : %2").arg(path, file.errorString()));

    // Les longueurs de nom et d'extra de l'en-tête LOCAL peuvent différer de celles de
    // l'annuaire central : il faut les relire ici, sinon on démarre au mauvais octet.
    if (!file.seek(entry.localHeaderOffset))
        return HashResult::failure(QStringLiteral("%1 : en-tête local inatteignable").arg(path));

    const QByteArray local = file.read(30);
    if (local.size() < 30 || readU32(local, 0) != kLocalFileHeader)
        return HashResult::failure(QStringLiteral("%1 : en-tête local invalide").arg(path));

    const qint64 dataStart = entry.localHeaderOffset + 30 + readU16(local, 26) + readU16(local, 28);
    if (!file.seek(dataStart))
        return HashResult::failure(QStringLiteral("%1 : données de l'entrée inatteignables").arg(path));

    uLong  crc       = crc32(0L, nullptr, 0);
    qint64 remaining = static_cast<qint64>(skipBytes);

    const auto consume = [&](const char *data, qint64 length) {
        // On ignore les `skipBytes` premiers octets du contenu DÉCOMPRESSÉ.
        if (remaining > 0) {
            const qint64 dropped = qMin(remaining, length);
            data += dropped;
            length -= dropped;
            remaining -= dropped;
        }
        if (length > 0)
            crc = crc32(crc, reinterpret_cast<const Bytef *>(data), static_cast<uInt>(length));
    };

    if (entry.method == 0) { // stocké : rien à décompresser
        qint64 left = static_cast<qint64>(entry.uncompressedSize);
        while (left > 0) {
            const QByteArray chunk = file.read(qMin(left, kChunk));
            if (chunk.isEmpty())
                return HashResult::failure(QStringLiteral("%1 : lecture tronquée").arg(path));
            consume(chunk.constData(), chunk.size());
            left -= chunk.size();
        }
        return HashResult{ formatCrc(static_cast<quint32>(crc)), true, QString() };
    }

    if (entry.method != 8)
        return HashResult::failure(QStringLiteral("%1 : méthode de compression %2 non gérée")
                                       .arg(path)
                                       .arg(entry.method));

    // Deflate brut : pas d'en-tête zlib, d'où windowBits négatif.
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        return HashResult::failure(QStringLiteral("%1 : initialisation zlib refusée").arg(path));

    QByteArray out(kChunk, Qt::Uninitialized);
    qint64     left = static_cast<qint64>(entry.compressedSize);
    int        rc   = Z_OK;

    while (rc != Z_STREAM_END) {
        const QByteArray in = file.read(qMin(left, kChunk));
        if (in.isEmpty())
            break;
        left -= in.size();

        stream.next_in  = reinterpret_cast<Bytef *>(const_cast<char *>(in.constData()));
        stream.avail_in = static_cast<uInt>(in.size());

        do {
            stream.next_out  = reinterpret_cast<Bytef *>(out.data());
            stream.avail_out = static_cast<uInt>(out.size());
            rc               = inflate(&stream, Z_NO_FLUSH);
            if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) {
                inflateEnd(&stream);
                return HashResult::failure(QStringLiteral("%1 : décompression échouée (%2)")
                                               .arg(path)
                                               .arg(rc));
            }
            consume(out.constData(), out.size() - stream.avail_out);
        } while (stream.avail_out == 0 && rc != Z_STREAM_END);
    }

    inflateEnd(&stream);
    return HashResult{ formatCrc(static_cast<quint32>(crc)), true, QString() };
}

} // namespace igiris::scan
