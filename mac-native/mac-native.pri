# Add after the project's CONFIG settings:
# include($$PWD/mac-native/mac-native.pri)
# $$PWD inside this file is the directory containing the portable module.

win32:LIBS += -luser32 -lshell32 -lgdi32

macx {
    CONFIG -= c++11 c++14
    CONFIG += c++17
    INCLUDEPATH += $$PWD
    OBJECTIVE_SOURCES += $$PWD/gdi_compat.mm
    HEADERS += $$PWD/windows.h
    LIBS += -framework Cocoa
    # The Cocoa compatibility layer manages Objective-C objects manually.
    QMAKE_CXXFLAGS += -fno-objc-arc
}
