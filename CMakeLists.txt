cmake_minimum_required(VERSION 3.22)
project(kwin-gpu-effects LANGUAGES CXX)

find_package(ECM 6.0 REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Widgets DBus)
find_package(KF6 REQUIRED COMPONENTS Config CoreAddons WindowSystem KCMUtils I18n)
find_package(KDecoration3 REQUIRED)
find_package(KWin REQUIRED)
find_package(KWinDBusInterface REQUIRED)
find_package(XCB REQUIRED COMPONENTS XCB)

# the factory macro wraps K_PLUGIN_FACTORY, tell automoc
list(APPEND CMAKE_AUTOMOC_MACRO_NAMES KWIN_EFFECT_FACTORY_SUPPORTED)

include_directories(shared)

# A settings page: copied from the matching KWin effect, same strings, so it reuses kwin's translations.
function(gpu_add_effect_config name)
    list(REMOVE_AT ARGV 0)
    kcoreaddons_add_plugin(${name} SOURCES ${ARGV} INSTALL_NAMESPACE "kwin/effects/configs")
    target_compile_definitions(${name} PRIVATE TRANSLATION_DOMAIN="kwin")
    target_link_libraries(${name} KWin::kwin KF6::KCMUtils KF6::ConfigGui KF6::I18n Qt::DBus)
endfunction()

add_subdirectory(gpuwobbly)
add_subdirectory(gpublur)
