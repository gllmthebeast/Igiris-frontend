cmake_minimum_required(VERSION 3.22)

# SemVer strict (CLAUDE.md §15) : cette version est la seule source de vérité.
# Elle alimente version.h, et c'est elle qu'affiche `igiris-frontend --version`.
project(igiris-frontend VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick Test)

# Accès à l'export par l'API C de SQLite, et non par le pilote Qt SQL : le §2 impose
# file:games.db?immutable=1 avec SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, ce qui s'exprime
# sans ambiguïté avec l'API C. Le lien est établi dès maintenant pour prouver la dépendance.
find_package(SQLite3 REQUIRED)
find_package(ZLIB REQUIRED)

qt_standard_project_setup()

configure_file(src/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h @ONLY)

# Lot 1 — l'adaptateur de plateforme, en bibliothèque pour être liable par les tests.
add_library(igiris-platform STATIC
    src/platform/CommandLine.cpp
    src/platform/BatoceraAdapter.cpp
    src/platform/RecalboxAdapter.cpp
    src/platform/AdapterRegistry.cpp
    # Le lecteur du format EmulationStation vit avec les adaptateurs qui s'en servent :
    # c'est une couche FORMAT, sous l'adaptateur. L'en tenir séparé créait une dépendance
    # circulaire dès qu'un adaptateur a dû lire lui-même sa description (lot 9).
    src/systems/EsSystemsParser.cpp
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

# Lot 3 — chargeur de l'export SQLite. Façade du §9.1 : au-dessus de cette bibliothèque,
# le vocabulaire est « platformKey » et le nom réel de la colonne n'existe plus.
add_library(igiris-catalog STATIC
    src/catalog/ExportDatabase.cpp
)
target_include_directories(igiris-catalog PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(igiris-catalog PUBLIC Qt6::Core SQLite::SQLite3)

# Lot 4 — scanner de ROMs. zlib fournit crc32() et l'inflate brut des entrées de zip.
add_library(igiris-scan STATIC
    src/scan/RomHasher.cpp
    src/scan/ScanCache.cpp
    src/scan/RomScanner.cpp
)
target_include_directories(igiris-scan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(igiris-scan PUBLIC Qt6::Core igiris-catalog ZLIB::ZLIB SQLite::SQLite3)

# Lot 5 — le modèle exposé à QML. Le §12 veut très peu de C++ : cette couche expose les
# données et rien d'autre.
add_library(igiris-ui STATIC src/ui/GameListModel.cpp src/ui/GameDetailModel.cpp)
target_include_directories(igiris-ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(igiris-ui PUBLIC Qt6::Core igiris-catalog igiris-platform)

qt_add_executable(igiris-frontend src/main.cpp)

qt_add_qml_module(igiris-frontend
    URI IgirisFrontend
    VERSION 1.0
    # Fixé explicitement : Qt 6.4 place les ressources sous « / », Qt 6.5 sous « /qt/qml ».
    # Sans ce réglage, le chemin qrc du main.cpp dépendrait de la version de Qt du système.
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES qml/Main.qml qml/FilterCell.qml qml/GameDetail.qml
)

target_include_directories(igiris-frontend PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(igiris-frontend PRIVATE Qt6::Quick SQLite::SQLite3 igiris-platform igiris-catalog igiris-scan igiris-ui)

# ---------------------------------------------------------------------------------------
# Installation et empaquetage (lot 10)
# ---------------------------------------------------------------------------------------
#
# Le QML est compilé DANS le binaire (qt_add_qml_module + ressources) : il n'y a donc rien
# à installer à côté. C'est ce qui rend l'intégration au démarrage aussi mince — une ligne
# à changer, un fichier à copier (§16).
include(GNUInstallDirs)

install(TARGETS igiris-frontend RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES tools/fetch-export.sh tools/probe.py
        DESTINATION ${CMAKE_INSTALL_DATADIR}/igiris-frontend/tools
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
                    WORLD_READ WORLD_EXECUTE)
install(DIRECTORY packaging/ DESTINATION ${CMAKE_INSTALL_DATADIR}/igiris-frontend/packaging)
install(FILES docs/LOTS.md CLAUDE.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})

# §15 : « Itérations livrées sous forme d'archives zip. »
set(CPACK_PACKAGE_NAME igiris-frontend)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME
    "igiris-frontend-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Frontend de jeux unifie pour distributions de retrogaming")
set(CPACK_GENERATOR "ZIP;TGZ")
set(CPACK_SOURCE_GENERATOR "")
# L'export est un ARTEFACT : jamais dans la livraison (§15).
set(CPACK_PACKAGE_IGNORE_FILES "/build/;/data/;/.git/")
include(CPack)

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

# Lot 9 — le test de conception du §13.
add_executable(test-recalbox-adapter tests/test_recalbox_adapter.cpp)
target_link_libraries(test-recalbox-adapter PRIVATE igiris-platform Qt6::Test)
add_test(NAME recalbox-adapter COMMAND test-recalbox-adapter)

add_executable(test-es-systems-parser tests/test_es_systems_parser.cpp)
target_link_libraries(test-es-systems-parser PRIVATE igiris-platform Qt6::Test)
add_test(NAME es-systems-parser COMMAND test-es-systems-parser)

add_executable(test-export-database tests/test_export_database.cpp)
target_link_libraries(test-export-database PRIVATE igiris-catalog Qt6::Test)
# Permet au test d'aller chercher le vrai export s'il est présent, sans chemin en dur.
target_compile_definitions(test-export-database
                           PRIVATE IGIRIS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME export-database COMMAND test-export-database)

add_executable(test-rom-scanner tests/test_rom_scanner.cpp)
target_link_libraries(test-rom-scanner PRIVATE igiris-scan Qt6::Test)
target_include_directories(test-rom-scanner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
add_test(NAME rom-scanner COMMAND test-rom-scanner)

add_executable(test-game-list-model tests/test_game_list_model.cpp)
target_link_libraries(test-game-list-model PRIVATE igiris-ui Qt6::Test)
add_test(NAME game-list-model COMMAND test-game-list-model)
# Ce test construit une QGuiApplication : sans écran, il lui faut la plateforme offscreen.
set_tests_properties(game-list-model PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(test-game-detail-model tests/test_game_detail_model.cpp)
target_link_libraries(test-game-detail-model PRIVATE igiris-ui Qt6::Test)
add_test(NAME game-detail-model COMMAND test-game-detail-model)
set_tests_properties(game-detail-model PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
