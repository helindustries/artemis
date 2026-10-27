QT = core gui
CONFIG += console c++17
CONFIG -= app_bundle

TARGET = test_streamprofiles
INCLUDEPATH += $$PWD/../../app

SOURCES += \
    main.cpp \
    ../../app/settings/streamprofiles.cpp

HEADERS += \
    ../../app/settings/streamprofiles.h
