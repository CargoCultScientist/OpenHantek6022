# openhantek/CMakeLists.txt

project(OpenHantek CXX)

if(WIN32)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    set(CMAKE_EXE_LINKER_FLAGS "-s")
    set(EXECTYPE WIN32)
endif()


find_package(Qt6PrintSupport REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS OpenGLWidgets)
find_package(OpenGL)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# After Qt5 -> Qt6 transition use minimum Qt 6.2 LTS stadard
if (Qt6Widgets_VERSION VERSION_LESS 6.2.0)
    message(FATAL_ERROR "Minimum supported Qt6 version is 6.2.0!")
else()
    message("-- Qt6 version is ${Qt6Widgets_VERSION}")
endif()

# include directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
include_directories(src/ src/hantekdso src/widgets src/docks src/configdialog)

# collect sources and other files
file(GLOB_RECURSE SRC CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "src/*.h")
file(GLOB_RECURSE UI "src/*.ui")
file(GLOB_RECURSE QRC "res/*.qrc")

if(WIN32)
    file(GLOB_RECURSE RC "res/*.rc")
endif()

add_custom_target(format SOURCES ".clang-format"
    COMMAND "clang-format" "-style=file" "-i" "-sort-includes" ${SRC} ${HEADERS})

# here the translation magic happens
include(translations/Translations.cmake)

option(USE_OPENHANTEK_DRIVER "Use OpenHantek Windows driver" ON)

# Enable MacOSX bundle magic in the next line
option(BUILD_MACOSX_BUNDLE "Build MacOS app bundle" ON)
#
if( APPLE AND BUILD_MACOSX_BUNDLE )
    set( EXECTYPE MACOSX_BUNDLE )
    set( MACOSX_BUNDLE_BUNDLE_NAME "${PROJECT_NAME}" )
    set( MACOSX_BUNDLE_INFO_STRING "Digital oscilloscope software for Hantek DSO6022" )
    set( MACOSX_BUNDLE_GUI_IDENTIFIER "org.openhantek.${PROJECT_NAME}" )
    set( MACOSX_BUNDLE_SHORT_VERSION_STRING ${VCS_REVISION} )
    set( MACOSX_BUNDLE_BUNDLE_VERSION ${VERSION} )
    string(TIMESTAMP THISYEAR "%Y")
    set( MACOSX_BUNDLE_COPYRIGHT "(C) 2012-${THISYEAR} OpenHantek community" )
    set( MACOSX_BUNDLE_ICON_FILE openhantek.icns )
    set( ICONS ${CMAKE_CURRENT_SOURCE_DIR}/res/images/openhantek.icns )
    set_source_files_properties( ${ICONS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources" )
endif()

# make executable
list(REMOVE_ITEM SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
add_library(ohcore OBJECT ${SRC} ${HEADERS} ${UI}
${QRC} ${RC} ${TRANSLATION_BIN_FILES} ${TRANSLATION_QRC} ${ICONS})
target_include_directories(ohcore PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/ohcore_autogen/include")
add_executable(${PROJECT_NAME} ${EXECTYPE} src/main.cpp)
target_link_libraries(${PROJECT_NAME} ohcore)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME OpenHantekLab)
target_link_libraries(ohcore PUBLIC Qt6::Widgets Qt6::PrintSupport Qt6::OpenGL Qt6::OpenGLWidgets ${OPENGL_LIBRARIES})
target_link_libraries(${PROJECT_NAME} Qt6::Widgets Qt6::PrintSupport Qt6::OpenGL Qt6::OpenGLWidgets ${OPENGL_LIBRARIES} )
if( APPLE AND BUILD_MACOSX_BUNDLE )
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    target_link_libraries(${PROJECT_NAME} Qt6::DBus )
endif()
target_compile_features(${PROJECT_NAME} PRIVATE cxx_range_for)

if(MSVC)
    include(../cmake/fftw_on_windows.cmake)
    include(../cmake/libusb_on_windows.cmake)
    target_include_directories(ohcore PUBLIC "${CMAKE_BINARY_DIR}/fftw" "${LIBUSB_DIR}" "${LIBUSB_DIR}/libusb-1.0")
    target_link_libraries(ohcore PUBLIC "${CMAKE_BINARY_DIR}/fftw/libfftw3-3.lib" "${LIBUSB_DIR}/${ARCH}/libusb-1.0.lib")
    include(../cmake/copy_qt6_dlls_to_bin_dir.cmake)
    target_compile_options(${PROJECT_NAME} PRIVATE "/W4" "/wd4251" "/wd4127" "/wd4275" "/wd4200" "/nologo" "/J" "/Zi")
    target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:DEBUG>:/MDd>")
else()
    if( APPLE AND BUILD_MACOSX_BUNDLE )
        # Use own template that defines NSPrincipalClass=NSApplication & NSHighResolutionCapable=True
        set_target_properties( ${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_LIST_DIR}/../cmake/OpenHantekBundleInfo.plist.in
        )
    endif()
    find_package(libusb REQUIRED)
    target_include_directories(${PROJECT_NAME} PRIVATE ${LIBUSB_INCLUDE_DIRS})
    target_include_directories(ohcore PUBLIC ${LIBUSB_INCLUDE_DIRS})
    target_link_libraries(ohcore PUBLIC ${LIBUSB_LIBRARIES})
    target_link_libraries(${PROJECT_NAME} ${LIBUSB_LIBRARIES})

    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} ${CMAKE_THREAD_LIBS_INIT})

    find_package(FFTW REQUIRED)
    target_include_directories(${PROJECT_NAME} PRIVATE ${FFTW_INCLUDE_DIRS})
    target_include_directories(ohcore PUBLIC ${FFTW_INCLUDE_DIRS})
    target_link_libraries(ohcore PUBLIC ${FFTW_LIBRARIES} Threads::Threads)
    target_link_libraries(${PROJECT_NAME} ${FFTW_LIBRARIES})

    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wno-long-long -pedantic)
    target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:DEBUG>:-DDEBUG>")
    target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:DEBUG>:-O0>")
endif()

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(oh_regression ../tests/regression.cpp)
    target_link_libraries(oh_regression PRIVATE ohcore Qt6::Test)
    add_test(NAME regression COMMAND oh_regression)
    set_tests_properties(regression PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(oh_resource_regression ../tests/resource_regression.cpp)
    target_link_libraries(oh_resource_regression PRIVATE ohcore Qt6::Test)
    add_test(NAME resource_regression COMMAND oh_resource_regression)
    set_tests_properties(resource_regression PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(WIN32)
    include(../cmake/docs_on_windows.cmake)
    include(../cmake/windows_driver.cmake)
endif()

# install commands
if ( NOT (APPLE AND BUILD_MACOSX_BUNDLE) )
    install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION "bin")
endif()
