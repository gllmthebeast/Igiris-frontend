// Fenêtre racine — lot 0 : volontairement vide.
//
// La liste de jeux arrive au lot 5. Ce fichier n'existe que pour prouver que la chaîne
// Qt/QML est fonctionnelle de bout en bout : compilation, module QML, ressource, affichage.

import QtQuick

Window {
    id: root

    width: 1280
    height: 720
    visible: true

    // Pas de titre de distribution ici, ni nulle part hors de l'adaptateur (§1).
    title: qsTr("igiris")

    color: "#101014"

    Text {
        anchors.centerIn: parent
        text: qsTr("igiris-frontend")
        color: "#e8e8ef"
        font.pixelSize: 32
    }
}
