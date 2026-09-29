QT += core qml testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle

TARGET = streamingpreferences-test

INCLUDEPATH += ..

SOURCES += \
    streamingpreferences_test.cpp \
    ../settings/streamingpreferences.cpp

HEADERS += \
    ../settings/streamingpreferences.h \
    ../settings/bitratecalculator.h \
    ../utils.h
