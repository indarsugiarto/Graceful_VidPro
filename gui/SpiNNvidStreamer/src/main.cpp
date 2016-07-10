#include "vidstreamer.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    vidStreamer w;
    w.show();

    return a.exec();
}
