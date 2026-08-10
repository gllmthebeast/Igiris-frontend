// Un badge de langue — CLAUDE.md §8.
//
// C'est un BADGE DE CODE ISO 639-1, jamais un drapeau. Le §8 tranche la question et la
// raison n'est pas esthétique : l'anglais n'a pas de drapeau évident (Royaume-Uni ?
// États-Unis ?), l'espagnol non plus, l'arabe encore moins. Un code ne se trompe pas.
//
// Deux états, un seul axe (§8) :
//   ILLUMINÉ  au moins une ROM possédée fournit cette langue
//   GRISÉ     la langue existe au catalogue, aucune ROM possédée ne la fournit
// Il n'y a pas de troisième état — une langue absente du catalogue ne s'affiche pas.
//
// Pas d'atlas de sprites, et c'est un choix, pas un oubli. Le §8 le demande pour éviter
// N fichiers image par ligne ; du texte n'a pas ce coût — Qt rend les glyphes depuis un
// atlas de texture unique, ce qui est exactement la propriété recherchée. En prime, ça
// évite la question de licence que le §16 pose sur les icônes redistribuables : il n'y a
// aucun asset à redistribuer.
//
// Le grisé est obtenu par OPACITÉ sur le même élément, jamais par un second jeu de
// couleurs ou d'assets (§8).

import QtQuick

Rectangle {
    id: badge

    required property string code
    required property bool   owned

    property color accent: "#3f9d4f" // le vert du §7 : possédé, ici comme là-bas
    property color textColor: "#e8e8ef"
    property color dimColor: "#8a8a9a"

    // Taille FIXE, indépendante du code affiché : la largeur d'une ligne ne doit pas
    // dépendre de son contenu, sinon le layout se recalcule pendant le défilement (§8).
    width: 30
    height: 20
    radius: 4

    color: owned ? accent : "transparent"
    border.width: owned ? 0 : 1
    border.color: badge.dimColor
    opacity: owned ? 1.0 : 0.5

    Text {
        anchors.centerIn: parent
        text: badge.code.toUpperCase()
        color: badge.owned ? badge.textColor : badge.dimColor
        font.pixelSize: 12
        font.bold: badge.owned
    }
}
