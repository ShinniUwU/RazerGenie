/*
 * Copyright (C) 2017-2018  Luca Weiss <luca (at) z3ntu (dot) xyz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "customeditor.h"

#include "config.h"
#include "util.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QPushButton>
#include <QtWidgets>

CustomEditor::CustomEditor(libopenrazer::Device *device, bool launchMatrixDiscovery, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("RazerGenie - Custom Editor"));
    this->device = device;

    auto *vbox = new QVBoxLayout(this);

    dimens = device->getMatrixDimensions();

    // Initialize internal colors list
    for (int i = 0; i < dimens.x; i++) {
        colors << QVector<QColor>(dimens.y);

        for (int j = 0; j < dimens.y; j++) {
            colors[i][j] = QColor(Qt::black);
        }
    }

    // Initialize selectedColor variable
    selectedColor = QColor(Qt::green);

    // Initialize drawStatus variable
    drawStatus = DrawStatus::set;

    // Add the main controls to the layout
    vbox->addLayout(buildMainControls());

    QString type = device->getDeviceType();

    QLayout *deviceLayout = nullptr;
    // Build matrix discovery if requested - ignore device type
    if (launchMatrixDiscovery) {
        deviceLayout = buildFallback();
    } else if (type == "keyboard") {
        deviceLayout = buildKeyboard();
    } else if (type == "mousepad") {
        deviceLayout = buildMousemat();
    }

    if (deviceLayout == nullptr) {
        qWarning("Unsupported custom layout for %s with type %s and dimensions %d x %d. Using fallback layout.",
                 qUtf8Printable(device->getDeviceName()), qUtf8Printable(type), dimens.x, dimens.y);
        deviceLayout = buildFallback();
    }

    vbox->addLayout(deviceLayout);

    // Set every LED to "off"/black
    clearAll();
}

CustomEditor::~CustomEditor() = default;

void CustomEditor::closeWindow()
{
    setAttribute(Qt::WA_DeleteOnClose);
    this->close();
}

QLayout *CustomEditor::buildMainControls()
{
    auto *hbox = new QHBoxLayout();

    auto *btnColor = new QPushButton();
    QPalette pal = btnColor->palette();
    pal.setColor(QPalette::Button, QColor(Qt::green));

    btnColor->setAutoFillBackground(true);
    btnColor->setFlat(true);
    btnColor->setPalette(pal);
    btnColor->setMaximumWidth(70);

    QPushButton *btnSet = new QPushButton(tr("Set"));
    QPushButton *btnClear = new QPushButton(tr("Clear"));
    QPushButton *btnClearAll = new QPushButton(tr("Clear All"));

    hbox->addWidget(btnColor);
    hbox->addWidget(btnSet);
    hbox->addWidget(btnClear);
    hbox->addWidget(btnClearAll);

    connect(btnColor, &QPushButton::clicked, this, &CustomEditor::colorButtonClicked);
    connect(btnSet, &QPushButton::clicked, this, &CustomEditor::setDrawStatusSet);
    connect(btnClear, &QPushButton::clicked, this, &CustomEditor::setDrawStatusClear);
    connect(btnClearAll, &QPushButton::clicked, this, &CustomEditor::clearAll);

    return hbox;
}

/*
 * Build layout specific to keyboards, incl. checking physical keyboard layout language.
 */
QLayout *CustomEditor::buildKeyboard()
{
    // Get the matching layout file name for the dimensions
    QString layout;
    if (dimens.x == 6 && dimens.y == 16) { // Razer Blade Stealth (Late 2017)
        layout = "razerblade16";
    } else if (dimens.x == 6 && dimens.y == 22) { // "Normal" Razer keyboad (e.g. BlackWidow Chroma)
        layout = "razerdefault22";
    } else if (dimens.x == 6 && dimens.y == 25) { // Razer Blade Pro 2017
        layout = "razerblade25";
    } else {
        return nullptr;
    }

    QJsonDocument keyboardKeysDoc = loadMatrixLayoutJson(layout);
    if (keyboardKeysDoc.isNull()) {
        return nullptr;
    }

    QString kbdLayout = device->getKeyboardLayout();

    // Show a message when a completely unknown keyboard layout has been detected
    if (kbdLayout == "unknown") {
        util::showInfo(tr("You are using a keyboard with a layout which is not known to the daemon. Please help us by visiting <a href='https://github.com/openrazer/openrazer/wiki/Keyboard-layouts'>https://github.com/openrazer/openrazer/wiki/Keyboard-layouts</a>. Using a fallback layout for now."));
    }

    QJsonObject keyboardKeys = keyboardKeysDoc.object();

    // Check if we have an exact layout match
    if (keyboardKeys.contains(kbdLayout)) {
        return buildLayoutFromJson(keyboardKeys[kbdLayout].toObject());
    }

    // Otherwise try to get a sane fallback
    QStringList langs({ "US", "German" });
    for (const QString &lang : qAsConst(langs)) {
        if (keyboardKeys.contains(lang)) {
            return buildLayoutFromJson(keyboardKeys[lang].toObject());
        }
    }

    return nullptr;
}

/*
 * Build a layout from the provided json.
 *
 * This operates on the object containing the different rows, the keybaord
 * layout needs to be unpacked already.
 *
 * See https://github.com/z3ntu/RazerGenie/wiki/Keyboard-layout-files
 */
QLayout *CustomEditor::buildLayoutFromJson(QJsonObject layout)
{
    auto *vbox = new QVBoxLayout();

    // Iterate over rows in the object
    QJsonObject::const_iterator it;
    for (it = layout.constBegin(); it != layout.constEnd(); ++it) {
        QJsonArray row = (*it).toArray();

        auto *hbox = new QHBoxLayout();
        hbox->setAlignment(Qt::AlignLeft);

        // Iterate over keys in row
        QJsonArray::const_iterator jt;
        for (jt = row.constBegin(); jt != row.constEnd(); ++jt) {
            QJsonObject obj = (*jt).toObject();

            if (!obj["label"].isNull()) {
                MatrixPushButton *btn = new MatrixPushButton(obj["label"].toString());
                int width = obj.contains("width") ? obj.value("width").toInt() : 60;
                int height = /*obj.contains("height") ? obj.value("height").toInt() : */ 63;
                btn->setFixedSize(width, height);
                if (obj.contains("matrix")) {
                    QJsonArray arr = obj["matrix"].toArray();
                    btn->setMatrixPos(arr[0].toInt(), arr[1].toInt());
                }
                if (obj.contains("disabled")) {
                    btn->setEnabled(false);
                }
                /*if(obj["cut"] == "enter") {
                    QPixmap pixmap("../../data/de_DE_mask.png");
                    btn->setMask(pixmap.mask());
                }*/
                connect(btn, &QPushButton::clicked, this, &CustomEditor::onMatrixPushButtonClicked);

                hbox->addWidget(btn);
                matrixPushButtons.append(btn);
            } else {
                auto *spacer = new QSpacerItem(66, 69, QSizePolicy::Fixed, QSizePolicy::Fixed);
                hbox->addItem(spacer);
            }
        }
        vbox->addLayout(hbox);
    }
    return vbox;
}

/*
 * Build layout specific to mousemats (e.g. Firefly)
 */
QLayout *CustomEditor::buildMousemat()
{
    if (dimens.x != 1 || dimens.y != 15) {
        return nullptr;
    }

    auto *hbox = new QHBoxLayout();
    // TODO: Improve visual style of the mousemat grid (make it look like the mousepad!)
    for (int i = 0; i < dimens.y; i++) {
        MatrixPushButton *btn = new MatrixPushButton(QString::number(i));
        btn->setMatrixPos(0, i);

        connect(btn, &QPushButton::clicked, this, &CustomEditor::onMatrixPushButtonClicked);

        hbox->addWidget(btn);
        matrixPushButtons.append(btn);
    }
    return hbox;
}

/*
 * Build a generic layout that has a button for each index
 */
QLayout *CustomEditor::buildFallback()
{
    auto *vbox = new QVBoxLayout();
    for (int i = 0; i < dimens.x; i++) {
        auto *hbox = new QHBoxLayout();
        for (int j = 0; j < dimens.y; j++) {
            MatrixPushButton *btn = new MatrixPushButton(QString::number(i) + ":" + QString::number(j));
            btn->setMatrixPos(i, j);

            connect(btn, &QPushButton::clicked, this, &CustomEditor::onMatrixPushButtonClicked);

            hbox->addWidget(btn);
            matrixPushButtons.append(btn);
        }
        vbox->addLayout(hbox);
    }
    return vbox;
}

/*
 * Load the requested json file from the correct location.
 * On error returns a null QJsonDocument.
 */
QJsonDocument CustomEditor::loadMatrixLayoutJson(QString jsonname)
{
    QFile *file; // Pointer to file object to use
    QFile file_devel("../../data/matrix_layouts/" + jsonname + ".json"); // File during developemnt
    QFile file_prod(QString(RAZERGENIE_DATADIR) + "/matrix_layouts/" + jsonname + ".json"); // File for production

    // Try to open the dev file (higher priority)
    if (file_devel.open(QIODevice::ReadOnly)) {
        qDebug() << "RazerGenie: Using the development " + jsonname + ".json file.";
        file = &file_devel;
    } else {
        qDebug() << "RazerGenie: Development " + jsonname + ".json failed to open. Trying the production location. Error: " << file_devel.errorString();

        // Try to open the production file
        if (file_prod.open(QIODevice::ReadOnly)) {
            file = &file_prod;
        } else {
            util::showInfo(tr("The file %1.json, used for the custom editor failed to load: %2\nThe editor won't open now.").arg(jsonname, file_prod.errorString()));
            return QJsonDocument();
        }
    }

    // Read the file
    QTextStream in(file);
    QString data = in.readAll();
    file->close();

    // Convert it to a QJsonObject
    return QJsonDocument::fromJson(data.toUtf8());
}

bool CustomEditor::updateKeyrow(int row)
{
    return device->defineCustomFrame(row, 0, dimens.y - 1, colors[row]) && device->displayCustomFrame();
}

void CustomEditor::clearAll()
{
    QVector<QColor> blankColors;
    // Initialize the array with the width of the matrix with black = off
    for (int i = 0; i < dimens.y; i++) {
        blankColors << QColor(Qt::black);
    }

    // Send one request per row
    for (int i = 0; i < dimens.x; i++) {
        device->defineCustomFrame(i, 0, dimens.y - 1, blankColors);
    }

    device->displayCustomFrame();

    // Reset view
    for (auto matrixPushButton : qAsConst(matrixPushButtons)) {
        matrixPushButton->resetButtonColor();
    }

    // Reset model
    for (auto &color : colors) {
        for (auto &j : color) {
            j = QColor(Qt::black);
        }
    }
}

void CustomEditor::colorButtonClicked()
{
    auto *sender = qobject_cast<QPushButton *>(QObject::sender());

    QPalette pal(sender->palette());

    QColor oldColor = pal.color(QPalette::Button);

    QColor color = QColorDialog::getColor(oldColor);
    if (color.isValid()) {

        // Colorize the button
        pal.setColor(QPalette::Button, color);
        sender->setPalette(pal);

        // Set the color for other methods to use
        selectedColor = color;
    } else {
        qDebug() << "User cancelled the dialog.";
    }
}

void CustomEditor::onMatrixPushButtonClicked()
{
    auto *sender = dynamic_cast<MatrixPushButton *>(QObject::sender());
    QPair<int, int> pos = sender->matrixPos();
    if (drawStatus == DrawStatus::set) {
        // Set color in model
        colors[pos.first][pos.second] = selectedColor;
        // Set color in view
        sender->setButtonColor(selectedColor);
    } else if (drawStatus == DrawStatus::clear) {
        qDebug() << "Clearing color.";
        // Set color in model
        colors[pos.first][pos.second] = QColor(Qt::black);
        // Set color in view
        sender->resetButtonColor();
    } else {
        qDebug() << "RazerGenie: Unhandled DrawStatus: " << drawStatus;
    }
    // Set color on device
    updateKeyrow(pos.first);
}

void CustomEditor::setDrawStatusSet()
{
    drawStatus = DrawStatus::set;
}

void CustomEditor::setDrawStatusClear()
{
    drawStatus = DrawStatus::clear;
}
