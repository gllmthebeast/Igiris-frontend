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
    // Liseré porté par TOUS les textes de la fiche depuis que le visuel occupe le
    // fond : sans lui, une phrase qui traverse une zone claire de l'illustration
    // devient illisible. C'est le rendu de texte de Qt qui le dessine, sans effet
    // graphique ni seconde passe — le §12 rappelle que le GPU cible est faible.
    property color outlineColor: "#0a0a0e"

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

    // ------------------------------------------------------------------ visuel de fond
    //
    // L'illustration occupe TOUTE la fiche, et non plus une bande en haut : c'est une image
    // composée, la réduire à un bandeau en jetait l'essentiel.
    //
    // Elle court donc derrière du texte dense, ce que le §6 met en garde de faire. TROIS
    // choses le rendent tenable, et il faut les garder ensemble :
    //   - l'opacité, qui fait le mélange avec le fond sombre de la fiche ;
    //   - le voile ci-dessous, qui rattrape le contraste là où le texte est le plus dense ;
    //   - le liseré porté par chaque texte, qui le détache d'une zone claire de l'image.
    // Monter l'opacité sans ces deux-là rendrait la liste des systèmes illisible à trois
    // mètres — et c'est cette liste qu'on vient lire.
    Image {
        id: bannerImage

        anchors.fill: parent
        source: games.coversEnabled ? detail.bannerRef : ""
        // Recadrée pour REMPLIR la fiche, sans bande vide.
        //
        // Montrer l'image entière était l'autre option ; elle a été essayée et écartée sur
        // mesure. Les illustrations d'IGDB n'ont pas de format commun — relevé sur les 12
        // premières du catalogue : de 3,1 (1920×620) à 0,82 (891×1080), et DEUX seulement
        // en 16:9. Les afficher entières laisserait donc des bandes vides sur la majorité
        // des fiches, avec une arête franche à la limite de l'image. Le recadrage perd des
        // bords ; l'ajustement perdait le fond de page.
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        // Décodage borné à la largeur réellement affichée : sans cela Qt garderait un
        // second décodage pleine taille de l'image déjà affichée en vignette.
        sourceSize.width: 1280
        // Franche pour une vraie illustration, discrète quand ce n'est qu'une jaquette
        // étirée : la fiche ne prétend pas montrer ce qu'elle n'a pas.
        opacity: status === Image.Ready ? (detail.hasRealBanner ? 0.65 : 0.32) : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }

    // Voile de lisibilité. Léger en haut, où il n'y a que le titre sur deux lignes ; plus
    // soutenu vers le bas, où s'empilent les systèmes, leurs badges et la légende.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.00; color: Qt.rgba(0.06, 0.06, 0.08, 0.30) }
            GradientStop { position: 0.28; color: Qt.rgba(0.06, 0.06, 0.08, 0.45) }
            GradientStop { position: 1.00; color: Qt.rgba(0.06, 0.06, 0.08, 0.78) }
        }
    }

    // Repère de mise en page de l'en-tête — jaquette, titre, métadonnées. Purement
    // géométrique : il ne dessine rien depuis que le visuel occupe toute la fiche.
    Item {
        id: banner

        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 208
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
        style: Text.Outline; styleColor: sheet.outlineColor
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
            style: Text.Outline; styleColor: sheet.outlineColor
            font.pixelSize: 17
        }

        Text {
            visible: detail.rating > 0
            // « note » et pas « score » : le §5 interdit de la confondre avec emu_score,
            // qui mesure la fidélité d'ÉMULATION et non la qualité du jeu.
            text: qsTr("note %1 / 100").arg(detail.rating)
            color: sheet.dimColor
            style: Text.Outline; styleColor: sheet.outlineColor
            font.pixelSize: 17
        }

        // Modes de jeu (export 1.6.0). Les libellés viennent de l'export — l'appareil
        // n'invente aucune table (§0). C'est la réponse au « nombre de joueurs » demandé :
        // la source ne couvrait « 1-4 » que sur 12 % du catalogue, les modes sur 97 %.
        Text {
            visible: detail.modeLabels.length > 0
            text: detail.modeLabels.join("  ·  ")
            color: sheet.dimColor
            style: Text.Outline; styleColor: sheet.outlineColor
            font.pixelSize: 17
        }
    }

    // Synopsis (export 1.6.0). §7 : « il lui manque ce qui raconte le jeu ».
    //
    // TOUJOURS EN ANGLAIS, y compris sur une interface en français : IGDB n'en fournit pas
    // de traduit. Mieux vaut le texte réel que rien — mais il n'est pas présenté comme s'il
    // était localisé, et il reste borné : la fiche sert à CHOISIR un système et à lancer,
    // pas à lire. Trois lignes, élidées, à 3 m d'un téléviseur (§6).
    Text {
        id: synopsis

        anchors { top: banner.bottom; topMargin: 12
                  left: parent.left; leftMargin: 32
                  right: parent.right; rightMargin: 32 }
        visible: text.length > 0
        // Les synopsis d'IGDB contiennent des sauts de ligne : gardés tels quels, la
        // troisième ligne se réduit à un « … » solitaire dès qu'un paragraphe commence
        // au mauvais endroit. On les replie en un seul paragraphe — c'est un teaser de
        // trois lignes, pas un texte à lire.
        text: detail.summary.replace(/\s+/g, " ").trim()
        color: sheet.dimColor
        style: Text.Outline; styleColor: sheet.outlineColor
        font.pixelSize: 16
        lineHeight: 1.25
        wrapMode: Text.WordWrap
        maximumLineCount: 3
        elide: Text.ElideRight
    }

    Text {
        id: warning
        anchors { top: meta.bottom; topMargin: 10; left: coverArt.right; leftMargin: 24
                  right: parent.right; rightMargin: 32 }
        visible: text.length > 0
        // Une capacité non déclarée se DIT, elle ne se devine pas (§1).
        text: detail.launchWarning
        color: "#d8a657"
        style: Text.Outline; styleColor: sheet.outlineColor
        font.pixelSize: 16
        wrapMode: Text.WordWrap
    }

    ListView {
        id: platforms

        anchors { top: synopsis.visible ? synopsis.bottom : banner.bottom; topMargin: 14
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
            required property int releaseYear
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

                // L'année est celle de la sortie SUR CETTE MACHINE (export 1.5.0), pas
                // celle du jeu affichée en tête de fiche : 42 % des lignes en diffèrent.
                // Prince of Persia est Apple ][ 1989 et PC Engine CD 1991 — c'est
                // exactement ce que cette liste sert à montrer.
                Text {
                    text: platformRow.displayName + "  ·  " + platformRow.platformKey
                          + (platformRow.releaseYear > 0
                             ? "  ·  " + platformRow.releaseYear : "")
                    color: sheet.textColor
                    style: Text.Outline; styleColor: sheet.outlineColor
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
                style: Text.Outline; styleColor: sheet.outlineColor
                font.pixelSize: 16
            }

            Text {
                id: scoreText
                anchors { right: parent.right; rightMargin: 32; verticalCenter: parent.verticalCenter }
                // emu_score est une note d'ÉMULATION, pas de qualité du jeu (§5).
                text: qsTr("émulation %1").arg(parent.emuScore)
                color: sheet.dimColor
                style: Text.Outline; styleColor: sheet.outlineColor
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
                    style: Text.Outline; styleColor: sheet.outlineColor
                    font.pixelSize: 15
                }
            }
        }
    }
}
