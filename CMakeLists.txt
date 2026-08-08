cmake_minimum_required(VERSION 3.22)

# SemVer strict (CLAUDE.md §15) : cette version est la seule source de vérité.
# Elle alimente version.h, et c'est elle qu'affiche `igiris-frontend --version`.
project(igiris-frontend VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick Test)

# Accès à l'export par l'API C de SQLite, et non par le pilote Qt SQL : le §2 impose
# file:games.db?immutable=1 avec SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, ce qui s'exprime
# sans ambiguïté avec l'API C. Le lien est établi dès maintenant pour prouver la dépendance.
find_package(SQLite3 REQUIRED)

qt_standard_project_setup()

configure_file(src/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h @ONLY)

# Lot 1 — l'adaptateur de plateforme, en bibliothèque pour être liable par les tests.
add_library(igiris-platform STATIC
    src/platform/CommandLine.cpp
    src/platform/BatoceraAdapter.cpp
    src/platform/AdapterRegistry.cpp
)

# Lot 2 — lecture du fichier de description des systèmes. Format de la famille
# EmulationStation : aucune connaissance de distribution ici.
add_library(igiris-systems STATIC
    src/systems/EsSystemsParser.cpp
)
target_include_directories(igiris-systems PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(igiris-systems PUBLIC Qt6::Core igiris-platform)
target_include_directories(igiris-platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(igiris-platform PUBLIC Qt6::Core)

qt_add_executable(igiris-frontend src/main.cpp)

qt_add_qml_module(igiris-frontend
    URI IgirisFrontend
    VERSION 1.0
    # Fixé explicitement : Qt 6.4 place les ressources sous « / », Qt 6.5 sous « /qt/qml ».
    # Sans ce réglage, le chemin qrc du main.cpp dépendrait de la version de Qt du système.
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES qml/Main.qml
)

target_include_directories(igiris-frontend PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(igiris-frontend PRIVATE Qt6::Quick SQLite::SQLite3 igiris-platform igiris-systems)

# ---------------------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------------------
enable_testing()

# CLAUDE.md §1 : « Aucune chaîne littérale spécifique à une distribution en dehors de
# l'adaptateur. C'est vérifiable par un simple grep, et ça doit l'être en CI. »
# Ce test existe dès le lot 0 pour protéger tous les lots suivants.
add_test(NAME no-distro-literals
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/check-no-distro-literals.sh
                      ${CMAKE_CURRENT_SOURCE_DIR})

# Le binaire démarre et annonce ses versions, sans écran (livrable du lot 0).
add_test(NAME starts-and-reports-version
         COMMAND igiris-frontend --version)

# Lot 1 — l'adaptateur, testé sur arborescence factice (rootPrefix), donc exécutable ici
# et pas seulement sur l'appareil cible.
add_executable(test-platform-adapter tests/test_platform_adapter.cpp)
target_link_libraries(test-platform-adapter PRIVATE igiris-platform Qt6::Test)
add_test(NAME platform-adapter COMMAND test-platform-adapter)

add_executable(test-es-systems-parser tests/test_es_systems_parser.cpp)
target_link_libraries(test-es-systems-parser PRIVATE igiris-systems Qt6::Test)
add_test(NAME es-systems-parser COMMAND test-es-systems-parser)
