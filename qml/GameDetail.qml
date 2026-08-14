// Fiche de jeu — CLAUDE.md §7.
//
// Les systèmes sur lesquels le jeu existe, avec le code couleur vert / rouge / noir, et
// le lancement depuis une ligne VERTE uniquement.

import QtQuick

FocusScope {
    id: sheet

    property color background: "#101014"
    property color surface: "#1a1a22"
    property color selection: "#2d4f7c"
    property color textColor: "#e8e8ef"
    property color dimColor: "#8a8a9a"

    signal closed()

    // Message de retour du lancement : succès comme échec, verbatim (§15).
    property string lastMessage

    // Opaque : la liste ne doit pas transparaître derrière la fiche.
    Rectangle {
        anchors.fill: parent
        color: sheet.background
    }

    // Le code couleur du §7 est DESSINÉ, pas écrit en emoji : la police du système cible
    // n'en a pas forcément, et un carré vide ne veut rien dire. C'est aussi moins coûteux
    // au défilement.
    readonly property var statusColors: ["#3a3a44", "#b03b3b", "#3f9d4f"] // noir, rouge, vert

    // ------------------------------------------------------------------ bandeau
    //
    // Un BANDEAU, pas un fond de page : une bande en haut de la fiche, qui se fond dans
    // l'arrière-plan et sous laquelle la liste des systèmes reste sur un fond neutre. Un
    // visuel qui court derrière du texte dense le rend illisible à trois mètres (§6).
    //
    // ⚠️ L'image affichée est aujourd'hui la JAQUETTE recadrée, faute de mieux : l'export
    // 1.4.0 ne fournit aucune autre image. Une jaquette est verticale et porte du texte —
    // l'étirer en bandeau donne un résultat médiocre, et c'est pourquoi le rendu est
    // volontairement discret tant que `detail.hasRealBanner` est faux. Le jour où le
    // backend livre une illustration dédiée (demande export-1.5.0), il n'y a rien à
    // redessiner : seule l'opacité change.
    Item {
        id: banner

        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 208
        clip: true

        Image {
            id: bannerImage
            anchors.fill: parent
            source: games.coversEnabled ? detail.bannerRef : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            // Décodage borné : sans cela, Qt garderait un second décodage pleine taille de
            // la même image déjà affichée en vignette.
            sourceSize.width: 720
            // Discret tant que ce n'est qu'une jaquette étirée ; franc quand ce sera une
            // vraie illustration.
            opacity: status === Image.Ready ? (detail.hasRealBanner ? 0.55 : 0.22) : 0
            Behavior on opacity { NumberAnimation { duration: 200 } }
        }

        // Fondu vers le fond de la fiche : le bandeau ne doit pas se terminer par une
        // arête nette, et le texte posé dessus doit garder son contraste.
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0.06, 0.06, 0.08, 0.60) }
                GradientStop { position: 0.55; color: Qt.rgba(0.06, 0.06, 0.08, 0.80) }
                GradientStop { position: 1.0; color: sheet.background }
            }
        }
    }

    // §7 : « Jaquette, métadonnées, et la liste des systèmes ».
    GameCover {
        id: coverArt
        anchors { top: parent.top; topMargin: 34; left: parent.left; leftMargin: 32 }
        width: 105
        height: 140
        source: detail.coverRef
        enabled: games.coversEnabled
        dimColor: sheet.dimColor
    }

    Text {
        id: title
        anchors { top: parent.top; topMargin: 40; left: coverArt.right; leftMargin: 24
                  right: parent.right; rightMargin: 32 }
        text: detail.title
        color: sheet.textColor
        font.pixelSize: 32
        elide: Text.ElideRight
    }

    // Métadonnées du §7. Elles existaient dans l'export sans être affichées nulle part.
    Row {
        id: meta
        anchors { top: title.bottom; topMargin: 10; left: coverArt.right; leftMargin: 24 }
        spacing: 18

        Text {
            visible: detail.year > 0
            text: detail.year
            color: sheet.dimColor
            font.pixelSize: 17
        }

        Text {
            visible: detail.rating > 0
            // « note » et pas « score » : le §5 interdit de la confondre avec emu_score,
            // qui mesure la fidélité d'ÉMULATION et non la qualité du jeu.
            text: qsTr("note %1 / 100").arg(detail.rating)
            color: sheet.dimColor
            font.pixelSize: 17
        }

        // Ce que l'export ne porte pas, et qui manque visiblement ici : synopsis, nombre
        // de joueurs, année par plateforme. Demandé au backend (export 1.5.0) plutôt que
        // deviné sur l'appareil (§0).
    }

    Text {
        id: warning
        anchors { top: meta.bottom; topMargin: 10; left: coverArt.right; leftMargin: 24
                  right: parent.right; rightMargin: 32 }
        visible: text.length > 0
        // Une capacité non déclarée se DIT, elle ne se devine pas (§1).
        text: detail.launchWarning
        color: "#d8a657"
        font.pixelSize: 16
        wrapMode: Text.WordWrap
    }

    ListView {
        id: platforms

        anchors { top: banner.bottom; topMargin: 14
                  left: parent.left; right: parent.right
                  bottom: colourKey.visible ? colourKey.top : legend.top
                  bottomMargin: 12 }

        model: detail
        focus: true
        clip: true
        keyNavigationEnabled: true
        highlightMoveDuration: 0
        highlight: Rectangle { color: sheet.selection }

        delegate: Item {
            id: platformRow

            required property string platformKey
            required property string displayName
            required property int emuScore
            required property int status
            required property bool isDefaultChoice
            required property string romPath
            required property var languages
            required property string hardware
            required property string emulators
            required property string driverStatus
            required property int index

            width: ListView.view.width
            // Deux hauteurs seulement, selon qu'il y ait ou non des langues : la fiche
            // compte quelques lignes, pas 7 581 — le coût d'un layout variable y est nul.
            height: 56 + (languages.length > 0 ? 12 : 0) + (hardware.length > 0 ? 20 : 0)

            // 0 noir, 1 rouge, 2 vert — l'ordre de l'énumération du modèle.
            Rectangle {
                anchors { left: parent.left; leftMargin: 32; verticalCenter: parent.verticalCenter }
                width: 18
                height: 18
                radius: 3
                color: sheet.statusColors[parent.status]
                border.width: parent.status === 0 ? 1 : 0
                border.color: sheet.dimColor
            }

            Column {
                anchors { left: parent.left; leftMargin: 72; verticalCenter: parent.verticalCenter }
                spacing: 6

                Text {
                    text: platformRow.displayName + "  ·  " + platformRow.platformKey
                    color: sheet.textColor
                    font.pixelSize: 20
                }

                // §7 : le détail « quelle release apporte quelles langues » appartient à la
                // fiche. Même sémantique illuminé/grisé qu'en vue liste, mais RESTREINTE à
                // cette plateforme — un badge illuminé ici veut dire « la ROM que vous
                // possédez SUR CE SYSTÈME fournit cette langue ».
                //
                // Aucune borne, contrairement à la vue liste : la fiche peut se permettre
                // d'être exhaustive, et c'est justement ce qu'on vient y chercher.
                // ARCADE : le matériel réel et l'état du pilote. Le §4 le rappelle —
                // IGDB n'a qu'une plateforme « Arcade » globale, alors que « neogeo » et
                // « cps2 » ne se ressemblent en rien. L'information était dans l'export
                // depuis le lot 3 sans jamais être montrée.
                Text {
                    visible: platformRow.hardware.length > 0
                    text: platformRow.hardware
                          + (platformRow.driverStatus.length > 0
                             ? "  ·  pilote " + platformRow.driverStatus : "")
                          + (platformRow.emulators.length > 0
                             ? "  ·  " + platformRow.emulators : "")
                    color: platformRow.driverStatus === "preliminary" ? "#d8a657" : sheet.dimColor
                    font.pixelSize: 15
                }

                Row {
                    spacing: 6
                    visible: platformRow.languages.length > 0

                    Repeater {
                        model: platformRow.languages
                        delegate: LanguageBadge {
                            required property var modelData
                            code: modelData.code
                            owned: modelData.owned
                            textColor: sheet.textColor
                            dimColor: sheet.dimColor
                        }
                    }
                }
            }

            Text {
                anchors { right: scoreText.left; rightMargin: 24; verticalCenter: parent.verticalCenter }
                visible: parent.isDefaultChoice
                text: qsTr("proposé par défaut")
                color: sheet.dimColor
                font.pixelSize: 16
            }

            Text {
                id: scoreText
                anchors { right: parent.right; rightMargin: 32; verticalCenter: parent.verticalCenter }
                // emu_score est une note d'ÉMULATION, pas de qualité du jeu (§5).
                text: qsTr("émulation %1").arg(parent.emuScore)
                color: sheet.dimColor
                font.pixelSize: 16
            }
        }

        Keys.onReturnPressed: sheet.tryLaunch()
        Keys.onEnterPressed: sheet.tryLaunch()
        Keys.onEscapePressed: sheet.closed()
    }

    function tryLaunch() {
        // Le lancement ne part que d'une ligne verte : le modèle le refuse, l'interface
        // n'a pas à dupliquer la règle, seulement à montrer le message.
        sheet.lastMessage = detail.launch(platforms.currentIndex)
        if (sheet.lastMessage.length === 0)
            sheet.lastMessage = qsTr("lancement demandé")
    }

    Text {
        id: legend
        anchors { left: parent.left; leftMargin: 32; right: parent.right; rightMargin: 32
                  bottom: parent.bottom; bottomMargin: 24 }
        text: sheet.lastMessage.length > 0
              ? sheet.lastMessage
              : qsTr("Entrée : lancer · Échap : retour · badge plein = langue fournie par une ROM possédée")
        color: sheet.lastMessage.length > 0 ? "#d8a657" : sheet.dimColor
        font.pixelSize: 16
        wrapMode: Text.WordWrap
    }

    // Légende dessinée avec les mêmes pastilles que les lignes.
    Row {
        id: colourKey
        anchors { left: parent.left; leftMargin: 32; bottom: legend.top; bottomMargin: 10 }
        spacing: 22
        visible: sheet.lastMessage.length === 0

        Repeater {
            model: [
                { c: 2, t: qsTr("possédé") },
                { c: 1, t: qsTr("système présent, ROM absente") },
                { c: 0, t: qsTr("système absent") }
            ]
            delegate: Row {
                required property var modelData
                spacing: 8
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14; height: 14; radius: 3
                    color: sheet.statusColors[parent.modelData.c]
                    border.width: parent.modelData.c === 0 ? 1 : 0
                    border.color: sheet.dimColor
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.modelData.t
                    color: sheet.dimColor
                    font.pixelSize: 15
                }
            }
        }
    }
}
