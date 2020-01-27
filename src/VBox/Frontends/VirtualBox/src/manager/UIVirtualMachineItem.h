/* $Id$ */
/** @file
 * VBox Qt GUI - UIVirtualMachineItem class declarations.
 */

/*
 * Copyright (C) 2006-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef FEQT_INCLUDED_SRC_manager_UIVirtualMachineItem_h
#define FEQT_INCLUDED_SRC_manager_UIVirtualMachineItem_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QDateTime>
#include <QMimeData>
#include <QPixmap>

/* GUI includes: */
#include "UISettingsDefs.h"

/* COM includes: */
#include "COMEnums.h"
#include "CMachine.h"
#include "CVirtualBoxErrorInfo.h"

/* Using declarations: */
using namespace UISettingsDefs;

/** Virtual Machine item interface.
  * A wrapper caching data about local VM. */
class UIVirtualMachineItem
{
public:

    /** Constructs local VM item on the basis of taken @a comMachine. */
    UIVirtualMachineItem(const CMachine &comMachine);
    /** Destructs local VM item. */
    virtual ~UIVirtualMachineItem();

    /** @name Arguments.
      * @{ */
        /** Returns cached virtual machine object. */
        CMachine machine() const { return m_comMachine; }
    /** @} */

    /** @name Basic attributes.
      * @{ */
        /** Returns cached machine id. */
        QUuid id() const { return m_uId; }
        /** Returns cached machine settings file name. */
        QString settingsFile() const { return m_strSettingsFile; }
    /** @} */

    /** @name VM access attributes.
      * @{ */
        /** Returns whether VM was accessible. */
        bool accessible() const { return m_fAccessible; }
        /** Returns the last cached access error. */
        const CVirtualBoxErrorInfo &accessError() const { return m_comAccessError; }
    /** @} */

    /** @name Advanced attributes.
      * @{ */
        /** Returns cached machine name. */
        QString name() const { return m_strName; }
        /** Returns cached machine OS type id. */
        QString osTypeId() const { return m_strOSTypeId; }
        /** Returns cached machine OS type pixmap.
          * @param  pLogicalSize  Argument to assign actual pixmap size to. */
        QPixmap osPixmap(QSize *pLogicalSize = 0) const;
        /** Returns cached machine group list. */
        const QStringList &groups() { return m_groups; }
    /** @} */

    /** @name Snapshot attributes.
      * @{ */
        /** Returns cached snapshot name. */
        QString snapshotName() const { return m_strSnapshotName; }
        /** Returns cached snapshot children count. */
        ULONG snapshotCount() const { return m_cSnaphot; }
    /** @} */

    /** @name State attributes.
      * @{ */
        /** Returns cached machine state. */
        KMachineState machineState() const { return m_enmMachineState; }
        /** Returns cached session state. */
        KSessionState sessionState() const { return m_enmSessionState; }
        /** Returns cached machine state name. */
        QString machineStateName() const;
        /** Returns cached session state name. */
        QString sessionStateName() const;
        /** Returns cached machine state icon. */
        QIcon machineStateIcon() const;
        /** Returns cached configuration access level. */
        ConfigurationAccessLevel configurationAccessLevel() const { return m_enmConfigurationAccessLevel; }
    /** @} */

    /** @name Visual attributes.
      * @{ */
        /** Returns cached machine tool-tip. */
        QString toolTipText() const;
    /** @} */

    /** @name Console attributes.
      * @{ */
        /** Returns whether we can switch to main window of VM process. */
        bool canSwitchTo() const;
        /** Tries to switch to the main window of the VM process.
          * @return true if switched successfully. */
        bool switchTo();
    /** @} */

    /** @name Extra-data options.
      * @{ */
        /** Returns whether we should show machine details. */
        bool hasDetails() const { return m_fHasDetails; }
    /** @} */

    /** @name Update stuff.
      * @{ */
        /** Recaches machine data. */
        void recache();
        /** Recaches machine item pixmap. */
        void recachePixmap();
    /** @} */

    /** @name Validation stuff.
      * @{ */
        /** Returns whether passed machine @a pItem is editable. */
        static bool isItemEditable(UIVirtualMachineItem *pItem);
        /** Returns whether passed machine @a pItem is saved. */
        static bool isItemSaved(UIVirtualMachineItem *pItem);
        /** Returns whether passed machine @a pItem is powered off. */
        static bool isItemPoweredOff(UIVirtualMachineItem *pItem);
        /** Returns whether passed machine @a pItem is started. */
        static bool isItemStarted(UIVirtualMachineItem *pItem);
        /** Returns whether passed machine @a pItem is running. */
        static bool isItemRunning(UIVirtualMachineItem *pItem);
        /** Returns whether passed machine @a pItem is running headless. */
        static bool isItemRunningHeadless(UIVirtualMachineItem *pItem);
        /** Returns whether passed machine @a pItem is paused. */
        static bool isItemPaused(UIVirtualMachineItem *pItem);
        /** Returns whether passed machine @a pItem is stuck. */
        static bool isItemStuck(UIVirtualMachineItem *pItem);
    /** @} */

private:

    /** @name Arguments.
      * @{ */
        /** Holds cached machine object reference. */
        CMachine  m_comMachine;
    /** @} */

    /** @name Basic attributes.
      * @{ */
        /** Holds cached machine id. */
        QUuid    m_uId;
        /** Holds cached machine settings file name. */
        QString  m_strSettingsFile;
    /** @} */

    /** @name VM access attributes.
      * @{ */
        /** Holds whether VM was accessible. */
        bool                  m_fAccessible;
        /** Holds the last cached access error. */
        CVirtualBoxErrorInfo  m_comAccessError;
    /** @} */

    /** @name Advanced attributes.
      * @{ */
        /** Holds cached machine name. */
        QString      m_strName;
        /** Holds cached machine OS type id. */
        QString      m_strOSTypeId;
        /** Holds cached machine OS type pixmap. */
        QPixmap      m_pixmap;
        /** Holds cached machine OS type pixmap size. */
        QSize        m_logicalPixmapSize;
        /** Holds cached machine group list. */
        QStringList  m_groups;
    /** @} */

    /** @name Snapshot attributes.
      * @{ */
        /** Holds cached snapshot name. */
        QString    m_strSnapshotName;
        /** Holds cached last state change date/time. */
        QDateTime  m_lastStateChange;
        /** Holds cached snapshot children count. */
        ULONG      m_cSnaphot;
    /** @} */

    /** @name State attributes.
      * @{ */
        /** Holds cached machine state. */
        KMachineState             m_enmMachineState;
        /** Holds cached session state. */
        KSessionState             m_enmSessionState;
        /** Holds configuration access level. */
        ConfigurationAccessLevel  m_enmConfigurationAccessLevel;
    /** @} */

    /** @name Console attributes.
      * @{ */
        /** Holds machine PID. */
        ULONG  m_pid;
    /** @} */

    /** @name Extra-data options.
      * @{ */
        /** Holds whether we should show machine details. */
        bool  m_fHasDetails;
    /** @} */
};

/* Make the pointer of this class public to the QVariant framework */
Q_DECLARE_METATYPE(UIVirtualMachineItem *);

/** QMimeData subclass for handling UIVirtualMachineItem mime data. */
class UIVirtualMachineItemMimeData : public QMimeData
{
    Q_OBJECT;

public:

    /** Constructs mime data for passed VM @a pItem. */
    UIVirtualMachineItemMimeData(UIVirtualMachineItem *pItem);

    /** Returns cached VM item. */
    UIVirtualMachineItem *item() const { return m_pItem; }

    /** Returns supported format list. */
    virtual QStringList formats() const /* override */;

    /** Returns UIVirtualMachineItem mime data type. */
    static QString type() { return m_type; }

private:

    /** Holds cached VM item. */
    UIVirtualMachineItem *m_pItem;

    /** Holds UIVirtualMachineItem mime data type. */
    static QString  m_type;
};

#endif /* !FEQT_INCLUDED_SRC_manager_UIVirtualMachineItem_h */
