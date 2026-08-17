// Écran d'accueil — CLAUDE.md §6 : liste de jeux + recherche. Rien d'autre.
//
// Pas de liste de systèmes, pas d'onglets, pas de store. L'entité de premier niveau est le
// JEU. Et la liste montre TOUS les jeux du catalogue, possédés ou non (§0).
//
// La liste NUE a servi de référence de performance (§17), avant d'ajouter les badges de
// langue (lot 8) puis les jaquettes. Les deux ont été mesurés contre elle, pas estimés.

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
                // Réservé pour le compteur, la version ET le bouton des réglages, qui
                // vivent tous dans la barre du haut.
                rightMargin: 520
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
            Keys.onDownPressed: platformCell.forceActiveFocus()

            Text {
                anchors.fill: parent
                verticalAlignment: Text.AlignVCenter
                visible: searchField.text.length === 0
                text: qsTr("Rechercher un jeu")
                color: theme.dim
                font.pixelSize: 24
            }
        }

        Row {
            anchors { right: parent.right; rightMargin: 24; verticalCenter: parent.verticalCenter }
            spacing: 10

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: theme.dim
                font.pixelSize: 20
                text: games.visibleCount === games.totalCount
                      ? qsTr("%1 jeux").arg(games.totalCount)
                      : qsTr("%1 / %2 jeux").arg(games.visibleCount).arg(games.totalCount)
            }

            // La version, en petit. Sur un appareil mis à jour à la main, c'est le seul
            // moyen de savoir ce qui tourne sans ouvrir un terminal.
            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: theme.dim
                font.pixelSize: 13
                opacity: 0.7
                text: "v" + appVersion
            }

            // ------------------------------------------------- réglages de l'hôte
            //
            // EN HAUT À DROITE, et non parmi les filtres où il était en 1.9.0 : là-bas il
            // ressemblait à un filtre, se repliait sur une seconde ligne, et il fallait
            // traverser six cellules pour l'atteindre. Il est passé inaperçu — c'est la
            // seule chose qu'on ait eu à redemander après l'avoir livrée.
            //
            // Un accès à la configuration ne se cherche pas : sans lui, la machine n'a plus
            // ni manettes, ni wifi, ni mise à jour (§7.1).
            Item {
                id: settingsCell

                anchors.verticalCenter: parent.verticalCenter
                width: settingsRow.implicitWidth + 32
                height: 42
                activeFocusOnTab: true

                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    // Toujours dessiné, jamais transparent : un bouton qui n'apparaît qu'au
                    // focus n'existe pas pour qui ne sait pas qu'il faut le chercher.
                    color: settingsCell.activeFocus ? theme.selection : "#242430"
                    border.width: 1
                    border.color: settingsCell.activeFocus ? theme.selection : "#3a3a4a"
                }

                Row {
                    id: settingsRow
                    anchors.centerIn: parent
                    spacing: 8

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        // Dessiné, pas un emoji : la police de l'appareil cible n'en a pas
                        // forcément, et un carré vide ne veut rien dire (même règle qu'en
                        // §7 pour le code couleur).
                        text: "\u2699"
                        color: host.settingsAvailable ? theme.text : theme.dim
                        font.pixelSize: 18
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: host.settingsLabel
                        color: host.settingsAvailable ? theme.text : theme.dim
                        font.pixelSize: 17
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        // Le raccourci est ANNONCÉ : c'est ce qui le rend utilisable sans
                        // l'avoir lu dans une note de version.
                        text: host.settingsAvailable ? "F1" : "—"
                        color: theme.dim
                        font.pixelSize: 13
                    }
                }

                Keys.onReturnPressed: root.openHostSettings()
                Keys.onEnterPressed: root.openHostSettings()
            }
        }
    }

    // Atteignable de PARTOUT, sans navigation : depuis la liste, depuis la recherche,
    // depuis une fiche. C'est le geste de secours de l'utilisateur — il ne doit dépendre
    // d'aucun parcours.
    Shortcut {
        sequences: ["F1", StandardKey.Preferences]
        onActivated: root.openHostSettings()
    }

    function openHostSettings() {
        if (!host.settingsAvailable) {
            hostMessage.text = qsTr("Réglages indisponibles : %1").arg(host.unavailableReason)
            return
        }
        // Verbatim (§15) : c'est le seul message que verra quelqu'un devant une machine
        // qu'il ne peut plus configurer.
        var message = host.openSettings()
        hostMessage.text = message.length > 0 ? message : ""
    }

    // ------------------------------------------------------------------ filtres
    //
    // Statiques (plateforme, décennie, arcade) et dynamique (possession) côte à côte,
    // mais le dynamique se grise tant qu'aucun scan local n'a eu lieu (§6).
    Rectangle {
        id: filterBar

        anchors { top: searchBar.bottom; left: parent.left; right: parent.right }
        // Un Flow et non une hauteur fixe : les cellules sont assez nombreuses pour
        // dépasser 1280 px dès qu'une valeur longue est sélectionnée (« existe au
        // catalogue », « Écran partagé »). Un filtre qui sort de l'écran est pire qu'un
        // filtre grisé — le §6 veut qu'il reste VISIBLE, y compris quand il ne sert pas.
        height: filterRow.height + 16
        color: theme.background

        Flow {
            id: filterRow
            anchors { top: parent.top; topMargin: 8
                      left: parent.left; leftMargin: 24
                      right: parent.right; rightMargin: 24 }
            spacing: 12

            FilterCell {
                id: platformCell
                KeyNavigation.down: list
                KeyNavigation.up: searchField
                label: qsTr("Plateforme")
                values: [""].concat(games.availablePlatforms)
                textColor: theme.text; dimColor: theme.dim; focusColor: theme.selection
                // Refléter un filtre préréglé : sinon la barre affiche « tout » alors que
                // la liste est filtrée — exactement le défaut corrigé au lot 5.
                Component.onCompleted: {
                    var i = values.indexOf(games.platformFilter)
                    if (i > 0) currentIndex = i
                }
                onCurrentValueChanged: games.platformFilter = currentValue
                KeyNavigation.right: decadeCell
            }

            FilterCell {
                id: decadeCell
                KeyNavigation.down: list
                KeyNavigation.up: searchField
                label: qsTr("Décennie")
                values: [""].concat(games.availableDecades)
                textColor: theme.text; dimColor: theme.dim; focusColor: theme.selection
                Component.onCompleted: {
                    var i = values.indexOf(games.decadeFilter)
                    if (i > 0) currentIndex = i
                }
                onCurrentValueChanged: games.decadeFilter = currentValue === "" ? 0 : parseInt(currentValue)
                KeyNavigation.left: platformCell
                KeyNavigation.right: arcadeCell
            }

            FilterCell {
                id: arcadeCell
                KeyNavigation.down: list
                KeyNavigation.up: searchField
                label: qsTr("Type")
                values: ["", qsTr("arcade")]
                anyLabel: qsTr("tous")
                textColor: theme.text; dimColor: theme.dim; focusColor: theme.selection
                Component.onCompleted: if (games.arcadeOnly) currentIndex = 1
                onCurrentValueChanged: games.arcadeOnly = currentValue !== ""
                KeyNavigation.left: decadeCell
                KeyNavigation.right: ownedCell
            }

            FilterCell {
                id: ownedCell
                KeyNavigation.down: list
                KeyNavigation.up: searchField
                label: qsTr("Possession")
                values: ["", qsTr("possédés"), qsTr("manquants")]
                available: games.ownershipAvailable
                unavailableHint: qsTr("aucun scan local")
                textColor: theme.text; dimColor: theme.dim; focusColor: theme.selection
                Component.onCompleted: currentIndex = games.ownership
                onCurrentIndexChanged: games.ownership = currentIndex
                KeyNavigation.left: arcadeCell
                KeyNavigation.right: languageCell
            }

            // Les deux filtres de langue du §8. Ils sont VOISINS et non fusionnés : ce sont
            // deux questions différentes, et les confondre est le piège que le §8 signale.
            FilterCell {
                id: languageCell
                KeyNavigation.down: list
                KeyNavigation.up: searchField
                label: qsTr("Langue")
                values: [""].concat(games.availableLanguages)
                available: games.languagesAvailable
                unavailableHint: qsTr("export sans langues")
                textColor: theme.text; dimColor: theme.dim; focusColor: theme.selection
                Component.onCompleted: {
                    var i = values.indexOf(games.languageFilter.length > 0
                                           ? games.languageFilter[0] : "")
                    if (i > 0) currentIndex = i
                }
                // Le modèle attend une LISTE : plusieurs langues s'y combinent par ET
                // binaire. L'interface n'en propose qu'une à la fois, mais la mécanique
                // multi-langues du §8 est celle-là, pas une autre.
                onCurrentValueChanged: games.languageFilter = currentValue === ""
                                                              ? [] : [currentValue]
                KeyNavigation.left: ownedCell
                KeyNavigation.right: languageModeCell
            }

            FilterCell {
                id: languageModeCell
                KeyNavigation.down: list
                KeyNavigation.up: searchField
                // Libellés sans ambiguïté, comme l'exige le §8 : « existe » sert la
                // découverte, « jouable » est celui qui a une valeur d'usage réelle.
                label: qsTr("dispo.")
                anyLabel: qsTr("existe au catalogue")
                values: ["", qsTr("jouable ici")]
                // Statique tant qu'aucun scan n'a eu lieu : « jouable » n'est alors pas
                // calculable, et le prétendre afficherait une liste vide sans raison.
                available: games.ownedLanguagesAvailable
                unavailableHint: qsTr("aucun scan local")
                textColor: theme.text; dimColor: theme.dim; focusColor: theme.selection
                Component.onCompleted: if (games.languageOwnedOnly) currentIndex = 1
                onCurrentValueChanged: games.languageOwnedOnly = currentValue !== ""
                KeyNavigation.left: languageCell
                KeyNavigation.right: modeCell
            }

            // Modes de jeu (export 1.6.0) — le filtre « jouable à plusieurs » que le §6
            // demandait et qu'aucune donnée ne permettait.
            //
            // UN seul filtre ici, là où la langue en a deux : un mode est une propriété du
            // TITRE, pas de la ROM possédée. Il n'y a donc pas de « jouable ici » à lui
            // opposer, et en inventer un serait faux.
            FilterCell {
                id: modeCell
                KeyNavigation.down: list
                KeyNavigation.up: searchField
                label: qsTr("Mode")
                // Les LIBELLÉS sont affichés, les CLÉS sont envoyées au modèle : les deux
                // viennent de l'export, l'interface n'en invente ni ne traduit aucun.
                // La lambda n'est pas décorative : Array.map passe (valeur, index, tableau)
                // et modeLabel n'accepte qu'un argument — Qt s'en sort, mais en le disant
                // à chaque mode. Un avertissement qu'on apprend à ignorer est un
                // avertissement perdu.
                values: [""].concat(games.availableModes.map(function(key) {
                    return games.modeLabel(key)
                }))
                available: games.modesAvailable
                unavailableHint: qsTr("export sans modes")
                textColor: theme.text; dimColor: theme.dim; focusColor: theme.selection
                // Sur l'index et non sur la valeur : c'est lui qui fait le lien entre le
                // libellé montré et la clé attendue par le modèle.
                onCurrentIndexChanged: games.modeFilter =
                    currentIndex === 0 ? [] : [games.availableModes[currentIndex - 1]]
                KeyNavigation.left: languageModeCell
            }

        }
    }

    // Échec de l'ouverture des réglages, dit en clair et pas avalé (§15).
    Text {
        id: hostMessage

        anchors { top: filterBar.bottom; left: parent.left; leftMargin: 24
                  right: parent.right; rightMargin: 24 }
        visible: text.length > 0
        color: "#d8a657"
        font.pixelSize: 16
        wrapMode: Text.WordWrap
    }

    // ------------------------------------------------------------------- liste
    ListView {
        id: list

        anchors { top: hostMessage.visible ? hostMessage.bottom : filterBar.bottom
                  left: parent.left; right: parent.right; bottom: parent.bottom }

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
                platformCell.forceActiveFocus()
            else
                decrementCurrentIndex()
        }

        highlight: Rectangle { color: theme.selection }

        Keys.onReturnPressed: root.openDetail()
        Keys.onEnterPressed: root.openDetail()

        delegate: Item {
            id: gameRow

            required property string title
            required property int rating
            required property string gameKey
            required property string coverRef
            required property var languages
            required property int extraLanguages
            required property string matchedAlias
            required property int index

            width: ListView.view.width
            height: 56

            // §6 : la ligne porte une vignette. Sa largeur est FIXE, qu'il y ait une
            // image ou non : c'est ce qui garantit que tous les titres s'alignent.
            GameCover {
                id: cover
                anchors { left: parent.left; leftMargin: 24; verticalCenter: parent.verticalCenter }
                width: 33
                height: 44
                source: gameRow.coverRef
                enabled: games.coversEnabled
                dimColor: theme.dim
            }

            // Pas de colonne « année » : le titre de l'export la porte déjà, sur la
            // TOTALITÉ du catalogue (vérifié : 7 581 titres sur 7 581 finissent par
            // « (année) »). L'afficher deux fois encombrerait une liste qui doit rester
            // dense et lisible à distance (§6).
            Text {
                anchors {
                    left: cover.right
                    leftMargin: 16
                    right: badges.left
                    rightMargin: 24
                    verticalCenter: parent.verticalCenter
                }
                // Le titre, suivi de l'ALIAS qui a produit ce résultat quand ce n'est pas
                // le titre qui a mordu (export 1.8.0).
                //
                // Sans ça, taper « lttp » fait apparaître « The Legend of Zelda: A Link to
                // the Past » — un titre qui ne contient aucun des caractères tapés. À
                // l'écran, ça ne se distingue pas d'une recherche qui renvoie n'importe
                // quoi. Le §6 veut une liste dense : d'où une mention discrète sur la même
                // ligne, et non une seconde ligne qui changerait la hauteur des lignes.
                text: parent.matchedAlias.length > 0
                      ? parent.title + "  ·  " + parent.matchedAlias
                      : parent.title
                color: theme.text
                font.pixelSize: 22
                elide: Text.ElideRight
            }

            // Bandeau de badges à largeur RÉSERVÉE, calculée depuis games.maxBadges et non
            // depuis le contenu de la ligne : c'est ce qui garantit que le titre s'élide
            // toujours au même endroit, et qu'aucune ligne ne recalcule sa géométrie
            // pendant le défilement (§8).
            Row {
                id: badges

                anchors {
                    right: ratingText.left
                    rightMargin: 24
                    verticalCenter: parent.verticalCenter
                }
                width: games.maxBadges * 36 + 34
                height: 20
                spacing: 6

                // L'ordre du modèle est déjà celui du §8 — possédées d'abord, puis la
                // langue de l'interface, puis l'ordre stable du catalogue. La vue ne
                // retrie rien : elle afficherait sinon un ordre différent du filtre.
                Repeater {
                    model: gameRow.languages
                    delegate: LanguageBadge {
                        required property var modelData
                        code: modelData.code
                        owned: modelData.owned
                        textColor: theme.text
                        dimColor: theme.dim
                    }
                }

                // Le « +N » du §8 : les langues qui ne tiennent pas dans la borne.
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: gameRow.extraLanguages > 0
                    text: "+" + gameRow.extraLanguages
                    color: theme.dim
                    font.pixelSize: 13
                }
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

    function openDetail() {
        if (list.currentIndex < 0 || games.visibleCount === 0)
            return
        detail.setGame(list.currentItem.gameKey)
        detailSheet.lastMessage = ""
        detailSheet.visible = true
        detailSheet.forceActiveFocus()
    }

    // Un jeu préréglé en ligne de commande ouvre directement sa fiche : c'est ce qui rend
    // la fiche capturable sans écran.
    Component.onCompleted: {
        if (detail.gameKey.length > 0) {
            detailSheet.visible = true
            detailSheet.forceActiveFocus()
        }
    }

    // ------------------------------------------------------------------- fiche
    GameDetail {
        id: detailSheet

        anchors.fill: parent
        visible: false

        background: theme.background
        surface: theme.surface
        selection: theme.selection
        textColor: theme.text
        dimColor: theme.dim

        onClosed: {
            visible = false
            list.forceActiveFocus()
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
