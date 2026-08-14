// La jaquette d'un jeu — CLAUDE.md §6 et §11.
//
// ⚠️ C'est la SEULE donnée du projet qui exige le réseau. Tout le reste est hors ligne par
// construction : le catalogue est un fichier local, le scan est local, les badges sont
// précalculés. `cover_ref` est une URL, et le §11 la désigne comme « la seule entorse ».
//
// Trois conséquences, toutes visibles ici :
//
//   1. l'image peut ne JAMAIS arriver — appareil sans réseau, URL morte. L'emplacement
//      reste alors occupé par un cadre discret : une ligne ne doit pas changer de
//      géométrie selon qu'une requête aboutit ou non ;
//   2. le chargement est ASYNCHRONE, sinon le défilement se bloque sur chaque requête ;
//   3. sourceSize borne la décompression. Sans elle, Qt décode l'image à sa taille native
//      et garde ça en mémoire — sur un Raspberry Pi, une liste de 7 581 entrées suffit à
//      épuiser la RAM disponible.

import QtQuick

Item {
    id: cover

    required property string source
    property bool enabled: true
    property color frameColor: "#2a2a36"
    property color dimColor: "#8a8a9a"

    // Le cadre est TOUJOURS dessiné, chargée ou non : c'est lui qui réserve la place.
    Rectangle {
        anchors.fill: parent
        color: "#16161d"
        radius: 3
        border.width: 1
        border.color: cover.frameColor
        visible: image.status !== Image.Ready
    }

    // Marque discrète quand il n'y aura pas d'image — pas un message d'erreur : une
    // jaquette absente est une information mineure, pas un incident.
    Text {
        anchors.centerIn: parent
        visible: image.status !== Image.Ready
        text: cover.enabled ? "…" : "▯"
        color: cover.dimColor
        font.pixelSize: 13
    }

    Image {
        id: image

        anchors.fill: parent
        // Vide quand les jaquettes sont coupées : aucune requête n'est même tentée.
        source: cover.enabled ? cover.source : ""
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        cache: true
        // Décodage borné à la taille d'affichage — voir l'en-tête.
        sourceSize.width: Math.round(width)
        sourceSize.height: Math.round(height)
        visible: status === Image.Ready
    }
}
