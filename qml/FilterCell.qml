// Un sélecteur de filtre, pilotable à la manette — CLAUDE.md §6 et §12.
//
// Gauche / droite font défiler les valeurs, sans ouvrir de menu : un menu déroulant
// suppose un curseur, et impose une navigation à deux niveaux là où un axe suffit.

import QtQuick

FocusScope {
    id: cell

    // Libellé du filtre, et valeurs possibles. `values` contient toujours une entrée de
    // tête « pas de filtre », dont le libellé est fourni par `anyLabel`.
    property string label
    property var    values: []
    property string anyLabel: qsTr("tout")
    property int    currentIndex: 0

    // Un filtre indisponible reste VISIBLE mais grisé, et le dit. Le masquer laisserait
    // croire qu'il n'existe pas ; l'activer laisserait croire qu'il filtre (§6).
    property bool available: true
    property string unavailableHint

    property color textColor: "#e8e8ef"
    property color dimColor: "#8a8a9a"
    property color focusColor: "#2d4f7c"

    readonly property string currentValue: currentIndex === 0 || !available
                                           ? ""
                                           : String(values[currentIndex])

    implicitWidth: content.implicitWidth + 40
    implicitHeight: 44

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: cell.activeFocus ? cell.focusColor : "transparent"
        border.width: cell.activeFocus ? 0 : 1
        border.color: "#2a2a36"
    }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 10

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: cell.label
            color: cell.dimColor
            font.pixelSize: 17
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: !cell.available ? cell.unavailableHint
                                  : (cell.currentIndex === 0 ? cell.anyLabel
                                                             : String(cell.values[cell.currentIndex]))
            color: cell.available ? cell.textColor : cell.dimColor
            font.pixelSize: 18
            font.bold: cell.available && cell.currentIndex !== 0
        }

        // Les chevrons ne s'affichent que sur la cellule active : ils indiquent l'axe de
        // navigation au moment où il est utilisable.
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: cell.activeFocus && cell.available
            text: "‹ ›"
            color: cell.dimColor
            font.pixelSize: 17
        }
    }

    Keys.onLeftPressed: if (available && values.length > 0)
                            currentIndex = (currentIndex - 1 + values.length) % values.length
    Keys.onRightPressed: if (available && values.length > 0)
                             currentIndex = (currentIndex + 1) % values.length
}
