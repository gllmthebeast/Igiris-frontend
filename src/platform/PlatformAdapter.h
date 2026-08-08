#pragma once

// L'adaptateur de plateforme — CLAUDE.md §1.
//
// Tout ce qui est spécifique à une distribution est isolé derrière cette interface, qui
// expose EXACTEMENT QUATRE choses :
//
//   1. localiser le fichier de description des systèmes
//   2. résoudre une platform_key du catalogue vers le nom de système local
//   3. localiser les dossiers de ROMs
//   4. lancer — construire et exécuter la commande, substitution de %ROM%
//
// Plus la déclaration de capacités : une distribution qui ne sait pas faire quelque chose
// le dit, et l'interface s'adapte au lieu de planter.
//
// Rien d'autre n'a le droit d'entrer ici. Le reste du code — chargement du catalogue,
// scan, statuts, vues QML — ne sait pas sur quelle distribution il tourne.

#include <QFlags>
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

    // (1) Localiser le fichier de description des systèmes.
    //     Chaîne vide si introuvable — l'appelant doit le traiter, pas planter.
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
};

} // namespace igiris::platform
