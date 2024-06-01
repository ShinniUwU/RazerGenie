// Copyright (C) 2016-2018  Luca Weiss <luca (at) z3ntu (dot) xyz>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "razergenie.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("RazerGenie");
    QApplication::setApplicationVersion(RAZERGENIE_VERSION);
    QApplication::setOrganizationName("razergenie"); // for QSettings
    QApplication::setDesktopFileName("xyz.z3ntu.razergenie");

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();

    parser.process(app);

    QTranslator translator;
#if defined(Q_OS_MACOS)
    QString translationsDirectory = QApplication::applicationDirPath() + "/../Resources/translations/";
#else
    QString translationsDirectory = QString(RAZERGENIE_DATADIR) + "/translations/";
#endif
    bool ret = translator.load(QLocale::system(), QString(), QString(), translationsDirectory);
    qDebug() << "RazerGenie translation loaded:" << ret;
    app.installTranslator(&translator);

    QTranslator libopenrazerTranslator;
    ret = libopenrazer::loadTranslations(&libopenrazerTranslator);
    qDebug() << "libopenrazer translations loaded:" << ret;
    app.installTranslator(&libopenrazerTranslator);

    RazerGenie w;
    w.show();

    return app.exec();
}
