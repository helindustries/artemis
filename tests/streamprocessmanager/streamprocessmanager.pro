QT = core network
CONFIG += console c++17
CONFIG -= app_bundle

TARGET = test_streamprocessmanager
INCLUDEPATH += $$PWD/../../app

SOURCES += \
    main.cpp \
    ../../app/backend/streamprocessmanager.cpp

HEADERS += \
    ../../app/backend/streamprocessmanager.h
