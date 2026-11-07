TEMPLATE = app
TARGET = tandem
QT += quick quickcontrols2 network dbus sql
CONFIG += c++20 release
QMAKE_CXXFLAGS_RELEASE += -O2
DEFINES += QT_NO_KEYWORDS

SOURCES += $$files(src/*.cpp)
HEADERS += $$files(src/*.h)
RESOURCES += qml.qrc

target.path = $$(HOME)/.local/bin
INSTALLS += target
