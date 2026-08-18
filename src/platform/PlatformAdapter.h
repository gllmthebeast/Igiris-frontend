#pragma once

// L'adaptateur de plateforme — CLAUDE.md §1.
//
// Tout ce qui est spécifique à une distribution est isolé derrière cette interface, qui
// expose EXACTEMENT SIX choses :
//
//   1. FOURNIR les systèmes déclarés (les localiser ET lire leur format)
//   2. résoudre une platform_key du catalogue vers le nom de système local
//   3. localiser les dossiers de ROMs
//   4. lancer — construire et exécuter la commande, substitution de %ROM%
//   5. localiser l'export sur cet appareil
//   6. rendre la main à l'interface de RÉGLAGES de l'hôte
//
// ⚠️ La cinquième est arrivée APRÈS coup, et l'oubli avait un coût visible : sur Batocera
// l'application affichait « 0 jeux ». L'installateur dépose l'export dans /userdata, la
// seule partition inscriptible, tandis que main.cpp cherchait des chemins Unix génériques
// (data/, ~/.local/share, /var/lib) dont AUCUN n'existe là-bas. Le point d'entrée devinait
// donc un emplacement qui, lui aussi, dépend de la distribution.
//
// ⚠️ La sixième existe parce que remplacer l'écran d'accueil de l'hôte, c'est aussi
// remplacer sa SEULE interface de configuration. Chez Batocera, les manettes, le wifi, le
// bluetooth, l'audio, la résolution et la mise à jour du système vivent tous dans
// EmulationStation : le masquer, c'est les rendre inatteignables. On ne réimplémente rien
// — le §0 interdit les addons et le §12 interdit de forker ES — on lui rend l'écran.
//
// Plus la déclaration de capacités : une distribution qui ne sait pas faire quelque chose
// le dit, et l'interface s'adapte au lieu de planter.
//
// Rien d'autre n'a le droit d'entrer ici. Le reste du code — chargement du catalogue,
// scan, statuts, vues QML — ne sait pas sur quelle distribution il tourne.

#include <QFlags>
#include <QList>
#include <QString>
#include <QStringList>

namespace igiris::platform {

// Ce qu'une distribution sait faire. Déclaré, jamais supposé.
enum class Capability : quint32 {
    None              = 0,
    SystemsFile       = 1u << 0, // expose un fichier de description des systèmes
    RomDirectories    = 1u << 1, // sait localiser les dossiers de ROMs
    Launch            = 1u << 2, // sait lancer un jeu
    PerSystemEmulator = 1u << 3, // la commande accepte un choix d'émulateur explicite
    // La configuration des manettes est transmise à l'émulateur (%CONTROLLERSCONFIG%).
    // NON déclarée aujourd'hui : sans elle, l'émulateur retombe sur sa configuration par
    // défaut. Le §1 exige qu'une distribution DISE ce qu'elle ne sait pas faire, plutôt
    // que de laisser croire.
    ControllerMapping = 1u << 4,
    // Sait rendre la main à l'interface de réglages de l'hôte (§1, responsabilité 6).
    // NON déclarée quand le binaire de réglages est introuvable : mieux vaut une entrée
    // grisée qui s'explique qu'un bouton qui ne fait rien.
    HostSettings = 1u << 5,
};
Q_DECLARE_FLAGS(Capabilities, Capability)
Q_DECLARE_OPERATORS_FOR_FLAGS(Capabilities)

// Une façon de lancer un système. Le format en autorise PLUSIEURS par système, chacune
// étiquetée — constaté dans un vrai fichier : « MAME [Diskette] », « MAME [Cartridge] »,
// « ColEm »… Supposer une commande unique aurait fait perdre ce choix en silence.
struct LaunchOption {
    QString label;   // libellé d'émulateur, éventuellement vide
    QString command; // ligne de commande BRUTE, lue du fichier
};

// Une entrée du fichier de description des systèmes.
//
// C'est le PARSER (lot 2) qui remplit cette structure ; l'adaptateur ne fait que dire où
// trouver le fichier. Les commandes sont lues TELLES QUELLES : le §1 interdit de hardcoder
// le chemin du script de lancement, parce qu'il change (la version de Python embarquée a
// déjà bougé plusieurs fois chez Batocera).
struct SystemEntry {
    QString name;     // nom de système local, ex. « snes »
    QString fullName; // libellé affichable
    // Chemin des ROMs TEL QU'ÉCRIT : il peut contenir des placeholders (« %ROMPATH%/snes »)
    // ou un « ~ ». Sa résolution est le travail de l'adaptateur, pas du parser.
    QString     romPath;
    QString     platform; // clé de plateforme déclarée par le fichier
    QString     theme;
    QStringList extensions; // extensions reconnues, en minuscules et dédoublonnées

    QList<LaunchOption> launchOptions;

    // La commande retenue par défaut : la première déclarée. Le fichier les liste par
    // ordre de préférence.
    QString defaultCommand() const
    {
        return launchOptions.isEmpty() ? QString() : launchOptions.first().command;
    }

    bool isValid() const { return !name.isEmpty() && !defaultCommand().isEmpty(); }
};

// Une commande prête à exécuter — programme + arguments déjà séparés.
//
// Volontairement PAS une chaîne unique : on n'exécute jamais via un shell. Les noms de
// ROM contiennent des apostrophes, des parenthèses et des esperluettes
// (« Sonic & Knuckles.md », « Pokémon (USA) [!].zip ») ; passer par un shell, c'est
// choisir entre des lancements cassés et une injection de commande.
struct LaunchCommand {
    QString     program;
    QStringList arguments;

    // Placeholders %FOO% rencontrés mais non reconnus. Ils sont laissés VERBATIM dans la
    // commande et signalés ici : les avaler en silence produirait un lancement qui échoue
    // sans que personne sache pourquoi (§15).
    QStringList unresolvedPlaceholders;

    bool isValid() const { return !program.isEmpty(); }
};

// Ce qu'il faut savoir du jeu lui-même pour bâtir la commande. Séparé de SystemEntry :
// le système vient du fichier de description, le jeu vient du catalogue.
struct LaunchDetails {
    QString gameName;       // %GAMENAME%
    int     optionIndex = 0; // laquelle des launchOptions du système (§7)
};

class PlatformAdapter
{
public:
    virtual ~PlatformAdapter() = default;

    // Identité — « batocera », « recalbox »…
    virtual QString id() const = 0;
    virtual QString displayName() const = 0;

    virtual Capabilities capabilities() const = 0;
    bool supports(Capability c) const { return capabilities().testFlag(c); }

    // (1) Fournir les systèmes déclarés par cette installation.
    //
    //     ⚠️ Corrigé au lot 9. L'interface exposait d'abord `systemsFilePath()`, en
    //     supposant que toutes les distributions partagent UN format que le projet
    //     parserait une fois pour toutes. Recalbox dément cette hypothèse : son
    //     systemlist.xml met name, fullname, path et command en ATTRIBUTS XML, là où la
    //     convention EmulationStation utilise des éléments enfants.
    //
    //     Chaque adaptateur lit donc son propre format. Le chemin reste exposé, mais pour
    //     le diagnostic seulement — ce n'est plus le contrat.
    virtual QList<SystemEntry> readSystems(QString *error) const = 0;

    //     Où l'adaptateur a trouvé sa description. Diagnostic, pas contrat.
    virtual QString systemsFilePath() const = 0;

    // (2) Résoudre une platform_key du catalogue vers le nom de système local.
    //     `localSystems` vient du fichier de description : c'est LUI la source de vérité
    //     des systèmes présents, et donc lui qui décide du statut noir (§7).
    //     Chaîne vide = ce système n'existe pas sur cette installation.
    virtual QString resolvePlatformKey(const QString &platformKey,
                                       const QStringList &localSystems) const = 0;

    // (3) Localiser les dossiers de ROMs.
    virtual QStringList romDirectories() const = 0;

    // (4a) Construire la commande — fonction PURE, sans effet de bord.
    //      Séparée de launch() pour être testable sans rien exécuter.
    virtual LaunchCommand buildLaunchCommand(const SystemEntry &system,
                                             const QString      &romPath,
                                             const LaunchDetails &details) const = 0;

    // (4b) Exécuter. `error` reçoit le message COMPLET en cas d'échec (§15).
    virtual bool launch(const SystemEntry &system, const QString &romPath,
                        const LaunchDetails &details, QString *error) const = 0;

    // (5) Où l'export peut se trouver sur CETTE distribution, par ordre de préférence.
    //
    //     Chemins COMPLETS du fichier, pas des répertoires : c'est ce que le point d'entrée
    //     teste, et lui laisser recomposer un nom de fichier ferait ressortir une
    //     convention hors de l'adaptateur.
    //
    //     L'emplacement dépend de la distribution parce que la partition INSCRIPTIBLE en
    //     dépend : /userdata chez Batocera, /recalbox/share ailleurs. Ces chemins passent
    //     avant les emplacements génériques, sans les remplacer — un binaire lancé depuis
    //     un dépôt doit continuer à trouver son data/games.db.
    virtual QStringList exportSearchPaths() const = 0;

    // (6a) La commande qui ouvre les RÉGLAGES de l'hôte. Fonction pure, testable sans rien
    //      exécuter — même découpage que (4a).
    //
    //      Commande VIDE = cette distribution ne sait pas le faire, et le dit. C'est le
    //      défaut : une distribution dont on ignore le frontend de réglages ne doit pas
    //      voir le projet en inventer un nom au hasard.
    virtual LaunchCommand hostSettingsCommand() const { return {}; }

    // (6b) Exécuter. `error` reçoit le message COMPLET en cas d'échec (§15).
    virtual bool openHostSettings(QString *error) const
    {
        if (error)
            *error = QStringLiteral("cette distribution n'expose pas d'interface de "
                                    "réglages connue de ce frontend");
        return false;
    }

    // Libellé de l'entrée de menu, propre à l'hôte — « Paramètres Batocera ». Vide quand la
    // capacité n'est pas déclarée.
    virtual QString hostSettingsLabel() const { return {}; }

    // L'écran d'accueil de l'hôte TOURNE-T-IL DÉJÀ ?
    //
    // Vrai quand ce frontend a été lancé DEPUIS lui — en « port » déposé dans les ROMs,
    // plutôt qu'au démarrage à sa place. Dans ce cas, ouvrir les réglages en lancerait un
    // SECOND : deux écrans d'accueil se disputant l'affichage. Le bon geste est alors de
    // se terminer, ce qui rend la main à celui qui nous a lancés.
    //
    // Ce n'est pas un détail d'implémentation : c'est ce qui rend le déploiement « essayer
    // sans rien casser » possible, et sans cette détection le bouton deviendrait un piège.
    virtual bool hostFrontendIsRunning() const { return false; }

    // Libellé du bouton QUAND on est lancé depuis l'hôte — « Retour à Batocera » et non
    // « Paramètres ». Le libellé DIT ce qui va se passer ; c'est la seule façon honnête de
    // présenter deux comportements sous un même bouton.
    virtual QString hostReturnLabel() const { return {}; }

    // COMMENT on revient, dit dans les mots de l'hôte. Vide s'il n'y a rien à expliquer.
    //
    // Ce n'est pas du confort : sur certaines distributions, l'entrée de menu qui ramène
    // ici ne s'appelle pas « quitter » et personne ne peut le deviner. Une porte de sortie
    // qu'il faut chercher n'est pas une porte de sortie — la leçon vient du bouton
    // lui-même, resté invisible une version durant.
    virtual QString hostSettingsReturnHint() const { return {}; }
};

} // namespace igiris::platform
