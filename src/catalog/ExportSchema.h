#pragma once

// ⚠️ FICHIER D'EXCEPTION — le seul du dépôt, hors adaptateur, autorisé à écrire le nom
// « batocera_system ».
//
// Pourquoi une exception, et pourquoi ELLE SEULE :
//
// Le §1 interdit les chaînes spécifiques à une distribution hors de l'adaptateur, parce
// qu'elles créent une DÉPENDANCE de comportement. Ici, il ne s'agit pas d'un comportement
// mais d'un NOM DE COLONNE imposé par un contrat de données externe (l'export produit par
// le backend, schéma 1.3.0). On ne peut pas le renommer depuis ce dépôt.
//
// Le §9.1 fixe la conduite à tenir : le chargeur de catalogue est le SEUL endroit du code
// qui connaît ce nom, et il expose « platform_key » à tout le reste. Ce fichier matérialise
// ce « seul endroit ». Le jour où le backend renomme (rupture majeure 2.0.0), une seule
// ligne change ici.
//
// Le test no-distro-literals exclut ce fichier NOMMÉMENT, pas son répertoire : toute autre
// occurrence dans src/catalog/ reste refusée.

namespace igiris::catalog::schema {

// Nom réel de la colonne dans l'export. Partout ailleurs dans le code : « platformKey ».
inline constexpr const char *kPlatformKeyColumn = "batocera_system";

// Version MAJEURE du schéma d'export que ce binaire sait lire.
// Une majeure inconnue est refusée explicitement, jamais interprétée (§2).
inline constexpr int kSupportedMajor = 1;

} // namespace igiris::catalog::schema
