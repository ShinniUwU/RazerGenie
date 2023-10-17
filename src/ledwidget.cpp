// Copyright (C) 2018  Luca Weiss <luca (at) z3ntu (dot) xyz>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ledwidget.h"

#include "util.h"

#include <QColorDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>

LedWidget::LedWidget(QWidget *parent, libopenrazer::Led *led)
    : QWidget(parent)
{
    this->mLed = led;

    auto *verticalLayout = new QVBoxLayout(this);

    // Set appropriate text
    QLabel *lightingLocationLabel = new QLabel(tr("Lighting %1").arg(libopenrazer::ledIdToStringTable.value(led->getLedId(), "error")));

    auto *lightingHBox = new QHBoxLayout();
    verticalLayout->addWidget(lightingLocationLabel);
    verticalLayout->addLayout(lightingHBox);

    auto *comboBox = new QComboBox;
    QLabel *brightnessLabel = nullptr;
    QSlider *brightnessSlider = nullptr;

    comboBox->setObjectName("combobox");
    comboBox->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed));

    // TODO Sync effects in comboboxes & colorStuff when the sync checkbox is active

    openrazer::RazerEffect currentEffect = openrazer::RazerEffect::Static;
    try {
        currentEffect = led->getCurrentEffect();
    } catch (const libopenrazer::DBusException &e) {
        qWarning("Failed to get current effect");
    }
    QVector<openrazer::RGB> currentColors;
    try {
        currentColors = led->getCurrentColors();
    } catch (const libopenrazer::DBusException &e) {
        qWarning("Failed to get current colors");
    }

    // Add items from capabilities
    for (auto ledFx : libopenrazer::ledFxList) {
        if (led->hasFx(ledFx.getIdentifier())) {
            comboBox->addItem(ledFx.getDisplayString(), QVariant::fromValue(ledFx));
            // Set selection to current effect
            if (ledFx.getIdentifier() == currentEffect) {
                comboBox->setCurrentIndex(comboBox->count() - 1);
            }
        }
    }

    // Connect signal from combobox
    connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LedWidget::fxComboboxChanged);

    // Brightness slider
    if (led->hasBrightness()) {
        brightnessLabel = new QLabel(tr("Brightness"));
        brightnessSlider = new QSlider(Qt::Horizontal, this);
        brightnessSlider->setMaximum(255);
        uchar brightness;
        try {
            brightness = led->getBrightness();
        } catch (const libopenrazer::DBusException &e) {
            qWarning("Failed to get brightness");
            brightness = 100;
        }
        brightnessSlider->setValue(brightness);
        connect(brightnessSlider, &QSlider::valueChanged, this, &LedWidget::brightnessSliderChanged);
    }

    // Only add combobox if a capability was actually added
    if (comboBox->count() != 0) {
        lightingHBox->addWidget(comboBox);

        /* Color buttons */
        for (int i = 1; i <= 3; i++) {
            auto *colorButton = new QPushButton(this);
            QPalette pal = colorButton->palette();
            if (i - 1 < currentColors.count()) {
                openrazer::RGB color = currentColors.at(i - 1);
                pal.setColor(QPalette::Button, { color.r, color.g, color.b });
            } else {
                pal.setColor(QPalette::Button, QColor(Qt::green));
            }

            colorButton->setAutoFillBackground(true);
            colorButton->setFlat(true);
            colorButton->setPalette(pal);
            colorButton->setMaximumWidth(70);
            colorButton->setObjectName("colorbutton" + QString::number(i));
            lightingHBox->addWidget(colorButton);

            libopenrazer::RazerCapability capability = comboBox->currentData().value<libopenrazer::RazerCapability>();
            if (capability.getNumColors() < i)
                colorButton->hide();
            connect(colorButton, &QPushButton::clicked, this, &LedWidget::colorButtonClicked);
        }

        /* Wave left/right radio buttons */
        for (int i = 1; i <= 2; i++) {
            QString name;
            if (i == 1)
                name = tr("Left");
            else
                name = tr("Right");
            auto *radio = new QRadioButton(name, this);
            radio->setObjectName("radiobutton" + QString::number(i));
            if (i == 1) // set the 'left' checkbox to activated
                radio->setChecked(true);
            // Hide radio button when we don't need it
            if (currentEffect != openrazer::RazerEffect::Wave) {
                radio->hide();
            }
            lightingHBox->addWidget(radio);
            connect(radio, &QRadioButton::toggled, this, &LedWidget::waveRadioButtonChanged);
        }
    } else {
        // Otherwise delete comboBox again
        delete comboBox;
        comboBox = nullptr;
    }

    /* Brightness sliders */
    if (brightnessLabel != nullptr && brightnessSlider != nullptr) { // only if brightness capability exists
        verticalLayout->addWidget(brightnessLabel);
        auto *hboxSlider = new QHBoxLayout();
        QLabel *brightnessSliderValue = new QLabel;
        hboxSlider->addWidget(brightnessSlider);
        hboxSlider->addWidget(brightnessSliderValue);
        verticalLayout->addLayout(hboxSlider);
    }
}

void LedWidget::colorButtonClicked()
{
    auto *sender = qobject_cast<QPushButton *>(QObject::sender());

    QPalette pal(sender->palette());

    QColor oldColor = pal.color(QPalette::Button);

    QColor color = QColorDialog::getColor(oldColor);
    if (color.isValid()) {
        pal.setColor(QPalette::Button, color);
        sender->setPalette(pal);
    } else {
        qInfo("User cancelled the dialog.");
    }
    applyEffect();
}

void LedWidget::fxComboboxChanged(int index)
{
    auto *sender = qobject_cast<QComboBox *>(QObject::sender());
    libopenrazer::RazerCapability capability;

    /* In theory we could remove half of this special handling because
     * .value<>() will give us a default RazerCapability anyways if it's
     * missing. But to be explicit let's do it like this. */
    bool isCustomEffect = sender->itemText(index) == "Custom Effect";
    if (!isCustomEffect) {
        QVariant itemData = sender->itemData(index);
        if (!itemData.canConvert<libopenrazer::RazerCapability>())
            throw new std::runtime_error("Expected to be able to convert itemData into RazerCapability");
        capability = itemData.value<libopenrazer::RazerCapability>();
    } else {
        /* We're fine with getting an empty RazerCapability as we do want to
         * reset all the extra buttons etc. We just don't want to actually do
         * more than UI work with this though. */
        capability = libopenrazer::RazerCapability();
    }

    // Remove "Custom Effect" entry when you switch away from it - only gets added by the Custom Editor button
    if (!isCustomEffect)
        sender->removeItem(sender->findText("Custom Effect"));

    // Show/hide the color buttons
    if (capability.getNumColors() == 0) { // hide all
        for (int i = 1; i <= 3; i++)
            findChild<QPushButton *>("colorbutton" + QString::number(i))->hide();
    } else {
        for (int i = 1; i <= 3; i++) {
            if (capability.getNumColors() < i)
                findChild<QPushButton *>("colorbutton" + QString::number(i))->hide();
            else
                findChild<QPushButton *>("colorbutton" + QString::number(i))->show();
        }
    }

    // Show/hide the wave radiobuttons
    if (capability.getIdentifier() != openrazer::RazerEffect::Wave) {
        findChild<QRadioButton *>("radiobutton1")->hide();
        findChild<QRadioButton *>("radiobutton2")->hide();
    } else {
        findChild<QRadioButton *>("radiobutton1")->show();
        findChild<QRadioButton *>("radiobutton2")->show();
    }

    /* Actually go apply the effect in all cases, except for Custom Effect
     * because there we handle this in the CustomEditor class */
    if (!isCustomEffect)
        applyEffectStandardLoc(capability.getIdentifier());
}

openrazer::RGB LedWidget::getColorForButton(int num)
{
    QPalette pal = findChild<QPushButton *>("colorbutton" + QString::number(num))->palette();
    QColor color = pal.color(QPalette::Button);
    return QCOLOR_TO_RGB(color);
}

openrazer::WaveDirection LedWidget::getWaveDirection()
{
    return findChild<QRadioButton *>("radiobutton1")->isChecked() ? openrazer::WaveDirection::RIGHT_TO_LEFT : openrazer::WaveDirection::LEFT_TO_RIGHT;
}

void LedWidget::brightnessSliderChanged(int value)
{
    try {
        mLed->setBrightness(value);
    } catch (const libopenrazer::DBusException &e) {
        qWarning("Failed to change brightness");
        util::showError(tr("Failed to change brightness"));
    }
}

void LedWidget::applyEffectStandardLoc(openrazer::RazerEffect effect)
{
    try {
        switch (effect) {
        case openrazer::RazerEffect::Off: {
            mLed->setOff();
            break;
        }
        case openrazer::RazerEffect::On: {
            mLed->setOn();
            break;
        }
        case openrazer::RazerEffect::Static: {
            openrazer::RGB c = getColorForButton(1);
            mLed->setStatic(c);
            break;
        }
        case openrazer::RazerEffect::Breathing: {
            openrazer::RGB c = getColorForButton(1);
            mLed->setBreathing(c);
            break;
        }
        case openrazer::RazerEffect::BreathingDual: {
            openrazer::RGB c1 = getColorForButton(1);
            openrazer::RGB c2 = getColorForButton(2);
            mLed->setBreathingDual(c1, c2);
            break;
        }
        case openrazer::RazerEffect::BreathingRandom: {
            mLed->setBreathingRandom();
            break;
        }
        case openrazer::RazerEffect::BreathingMono: {
            mLed->setBreathingMono();
            break;
        }
        case openrazer::RazerEffect::Blinking: {
            openrazer::RGB c = getColorForButton(1);
            mLed->setBlinking(c);
            break;
        }
        case openrazer::RazerEffect::Spectrum: {
            mLed->setSpectrum();
            break;
        }
        case openrazer::RazerEffect::Wave: {
            mLed->setWave(getWaveDirection());
            break;
        }
        case openrazer::RazerEffect::Reactive: {
            openrazer::RGB c = getColorForButton(1);
            mLed->setReactive(c, openrazer::ReactiveSpeed::_500MS); // TODO Configure speed?
            break;
        }
        case openrazer::RazerEffect::Ripple: {
            openrazer::RGB c = getColorForButton(1);
            mLed->setRipple(c);
            break;
        }
        case openrazer::RazerEffect::RippleRandom: {
            mLed->setRippleRandom();
            break;
        }
        default:
            throw new std::invalid_argument("Effect not handled: " + QVariant::fromValue(effect).toString().toStdString());
        }
    } catch (const libopenrazer::DBusException &e) {
        qWarning("Failed to change effect");
        util::showError(tr("Failed to change effect"));
    }
}

void LedWidget::applyEffect()
{
    auto *combobox = findChild<QComboBox *>("combobox");

    libopenrazer::RazerCapability capability = combobox->itemData(combobox->currentIndex()).value<libopenrazer::RazerCapability>();

    applyEffectStandardLoc(capability.getIdentifier());
}

void LedWidget::waveRadioButtonChanged(bool enabled)
{
    if (enabled)
        applyEffect();
}

libopenrazer::Led *LedWidget::led()
{
    return mLed;
}
