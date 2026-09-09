cmake_minimum_required(VERSION 3.21)

set(LA_VERSION_FILE "${CMAKE_SOURCE_DIR}/VERSION")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${LA_VERSION_FILE}")
file(STRINGS "${LA_VERSION_FILE}" LA_VERSION LIMIT_COUNT 1)
string(STRIP "${LA_VERSION}" LA_VERSION)

project(LanAtlas VERSION "${LA_VERSION}" LANGUAGES CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/third_party/morf/morfdeploy/record_compile.cmake")
morf_enable_compile_recording()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_subdirectory(third_party/morf/beacon)
add_subdirectory(third_party/morf/update)

set(LA_SOURCES
    src/main.cpp
    src/AppSettings.cpp
    src/crypto/RsaPkcs1.cpp
    src/crypto/AesCbc.cpp
    src/core/Device.cpp
    src/core/Inventory.cpp
    src/core/OuiLookup.cpp
    src/core/IspDetect.cpp
    src/core/MorfService.cpp
    src/core/NetHints.cpp
    src/core/WebLookup.cpp
    src/core/WebEnricher.cpp
    src/scan/HttpUtil.cpp
    src/scan/LanContext.cpp
    src/scan/ArpTable.cpp
    src/scan/PingSweep.cpp
    src/scan/PortProbe.cpp
    src/scan/MorfHttpQualify.cpp
    src/scan/SsdpProbe.cpp
    src/scan/MdnsProbe.cpp
    src/scan/NetbiosProbe.cpp
    src/scan/ScanOrchestrator.cpp
    src/scan/BeaconListener.cpp
    src/scan/TuyaListener.cpp
    src/control/TuyaClient.cpp
    src/control/TuyaDriver.cpp
    src/control/TuyaController.cpp
    src/adapters/LiveboxClient.cpp
    src/adapters/DecoClient.cpp
    src/ui/Theme.cpp
    src/ui/PilotPanel.cpp
    src/ui/MainWindow.cpp
    src/ui/TopologyView.cpp
    src/ui/MorfServicesPage.cpp
    src/ui/SettingsDialog.cpp
    resources/lanatlas.qrc
)

# Icone : ressource Qt (icone de fenetre) + .rc Windows (icone de l'executable).
if(WIN32)
    enable_language(RC)
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I\"${CMAKE_CURRENT_SOURCE_DIR}/resources\"")
    list(APPEND LA_SOURCES resources/lanatlas.rc)
endif()

add_executable(LanAtlas WIN32 ${LA_SOURCES})
target_include_directories(LanAtlas PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(LanAtlas PRIVATE
    Qt6::Widgets
    Qt6::Network
    morfBeacon::morfBeacon
    morfUpdate::morfUpdate
    morfUpdate::Widgets
)
target_compile_definitions(LanAtlas PRIVATE LA_APP_VERSION="${LA_VERSION}")

# Table OUI embarquee : reconstruction possible, donc copie a cote du binaire.
add_custom_command(TARGET LanAtlas POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/data/oui.json"
        "$<TARGET_FILE_DIR:LanAtlas>/oui.json")

if(WIN32)
    target_link_libraries(LanAtlas PRIVATE iphlpapi ws2_32)
endif()
if(MSVC)
    target_compile_options(LanAtlas PRIVATE /utf-8)
endif()

if(WIN32)
    find_program(WINDEPLOYQT_EXE windeployqt
        HINTS "${Qt6_DIR}/../../../bin" "$ENV{QT_ROOT}/bin")
    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET LanAtlas POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}" --no-translations --compiler-runtime "$<TARGET_FILE:LanAtlas>"
            COMMENT "Deploiement des DLL Qt a cote de l'executable")
    endif()
    if(NOT MSVC)
        function(lanatlas_msys_path windows_path output)
            string(REGEX REPLACE "^([A-Za-z]):/" "/\\1/" converted "${windows_path}")
            string(SUBSTRING "${converted}" 1 1 drive)
            string(TOLOWER "${drive}" drive)
            string(SUBSTRING "${converted}" 2 -1 tail)
            set(${output} "/${drive}${tail}" PARENT_SCOPE)
        endfunction()
        lanatlas_msys_path("${CMAKE_SOURCE_DIR}/scripts/windows/deploy-mingw.sh"
                           LANATLAS_DEPLOY_SCRIPT)
        lanatlas_msys_path("${CMAKE_CURRENT_BINARY_DIR}" LANATLAS_TARGET_DIR)
        set(_lanatlas_cxx "${CMAKE_CXX_COMPILER}")
        if(NOT EXISTS "${_lanatlas_cxx}")
            find_program(_lanatlas_cxx NAMES "${CMAKE_CXX_COMPILER}" g++.exe)
        endif()
        get_filename_component(LANATLAS_COMPILER_BIN "${_lanatlas_cxx}" DIRECTORY)
        get_filename_component(LANATLAS_MINGW_ROOT "${LANATLAS_COMPILER_BIN}" DIRECTORY)
        get_filename_component(LANATLAS_MSYS_ROOT "${LANATLAS_MINGW_ROOT}" DIRECTORY)
        find_program(LANATLAS_MSYS_BASH bash.exe
            HINTS "${LANATLAS_MSYS_ROOT}/usr/bin"
            NO_DEFAULT_PATH)
        if(LANATLAS_MSYS_BASH)
            add_custom_command(TARGET LanAtlas POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E env
                    "PATH=${LANATLAS_MSYS_ROOT}/usr/bin;${LANATLAS_COMPILER_BIN}"
                    --
                    "${LANATLAS_MSYS_BASH}" "${LANATLAS_DEPLOY_SCRIPT}" "${LANATLAS_TARGET_DIR}"
                VERBATIM
                COMMENT "Copie des DLL MinGW a cote de l'executable")
        endif()
    endif()
endif()

morf_record_target_build(LanAtlas "LanAtlas")
