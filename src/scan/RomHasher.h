#pragma once

// Calcul des CRC32 des fichiers locaux — CLAUDE.md §4.
//
// Trois pièges que ce module traite, et qui produisent tous des erreurs SILENCIEUSES
// quand on les rate (rien ne plante, tout tombe en rouge à tort) :
//
//   1. les en-têtes de ROM, que les dats n'incluent pas ;
//   2. les archives : c'est le CONTENU qu'il faut hasher, jamais le zip ;
//   3. la casse du CRC, qui doit être normalisée avant tout lookup.

#include <QByteArray>
#include <QList>
#include <QString>

namespace igiris::scan {

struct HashResult {
    QString crc32; // 8 caractères, MAJUSCULES — la forme de l'export
    bool    ok = false;
    QString error; // verbatim quand ok == false (§15)

    static HashResult failure(const QString &message)
    {
        return HashResult{ QString(), false, message };
    }
};

// Une entrée d'archive, telle que décrite par l'annuaire central du zip.
struct ZipEntry {
    QString name;
    quint32 crc32            = 0; // CRC du contenu DÉCOMPRESSÉ, déjà stocké par le zip
    quint64 compressedSize   = 0;
    quint64 uncompressedSize = 0;
    quint16 method           = 0; // 0 = stocké, 8 = deflate
    quint64 localHeaderOffset = 0;
};

// CRC32 d'un fichier ordinaire, en ignorant `skipBytes` octets de tête.
HashResult crc32OfFile(const QString &path, qint64 skipBytes = 0);

// Lit l'annuaire central d'une archive. Ne décompresse RIEN.
//
// C'est le chemin rapide, et il vaut d'être compris : le zip stocke déjà le CRC32 du
// contenu décompressé de chaque entrée. Identifier une ROM zippée ne demande donc aucune
// décompression — ce qui compte sur un Raspberry Pi.
QList<ZipEntry> readZipEntries(const QString &path, QString *error);

// CRC32 du contenu d'une entrée, en ignorant `skipBytes` octets de tête.
//
// Nécessite, lui, de décompresser : le CRC stocké porte sur le contenu ENTIER. On n'y
// recourt que lorsque le CRC stocké n'a rien donné et que la plateforme est connue pour
// porter un en-tête.
HashResult crc32OfZipEntry(const QString &path, const ZipEntry &entry, qint64 skipBytes);

// Le SNES n'a PAS de header_skip dans l'export, alors que l'en-tête de copieur SMC de
// 512 octets circule largement (§4). Heuristique retenue : une taille qui dépasse un
// multiple de 1 024 d'exactement 512 octets trahit cet en-tête.
bool looksLikeSmcHeader(quint64 size);

} // namespace igiris::scan
