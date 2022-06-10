/* $Id$ */
/** @file
 * VBox Qt GUI - UIUSBControllerEditor class declaration.
 */

/*
 * Copyright (C) 2019-2022 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIUSBControllerEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIUSBControllerEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* GUI includes: */
#include "QIWithRetranslateUI.h"
#include "UILibraryDefs.h"

/* COM includes: */
#include "COMEnums.h"

/* Forward declarations: */
class QRadioButton;

/** QWidget subclass used as a USB controller editor. */
class SHARED_LIBRARY_STUFF UIUSBControllerEditor : public QIWithRetranslateUI<QWidget>
{
    Q_OBJECT;

signals:

    /** Notifies listeners about value change. */
    void sigValueChanged();

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIUSBControllerEditor(QWidget *pParent = 0);

    /** Defines editor @a enmValue. */
    void setValue(KUSBControllerType enmValue);
    /** Returns editor value. */
    KUSBControllerType value() const;

    /** Returns the vector of supported values. */
    QVector<KUSBControllerType> supportedValues() const { return m_supportedValues; }

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE;

private:

    /** Prepares all. */
    void prepare();

    /** Updates button set. */
    void updateButtonSet();

    /** Holds the value to be selected. */
    KUSBControllerType  m_enmValue;

    /** Holds the vector of supported values. */
    QVector<KUSBControllerType>  m_supportedValues;

    /** Holds the USB1 radio-button instance. */
    QRadioButton     *m_pRadioButtonUSB1;
    /** Holds the USB2 radio-button instance. */
    QRadioButton     *m_pRadioButtonUSB2;
    /** Holds the USB3 radio-button instance. */
    QRadioButton     *m_pRadioButtonUSB3;
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIUSBControllerEditor_h */
