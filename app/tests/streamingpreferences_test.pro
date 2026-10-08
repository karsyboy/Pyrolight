QT += core qml testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle

TARGET = streamingpreferences-test

INCLUDEPATH += ..

SOURCES += \
    streamingpreferences_test.cpp \
    ../settings/streamingpreferences.cpp \
    ../streaming/vrrratepolicy.cpp

HEADERS += \
    ../settings/streamingpreferences.h \
    ../settings/bitratecalculator.h \
    ../settings/vrrtimingoptions.h \
    ../streaming/vrrratepolicy.h \
    ../utils.h
