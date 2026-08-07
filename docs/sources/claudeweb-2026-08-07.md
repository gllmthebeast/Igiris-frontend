CLAUDE.md — Frontend de jeux unifié
Fichier de contexte projet destiné à Claude Code.
Emplacement attendu : racine du dépôt frontend, sur la VM Ubuntu.
Dépendance amont : ce projet consomme l'export produit par le projet
igiris (voir son CLAUDE.md). Il ne démarre qu'une fois l'export générable
de bout en bout.
1. Objectif
Remplacer l'interface d'accueil des distributions de rétrogaming par un
frontend custom présentant une liste unique de jeux, tous systèmes
confondus.
Principes non négociables :
Multi-distribution dès le premier jour. Le frontend n'est pas un projet
Batocera. Batocera est sa première cible, pas sa cible unique. Voir §2.
Le moteur d'émulation n'est pas modifié. Aucun patch des cores, de
RetroArch, ni des scripts de lancement de la distribution hôte.
Pas de liste de systèmes à l'accueil. L'entité de premier niveau est le
jeu, pas la plateforme.
Pas d'addons ni de store. Interface volontairement minimale.
Tous les jeux du catalogue sont affichés, ROM présente ou non. L'absence
de ROM est une information affichée, pas un filtre.
2. Contrainte de premier rang : compatibilité multi-OS
Le constat qui rend ça possible
es_systems.cfg est une convention EmulationStation, pas une invention
Batocera. Ce fichier décrit pour chaque système l'emplacement des ROMs et la
ligne de commande exacte de lancement. Presque toute la famille des
distributions de rétrogaming l'utilise.
Ce qui change d'une distribution à l'autre, ce ne sont pas les concepts mais
les chemins et la commande de lancement.
Matrice de compatibilité
Distribution
Base
Statut
Batocera
Buildroot
Cible de référence
Recalbox
Buildroot
Cible de validation (voir §10)
RetroPie
surcouche Raspberry Pi OS
Supporté (lance via runcommand.sh)
Retrobat
Windows, dérivé Batocera
Supporté
EmuELEC
LibreELEC
Supporté
Knulli
dérivé Batocera
Supporté
ArkOS / ROCKNIX
Linux + ES
À vérifier au cas par cas
ES-DE
format es_systems.xml
Adaptable, format proche
Lakka
RetroArch seul, pas d'ES
Hors périmètre
muOS
frontend propriétaire
Hors périmètre
Les deux exclusions ne sont pas des oublis : ces systèmes n'ont aucune couche
EmulationStation à remplacer. Les supporter demanderait un pilote de lancement
entièrement différent, ce qui ne se justifie qu'après validation du reste.
L'adaptateur de plateforme
Décision à appliquer avant la première ligne de code.
Tout ce qui est spécifique à une distribution est isolé derrière une interface
unique. Elle expose exactement quatre choses :
Localiser le fichier de description des systèmes
Résoudre une clé de plateforme du catalogue vers le nom de système local
Localiser les dossiers de ROMs
Lancer — construire et exécuter la commande, substitution de %ROM%
Rien d'autre. Tout le reste du code — chargement du catalogue, matching par
hash, calcul des statuts, interface QML — ne sait pas sur quelle
distribution il tourne.
Règles de contrôle
Aucune chaîne littérale spécifique à une distribution en dehors de
l'adaptateur. Pas de /userdata/, pas de batocera, pas de
emulatorlauncher.py ailleurs. C'est vérifiable par un simple grep, et ça
doit l'être en CI.
Ne jamais hardcoder le chemin du script de lancement. On lit toujours la
commande dans le fichier de description des systèmes. C'est ce qui fait
survivre l'application aux changements de chemin du launcher (la version de
Python embarquée a déjà changé plusieurs fois chez Batocera), à l'ajout ou au
retrait de systèmes, et aux changements d'options.
Le fichier de description des systèmes est la source de vérité des
systèmes présents → c'est lui qui décide du statut noir.
L'adaptateur déclare ses capacités. Une distribution qui ne sait pas
faire quelque chose le dit ; l'interface s'adapte au lieu de planter.
Conséquence sur le format d'export
Le catalogue est agnostique de la distribution. L'export ne doit contenir
aucune colonne nommée d'après un système Batocera : il porte une platform_key
neutre, et c'est l'adaptateur qui résout platform_key → nom de système
local, au démarrage, en lisant le fichier de description.
À répercuter côté igiris : exp_game_platform et exp_rom_hash portent
platform_key, pas batocera_system.
3. Écran d'accueil
Liste de jeux + recherche. Rien d'autre.
Composition d'une ligne
Élément
Rôle
Jaquette (vignette)
identification visuelle
Titre
canonique, issu du catalogue
Drapeaux de langues
voir §4
Pas d'indicateur de système en vue liste : le détail par plateforme est
l'affaire de la fiche de jeu (§5). La liste reste dense et lisible à distance.
Filtres
La liste est filtrable. La langue fait partie des filtres, au même titre que
la plateforme et le statut de possession.
Filtre
Nature
Résolution
Langue — existe au catalogue
statique
index de l'export
Langue — possédée
dynamique
export × index local
Plateforme
statique
index de l'export
Possédé / manquant
dynamique
index local
Année, arcade
statique
index de l'export
La distinction statique / dynamique n'est pas cosmétique : un filtre statique
est un index précalculé par igiris, un filtre dynamique impose un croisement
avec le résultat du scan local. Voir §4 et §8.
Les filtres sont combinables, et la combinaison doit rester interactive à la
manette — pas de temps d'attente perceptible entre l'appui et le résultat.
4. Drapeaux de langues en vue liste
Sémantique — deux états, un seul axe
Pour chaque langue affichée :
État
Signification
Illuminé
Au moins une ROM présente localement fournit cette langue
Grisé
La langue existe pour ce jeu au catalogue, mais aucune ROM possédée ne la fournit
Une langue qui n'existe dans aucune release du jeu n'est pas affichée du
tout — ni grisée, ni illuminée. Il n'y a pas de troisième état.
Cette sémantique est volontairement alignée sur le code couleur des systèmes
de la fiche de jeu : illuminé ≈ vert (possédé), grisé ≈ rouge (existe, pas
possédé), absent ≈ le sujet ne se pose pas.
Portée : le jeu, pas la ROM
La ligne de liste représente un jeu, qui couvre N plateformes et M ROMs.
Les drapeaux affichés sont donc l'union des langues de toutes les releases du
jeu, et l'état illuminé/grisé est calculé sur l'union des ROMs possédées,
tous systèmes confondus.
Le détail quelle ROM sur quelle plateforme apporte quelle langue appartient à
la fiche de jeu (§5), pas à la liste.
Rappel du titre de la demande : « drapeaux langues disponible par rom ».
L'information est bien d'origine par-ROM ; elle est agrégée pour
l'affichage en liste, et détaillée en fiche.
Ce qu'il faut savoir avant d'implémenter
Une langue n'est pas un pays. C'est le piège principal de cette
fonctionnalité. L'anglais n'a pas de drapeau évident (Royaume-Uni ? États-Unis ?),
l'espagnol non plus (Espagne ? Mexique ?), l'arabe encore moins.
Décision : les icônes sont des badges de code langue ISO 639-1
(EN, FR, DE, ES, IT, JA…), stylisés — pas des drapeaux nationaux.
Le mot « drapeau » reste employé dans l'équipe par commodité, mais l'asset est
un badge de langue. Ça évite une classe entière de bugs de représentation et
reste lisible à distance sur un écran de télévision.
Langue ≠ région. Une ROM (Europe) n'est pas « en européen » : elle porte
souvent (En,Fr,De,Es,It). Inversement une ROM (Japan) sans balise de langue
est implicitement japonaise. La donnée de langue vient de la balise de langues
du dat, avec repli sur une table région → langue implicite lorsque la balise
est absente. Cette résolution se fait côté igiris, jamais sur l'appareil.
Volume d'affichage. Certains jeux dépassent dix langues. La ligne de liste
en affiche un nombre borné (ordre : langues possédées d'abord, puis langue de
l'interface, puis ordre stable du catalogue), avec un indicateur +N pour le
reste. Le détail complet est en fiche.
La langue comme filtre
Le badge et le filtre sont la même règle appliquée à deux échelles : ce qui
illumine un badge est exactement ce qui fait passer un jeu à travers le filtre
« possédé dans cette langue ». Une seule implémentation, deux points d'appel.
Deux filtres distincts, à ne pas confondre dans l'interface :
« Existe en français » — le jeu a une release francophone au catalogue,
possédée ou non. Statique, index de l'export.
« Jouable en français » — une ROM possédée fournit le français.
Dynamique, croisement avec l'index local de hashes.
Le second est celui qui a une valeur d'usage réelle ; le premier sert la
découverte. Les deux doivent être proposés, avec des libellés qui ne laissent
aucune ambiguïté.
Combinaison multi-langues. Un filtre sur plusieurs langues simultanées passe
par exp_game.lang_mask et un ET binaire, pas par une jointure répétée sur
exp_game_language. Le masque est fourni par l'export précisément pour ça.
Règle de bit. Chaque langue occupe une position de bit fixe, attribuée à vie
par igiris. Le frontend ne déduit jamais une position depuis l'ordre
alphabétique ou l'ordre d'affichage : il lit exp_language.bit_index. Un
décalage sur ce point produit des badges faux, silencieusement.
Contrainte de rendu
Sur Raspberry Pi, une liste qui défile avec N badges par ligne est un piège à
performance.
Les badges sont servis depuis un atlas de sprites unique, pas N fichiers
image chargés individuellement.
L'état grisé est obtenu par shader ou opacité sur le même sprite, jamais
par un second jeu d'assets.
Le nombre de badges par ligne est borné et connu à la génération de
l'export, pour éviter tout calcul de layout variable pendant le défilement.
Données requises
L'agrégation par jeu et le calcul « quelles langues quelle ROM » sont
précalculés côté igiris. L'appareil ne fait qu'un lookup et une comparaison
avec les hashes possédés.
Ce que l'export fournit (déjà spécifié côté igiris) :
Objet
Contenu
exp_language
référentiel des langues : code, libellé, asset de badge, bit_index
exp_game_language
(game_key, lang_code, platform_key, crc32) — quelle ROM fournit quelle langue
exp_game.lang_mask
masque de bits des langues existant au catalogue pour ce jeu
Règle d'illumination, dans son intégralité :
une langue est illuminée si au moins un crc32 de exp_game_language pour
ce (game_key, lang_code) figure dans l'index local de hashes ; sinon grisée.
Il n'y a rien d'autre à calculer. Pas de parsing, pas de normalisation, pas de
correspondance région → langue : tout est résolu en amont.
5. Fiche de jeu
Jaquette, métadonnées, et la liste des systèmes sur lesquels le jeu existe,
avec icône et code couleur :
Couleur
Signification
Vert
Système présent sur cette installation et ROM présente localement
Rouge
Système présent, ROM absente
Noir
Système absent de cette installation
Le lancement se fait depuis cette fiche, sur un système en vert.
Le système marqué is_preferred dans l'export est proposé par défaut ; les
autres sont en options secondaires.
C'est également ici — et pas en vue liste — qu'on détaille quelle release
apporte quelles langues : chaque entrée de système porte ses propres badges
de langue, avec la même sémantique illuminé/grisé que §4 mais restreinte à
cette plateforme.
6. Architecture
Ne PAS forker EmulationStation
batocera-emulationstation est en C++/SDL. Le forker imposerait un rebase à
chaque release, et lierait le projet à une seule distribution — ce qui
contredit §2. Interdit dans ce projet.
Application autonome
Le frontend est une application séparée, packagée à part, qui :
Est lancée au démarrage à la place d'EmulationStation
Affiche sa propre interface
Pour lancer un jeu, passe par l'adaptateur de plateforme
7. Stack : Qt6 + QML
Trois contraintes déterminent ce choix :
Pilotage à la manette, pas à la souris. Élimine tout ce qui suppose un
curseur ; met la navigation par focus au centre.
Buildroot. Plusieurs distributions cibles sont construites avec
Buildroot : tout runtime ajouté doit être cross-compilable vers ARM et
x86_64. C'est là que la plupart des options meurent.
Matériel cible contraint. Sur Raspberry Pi, ~1-2 Go de RAM utilisable et
un GPU faible, pendant que les émulateurs tournent.
Qt est aussi le seul choix qui satisfait §2 sans effort : la même application
tourne sous Linux embarqué, Linux de bureau et Windows — ce dernier point étant
nécessaire pour Retrobat.
Répartition
C++ : chargement de l'export, adaptateurs de plateforme, scan des ROMs,
exécution du lancement
QML : toute l'interface — liste, recherche, badges de langue, fiche de
jeu, pastilles de statut. GridView / ListView gèrent nativement la
navigation par focus.
Concrètement, très peu de C++ : la couche qui expose les données à QML, plus
les adaptateurs.
Pourquoi pas .NET malgré l'expérience C# disponible
Faire entrer un runtime .NET dans une image Buildroot cross-compilée ARM, pour
un produit distribué sur plusieurs OS hôtes, est un chantier permanent. Le coût
dépasse le gain.
En revanche, les outils côté serveur (import IGDB, génération d'export) peuvent
parfaitement rester en C# — ils ne tournent pas sur l'appareil.
Références à étudier
Pegasus Frontend — application native cross-platform en C++/Qt, orientée
embarqué, navigation 100 % manette, tourne sur Raspberry Pi et Odroid.
À utiliser comme référence d'architecture (pont C++/QML, gestion manette),
pas comme base à forker : GPLv3 (copyleft, contraignant pour une distribution
commerciale) et il liste les jeux possédés, en désaccord de fond avec
l'affichage rouge/noir.
RomM — gestionnaire de collection auto-hébergé, AGPL-3.0, avec exports
ES-DE et Pegasus, API REST, et un écosystème d'applications compagnons
(Playnite, Android, handhelds Linux). Structurellement, ces compagnons jouent
vis-à-vis de RomM le rôle que ce frontend joue vis-à-vis d'igiris. Bon modèle
de découpage serveur / client.
romman (ryanm101) — matching par hash d'abord (SHA1 puis CRC32), DAT comme
source de vérité, identification des jeux manquants, règles déterministes de
sélection de la meilleure release. Très proche du backend igiris : à lire avant
d'écrire les règles de préférence de version.
8. Données : ce que fait l'appareil, et surtout ce qu'il ne fait pas
Le frontend consomme l'export SQLite produit par igiris : dénormalisé,
lecture seule, un fichier unique, agnostique de la distribution.
Ouverture
Code
Jamais WAL sur l'appareil : WAL crée des fichiers annexes -wal et -shm,
exige de la mémoire partagée accessible en écriture même en lecture seule, et
n'apporte rien sans concurrence. Le mode immuable saute tout le verrouillage et
c'est aussi le plus rapide.
Ce que l'appareil fait
Résolution platform_key → système local, via l'adaptateur
Calcul de CRC32 des fichiers ROM locaux
Lookup dans exp_rom_hash et exp_game_language
Application des filtres : statiques par index, dynamiques par croisement avec
l'index local
Rendu de l'interface
C'est tout. Déterministe, rapide, hors ligne.
Ce que l'appareil ne fait JAMAIS
Matching de titres, fuzzy, normalisation → côté serveur uniquement
Résolution région → langue, normalisation ISO des langues → côté serveur
Requête réseau pour afficher une fiche ou un badge
Écriture dans la base
Pièges du scan local
En-têtes de ROM. Les dats No-Intro NES et SNES sont sans en-tête, alors
que les ROMs en circulation en ont souvent un (16 octets iNES, 512 octets
SMC). Hasher le fichier brut fait tout tomber en rouge à tort. La colonne
header_skip de l'export dit combien d'octets ignorer.
Archives. Beaucoup de ROMs sont zippées : hasher le contenu, pas le zip.
Coût. Le scan doit être incrémental — pas de rehash complet à chaque
démarrage. Cache indexé par chemin + taille + date de modification.
Compatibilité de version
Le frontend vérifie la version majeure SemVer du manifeste de l'export et
refuse de charger un export qu'il ne sait pas lire, avec un message explicite.
9. Environnement de développement
VM Ubuntu — développement et build
Claude Code — outil de travail principal
Le matériel cible est distinct de la VM : prévoir un cycle
build → déploiement → test qui ne suppose pas de développer sur la machine
d'émulation
Des clones en lecture seule des distributions cibles, hors du dépôt
projet, servent de référence (vrais fichiers de description des systèmes,
structure des paquets, scripts de lancement)
Conventions
Versionnement SemVer strict sur tous les livrables. Priorité absolue.
Itérations livrées sous forme d'archives zip.
Erreurs remontées verbatim : messages complets et lisibles, jamais avalés
ni reformulés par le code.
10. Stratégie Git
Phase 1 — ce dépôt, aucun fork
Code neuf, dépôt neuf. Le frontend n'est une modification d'aucune
distribution ; c'est une application qui s'y branche.
Phase 2 — un fork par distribution empaquetée
Utile uniquement pour l'intégration système. Chaque fork reste le plus
mince possible :
un paquet pointant vers ce dépôt
la modification du service de démarrage pour lancer le frontend au lieu d'ES
rien d'autre
Plus le diff avec l'upstream est petit, moins le rebase coûte cher — et plus il
est réaliste de maintenir plusieurs cibles en parallèle. C'est le critère de
conception de ces forks.
Licences — à trancher avant toute diffusion
Chaque distribution hôte embarque des centaines de projets tiers aux licences
hétérogènes (GPL, LGPL, BSD, MIT, et clauses explicitement non
commerciales, notamment sur des cores libretro).
Une image pré-installée ne peut pas être vendue en l'état. Un audit licence
par licence est un prérequis à toute distribution, a fortiori commerciale, et
il est à refaire pour chaque distribution hôte.
Les assets de badges de langue suivent la même règle : n'embarquer que des
icônes dont la licence permet la redistribution commerciale, ou les produire
en propre.
11. Ordre de travail
Interface d'adaptateur de plateforme + implémentation Batocera
Parser du fichier de description des systèmes → systèmes disponibles +
commandes de lancement
Chargement de l'export SQLite + vérification de version
Scanner de ROMs incrémental par hash → statuts vert / rouge / noir
Interface QML : liste + recherche
Badges de langue en vue liste (dépend de exp_game_language côté igiris)
Filtres — statiques d'abord, dynamiques ensuite
Fiche de jeu + lancement effectif + détail des langues par plateforme
Second adaptateur (Recalbox) — voir §12
Empaquetage et intégration au démarrage (déclenche la phase 2 Git)
Note d'ordonnancement : l'étape 6 est placée après la liste nue
volontairement. Une liste qui défile de façon fluide sans badges est le
point de référence de performance ; on mesure ensuite le coût réel des badges
par rapport à cette base.
12. Le test qui valide l'abstraction
Le second adaptateur n'est pas une fonctionnalité, c'est un test de
conception, et il arrive volontairement avant l'empaquetage.
Critère de réussite : ajouter le support Recalbox ne doit toucher que
l'adaptateur. Si l'ajout impose de modifier le chargeur de catalogue, le
scanner ou une vue QML, l'abstraction est fausse — il faut la corriger à ce
moment-là, pas après avoir empaqueté.
Tant que ce test n'est pas passé, la compatibilité multi-OS reste une intention,
pas une propriété.
