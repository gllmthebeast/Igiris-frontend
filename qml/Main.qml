// Écran d'accueil — CLAUDE.md §6 : liste de jeux + recherche. Rien d'autre.
//
// Pas de liste de systèmes, pas d'onglets, pas de store. L'entité de premier niveau est le
// JEU. Et la liste montre TOUS les jeux du catalogue, possédés ou non (§0).
//
// Volontairement SANS jaquettes ni badges à ce stade : le §17 fait de la liste nue le point
// de référence de performance, mesuré avant d'ajouter quoi que ce soit.

// QtQuick SEUL, sans QtQuick.Controls : chaque module Qt ajouté doit être cross-compilé
// dans les images Buildroot des distributions cibles (§12). Un champ de saisie ne vaut pas
// cette dépendance — TextInput fait le travail.
import QtQuick

Window {
    id: root

    width: 1280
    height: 720
    visible: true
    color: theme.background
    title: qsTr("igiris")

    // Nommé « theme » et surtout PAS « palette » : Window possède déjà une propriété
    // palette (QPalette). L'ombrer faisait résoudre theme.dim vers la palette Qt, qui
    // n'a pas ce rôle — d'où des couleurs indéfinies dans les délégués, sans que rien
    // ne plante.
    //
    // Lisible à distance sur un téléviseur : contraste franc, rien de subtil.
    QtObject {
        id: theme
        readonly property color background: "#101014"
        readonly property color surface: "#1a1a22"
        readonly property color selection: "#2d4f7c"
        readonly property color text: "#e8e8ef"
        readonly property color dim: "#8a8a9a"
    }

    // ---------------------------------------------------------------- recherche
    Rectangle {
        id: searchBar

        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 72
        color: theme.surface

        TextInput {
            id: searchField

            anchors {
                fill: parent
                leftMargin: 24
                rightMargin: 260
                topMargin: 12
                bottomMargin: 12
            }

            verticalAlignment: TextInput.AlignVCenter
            color: theme.text
            font.pixelSize: 24
            selectByMouse: false // pilotage à la manette, pas au curseur (§12)

            // Un filtre peut être préréglé au démarrage : le champ doit le refléter,
            // sinon l'écran montre une liste filtrée avec un champ d'apparence vide.
            Component.onCompleted: text = games.filter

            onTextChanged: games.filter = text

            // La liste doit rester atteignable à la manette : bas quitte la recherche.
            Keys.onDownPressed: list.forceActiveFocus()

            Text {
                anchors.fill: parent
                verticalAlignment: Text.AlignVCenter
                visible: searchField.text.length === 0
                text: qsTr("Rechercher un jeu")
                color: theme.dim
                font.pixelSize: 24
            }
        }

        Text {
            anchors { right: parent.right; rightMargin: 24; verticalCenter: parent.verticalCenter }
            color: theme.dim
            font.pixelSize: 20
            text: games.visibleCount === games.totalCount
                  ? qsTr("%1 jeux").arg(games.totalCount)
                  : qsTr("%1 / %2 jeux").arg(games.visibleCount).arg(games.totalCount)
        }
    }

    // ------------------------------------------------------------------- liste
    ListView {
        id: list

        anchors { top: searchBar.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }

        model: games
        focus: true
        clip: true

        // Navigation par focus, pas au curseur : c'est la contrainte n°1 du §12.
        keyNavigationEnabled: true
        keyNavigationWraps: false
        highlightMoveDuration: 0 // pas d'animation : on mesure le défilement nu

        // Le défilement doit rester fluide sur un GPU faible : on ne garde en vie que ce
        // qui est visible, plus une marge d'un écran de part et d'autre.
        cacheBuffer: Math.max(0, height)

        Keys.onUpPressed: {
            if (currentIndex === 0)
                searchField.forceActiveFocus()
            else
                decrementCurrentIndex()
        }

        highlight: Rectangle { color: theme.selection }

        delegate: Item {
            required property string title
            required property int rating
            required property int index

            width: ListView.view.width
            height: 52

            // Pas de colonne « année » : le titre de l'export la porte déjà, sur la
            // TOTALITÉ du catalogue (vérifié : 7 581 titres sur 7 581 finissent par
            // « (année) »). L'afficher deux fois encombrerait une liste qui doit rester
            // dense et lisible à distance (§6).
            Text {
                anchors {
                    left: parent.left
                    leftMargin: 24
                    right: ratingText.left
                    rightMargin: 24
                    verticalCenter: parent.verticalCenter
                }
                text: parent.title
                color: theme.text
                font.pixelSize: 22
                elide: Text.ElideRight
            }

            Text {
                id: ratingText
                anchors { right: parent.right; rightMargin: 24; verticalCenter: parent.verticalCenter }
                // rating est la note IGDB, PAS emu_score : ne jamais les présenter
                // côte à côte sans l'expliquer (§5).
                text: parent.rating > 0 ? parent.rating : ""
                color: theme.dim
                font.pixelSize: 20
            }
        }
    }

    // Message explicite plutôt qu'une liste vide sans explication.
    Text {
        anchors.centerIn: list
        visible: games.visibleCount === 0
        text: games.totalCount === 0
              ? qsTr("Catalogue vide")
              : qsTr("Aucun jeu ne correspond à « %1 »").arg(games.filter)
        color: theme.dim
        font.pixelSize: 24
    }
}
