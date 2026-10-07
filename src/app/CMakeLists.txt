# dr_stein: the Qt Quick application. One QML module ("DrStein") holds the C++
# facades from ../qt and every QML file; the Qt-free logic is drstein::core.
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Svg)
qt_standard_project_setup(REQUIRES 6.5)

set(DRSTEIN_QT_SOURCES
  ../qt/util.hpp
  ../qt/settings.hpp ../qt/settings.cpp
  ../qt/report_model.hpp ../qt/report_model.cpp
  ../qt/job_runner.hpp ../qt/job_runner.cpp
  ../qt/models.hpp ../qt/models.cpp
  ../qt/workspace.hpp ../qt/workspace.cpp
  ../qt/struct_inspector.hpp ../qt/struct_inspector.cpp
  ../qt/file_browser.hpp ../qt/file_browser.cpp
  ../qt/mounts_model.hpp ../qt/mounts_model.cpp
  ../qt/image_controller.hpp ../qt/image_controller.cpp
  ../qt/partition_editor.hpp ../qt/partition_editor.cpp
  ../qt/tools_controller.hpp ../qt/tools_controller.cpp
  ../qt/profiles_model.hpp ../qt/profiles_model.cpp
)

set(DRSTEIN_QML_FILES
  qml/Main.qml
  qml/Theme.qml
  qml/Icons.js
  qml/components/Icon.qml
  qml/components/IconButton.qml
  qml/components/StButton.qml
  qml/components/StTag.qml
  qml/components/StCard.qml
  qml/components/Kicker.qml
  qml/components/StSegmented.qml
  qml/components/StTextField.qml
  qml/components/StCheck.qml
  qml/components/StRadio.qml
  qml/components/StCombo.qml
  qml/components/FadeDivider.qml
  qml/components/SegmentBar.qml
  qml/components/UsageBar.qml
  qml/components/KeyValueGrid.qml
  qml/components/HealthDot.qml
  qml/components/ProgressGlow.qml
  qml/components/JobCard.qml
  qml/components/EmptyState.qml
  qml/components/ToastHost.qml
  qml/components/ColumnHeader.qml
  qml/components/TopBar.qml
  qml/components/WindowControls.qml
  qml/components/FrameResizer.qml
  qml/components/Sidebar.qml
  qml/components/IdentityStrip.qml
  qml/views/TopologyView.qml
  qml/views/HexView.qml
  qml/views/BrowseView.qml
  qml/views/ImageView.qml
  qml/views/ProfilesView.qml
  qml/views/ToolsView.qml
  qml/views/PartitionsView.qml
  qml/dialogs/StDialog.qml
  qml/dialogs/PassphraseDialog.qml
  qml/dialogs/ConfirmDialog.qml
  qml/dialogs/PartitionDialog.qml
  qml/dialogs/NewProfileDialog.qml
  qml/dialogs/SettingsDialog.qml
  qml/dialogs/MountsDialog.qml
  qml/dialogs/KeysDialog.qml
)
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

set(DRSTEIN_PLATFORM_SOURCES platform/window_chrome.hpp platform/window_chrome.cpp)
if(APPLE)
  list(APPEND DRSTEIN_PLATFORM_SOURCES platform/window_chrome_mac.mm)
endif()

qt_add_executable(dr_stein MACOSX_BUNDLE main.cpp ${DRSTEIN_QT_SOURCES} ${DRSTEIN_PLATFORM_SOURCES})

qt_add_qml_module(dr_stein
  URI DrStein
  VERSION 1.0
  QML_FILES ${DRSTEIN_QML_FILES}
)

target_include_directories(dr_stein PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../qt ${CMAKE_CURRENT_SOURCE_DIR}/platform)
if(APPLE)
  target_link_libraries(dr_stein PRIVATE "-framework AppKit")
endif()
# libstein has a slots() method; Qt's keyword macros would eat it.
target_compile_definitions(dr_stein PRIVATE QT_NO_KEYWORDS)
target_link_libraries(dr_stein PRIVATE
  drstein::core drstein_warnings
  Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Svg)
set_target_properties(dr_stein PROPERTIES
  OUTPUT_NAME "Dr Stein"
  MACOSX_BUNDLE_BUNDLE_NAME "Dr Stein"
  MACOSX_BUNDLE_GUI_IDENTIFIER "local.dr-stein.app"
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
  WIN32_EXECUTABLE TRUE)

install(TARGETS dr_stein BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
