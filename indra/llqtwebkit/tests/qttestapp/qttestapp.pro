TEMPLATE = app
TARGET = 
DEPENDPATH += .
INCLUDEPATH += .
INCLUDEPATH += ../../
CONFIG -= app_bundle

include(../../static.pri)

QT += webkit opengl network

unix {
    LIBS += $$PWD/../../libllqtwebkit.a
}

!mac {
unix {
    DEFINES += LL_LINUX
}
}

mac {
    DEFINES += LL_OSX
}


win32{
    DEFINES += _WINDOWS
    INCLUDEPATH += ../
    DESTDIR=../build
    release {
      LIBS += $$PWD/../../Release/llqtwebkit.lib
    }
}

# Input
SOURCES += main.cpp
FORMS += window.ui
