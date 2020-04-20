/* $Id$ */
/** @file
 * VBox Qt GUI - UIChooserAbstractModel class implementation.
 */

/*
 * Copyright (C) 2012-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/* GUI includes: */
#include "UICommon.h"
#include "UIChooser.h"
#include "UIChooserAbstractModel.h"
#include "UIChooserNode.h"
#include "UIChooserNodeGroup.h"
#include "UIChooserNodeGlobal.h"
#include "UIChooserNodeMachine.h"
#include "UICloudNetworkingStuff.h"
#include "UIExtraDataManager.h"
#include "UIMessageCenter.h"
#include "UIVirtualBoxEventHandler.h"
#include "UIVirtualMachineItemCloud.h"
#ifdef VBOX_GUI_WITH_CLOUD_VMS
# include "UITaskCloudListMachines.h"
# include "UIThreadPool.h"
#endif

/* COM includes: */
#include "CMachine.h"
#ifdef VBOX_GUI_WITH_CLOUD_VMS
# include "CCloudClient.h"
# include "CCloudMachine.h"
# include "CCloudProfile.h"
# include "CCloudProvider.h"
# include "CCloudProviderManager.h"
#endif

/* Type defs: */
typedef QSet<QString> UIStringSet;


/*********************************************************************************************************************************
*   Class UIChooserAbstractModel implementation.                                                                                 *
*********************************************************************************************************************************/

UIChooserAbstractModel:: UIChooserAbstractModel(UIChooser *pParent)
    : QObject(pParent)
    , m_pParent(pParent)
    , m_pInvisibleRootNode(0)
{
    prepare();
}

void UIChooserAbstractModel::init()
{
    /* Create invisible root group node: */
    m_pInvisibleRootNode = new UIChooserNodeGroup(0 /* parent */,
                                                  false /* favorite */,
                                                  0 /* position */,
                                                  QString() /* name */,
                                                  UIChooserNodeGroupType_Local,
                                                  true /* opened */);
    if (invisibleRoot())
    {
        /* Link root to this model: */
        invisibleRoot()->setModel(this);

        /* Create global node: */
        new UIChooserNodeGlobal(invisibleRoot() /* parent */,
                                isGlobalNodeFavorite(invisibleRoot()),
                                0 /* position */,
                                QString() /* tip */);

        /* Acquire VBox: */
        const CVirtualBox comVBox = uiCommon().virtualBox();

        /* Add local machines: */
        LogRelFlow(("UIChooserAbstractModel: Loading local VMs...\n"));
        /* Acquire existing machines: */
        const QVector<CMachine> machines = comVBox.GetMachines();
        /* Show error message if necessary: */
        if (!comVBox.isOk())
            msgCenter().cannotAcquireVirtualBoxParameter(comVBox);
        else
        {
            /* Iterate through existing machines: */
            foreach (const CMachine &comMachine, machines)
            {
                /* Skip if we have nothing to populate (wtf happened?): */
                if (comMachine.isNull())
                    continue;

                /* Get machine ID: */
                const QUuid uMachineID = comMachine.GetId();
                /* Show error message if necessary: */
                if (!comMachine.isOk())
                {
                    msgCenter().cannotAcquireMachineParameter(comMachine);
                    continue;
                }

                /* Skip if we have nothing to show (wtf happened?): */
                if (uMachineID.isNull())
                    continue;

                /* Skip if machine is restricted from being shown: */
                if (!gEDataManager->showMachineInVirtualBoxManagerChooser(uMachineID))
                    continue;

                /* Add machine into tree: */
                addLocalMachineIntoTheTree(comMachine);
            }
        }
        LogRelFlow(("UIChooserAbstractModel: Local VMs loaded.\n"));

#ifdef VBOX_GUI_WITH_CLOUD_VMS
        /* Add cloud providers/profiles: */
        LogRelFlow(("UIChooserAbstractModel: Loading cloud providers/profiles...\n"));
        /* Acquire cloud provider manager: */
        const CCloudProviderManager comCloudProviderManager = comVBox.GetCloudProviderManager();
        /* Show error message if necessary: */
        if (!comVBox.isOk())
            msgCenter().cannotAcquireCloudProviderManager(comVBox);
        else
        {
            /* Acquire existing providers: */
            const QVector<CCloudProvider> providers = comCloudProviderManager.GetProviders();
            /* Show error message if necessary: */
            if (!comCloudProviderManager.isOk())
                msgCenter().cannotAcquireCloudProviderManagerParameter(comCloudProviderManager);
            else
            {
                /* Iterate through existing providers: */
                foreach (CCloudProvider comCloudProvider, providers)
                {
                    /* Skip if we have nothing to populate (file missing?): */
                    if (comCloudProvider.isNull())
                        continue;

                    /* Get profile names: */
                    const QVector<QString> profileNames = comCloudProvider.GetProfileNames();
                    /* Show error message if necessary: */
                    if (!comCloudProvider.isOk())
                    {
                        msgCenter().cannotAcquireCloudProviderParameter(comCloudProvider);
                        continue;
                    }

                    /* Skip if we have nothing to populate (profiles missing?): */
                    if (profileNames.isEmpty())
                        continue;

                    /* Get provider name: */
                    const QString strProviderName = comCloudProvider.GetShortName();
                    /* Show error message if necessary: */
                    if (!comCloudProvider.isOk())
                    {
                        msgCenter().cannotAcquireCloudProviderParameter(comCloudProvider);
                        continue;
                    }

                    /* Add provider group node: */
                    UIChooserNodeGroup *pProviderNode =
                        new UIChooserNodeGroup(invisibleRoot() /* parent */,
                                               false /* favorite */,
                                               getDesiredNodePosition(invisibleRoot(),
                                                                      UIChooserNodeType_Group,
                                                                      strProviderName),
                                               strProviderName,
                                               UIChooserNodeGroupType_Provider,
                                               false /* opened */);

                    /* Iterate through provider's profile names: */
                    foreach (const QString &strProfileName, profileNames)
                    {
                        /* Skip if we have nothing to show (wtf happened?): */
                        if (strProfileName.isEmpty())
                            continue;

                        /* Acquire cloud profile: */
                        CCloudProfile comCloudProfile = comCloudProvider.GetProfileByName(strProfileName);
                        /* Show error message if necessary: */
                        if (!comCloudProvider.isOk())
                        {
                            msgCenter().cannotFindCloudProfile(comCloudProvider, strProfileName);
                            continue;
                        }

                        /* Create cloud client: */
                        CCloudClient comCloudClient = comCloudProfile.CreateCloudClient();
                        /* Show error message if necessary: */
                        if (!comCloudProfile.isOk())
                        {
                            msgCenter().cannotCreateCloudClient(comCloudProfile);
                            continue;
                        }

                        /* Add profile sub-group node: */
                        UIChooserNodeGroup *pProfileNode =
                            new UIChooserNodeGroup(pProviderNode /* parent */,
                                                   false /* favorite */,
                                                   getDesiredNodePosition(pProviderNode,
                                                                          UIChooserNodeType_Group,
                                                                          strProfileName),
                                                   strProfileName,
                                                   UIChooserNodeGroupType_Profile,
                                                   true /* opened */);
                        /* Add fake cloud VM item: */
                        new UIChooserNodeMachine(pProfileNode /* parent */,
                                                 false /* favorite */,
                                                 0 /* position */);

                        /* Create cloud list machines task: */
                        UITaskCloudListMachines *pTask = new UITaskCloudListMachines(comCloudClient,
                                                                                     pProfileNode);
                        if (pTask)
                        {
                            connect(uiCommon().threadPoolCloud(), &UIThreadPool::sigTaskComplete,
                                    this, &UIChooserAbstractModel::sltHandleCloudListMachinesTaskComplete);
                            uiCommon().threadPoolCloud()->enqueueTask(pTask);
                        }
                    }
                }
            }
        }
        LogRelFlow(("UIChooserAbstractModel: Cloud providers/profiles loaded.\n"));
#endif /* VBOX_GUI_WITH_CLOUD_VMS */
    }
}

void UIChooserAbstractModel::deinit()
{
    // WORKAROUND:
    // Currently we are not saving group descriptors
    // (which reflecting group toggle-state) on-the-fly,
    // so for now we are additionally save group orders
    // when exiting application:
    saveGroupOrders();

    /* Make sure all saving steps complete: */
    makeSureGroupDefinitionsSaveIsFinished();
    makeSureGroupOrdersSaveIsFinished();

    /* Delete tree: */
    delete m_pInvisibleRootNode;
    m_pInvisibleRootNode = 0;
}

void UIChooserAbstractModel::wipeOutEmptyGroups()
{
    wipeOutEmptyGroupsStartingFrom(invisibleRoot());
}

/* static */
QString UIChooserAbstractModel::uniqueGroupName(UIChooserNode *pRoot)
{
    /* Enumerate all the group names: */
    QStringList groupNames;
    foreach (UIChooserNode *pNode, pRoot->nodes(UIChooserNodeType_Group))
        groupNames << pNode->name();

    /* Prepare reg-exp: */
    const QString strMinimumName = tr("New group");
    const QString strShortTemplate = strMinimumName;
    const QString strFullTemplate = strShortTemplate + QString(" (\\d+)");
    const QRegExp shortRegExp(strShortTemplate);
    const QRegExp fullRegExp(strFullTemplate);

    /* Search for the maximum index: */
    int iMinimumPossibleNumber = 0;
    foreach (const QString &strName, groupNames)
    {
        if (shortRegExp.exactMatch(strName))
            iMinimumPossibleNumber = qMax(iMinimumPossibleNumber, 2);
        else if (fullRegExp.exactMatch(strName))
            iMinimumPossibleNumber = qMax(iMinimumPossibleNumber, fullRegExp.cap(1).toInt() + 1);
    }

    /* Prepare/return result: */
    QString strResult = strMinimumName;
    if (iMinimumPossibleNumber)
        strResult += " " + QString::number(iMinimumPossibleNumber);
    return strResult;
}

void UIChooserAbstractModel::performSearch(const QString &strSearchTerm, int iItemSearchFlags)
{
    /* Make sure invisible root exists: */
    AssertPtrReturnVoid(invisibleRoot());

    /* Currently we perform the search only for machines, when this to be changed make
     * sure the disabled flags of the other item types are also managed correctly. */

    /* Reset the search first to erase the disabled flag,
     * this also returns a full list of all machine nodes: */
    const QList<UIChooserNode*> nodes = resetSearch();

    /* Stop here if no search conditions specified: */
    if (strSearchTerm.isEmpty())
        return;

    /* Search for all the nodes matching required condition: */
    invisibleRoot()->searchForNodes(strSearchTerm, iItemSearchFlags, m_searchResults);

    /* Assign/reset the disabled flag for required nodes: */
    foreach (UIChooserNode *pNode, nodes)
    {
        if (!pNode)
            continue;
        pNode->setDisabled(!m_searchResults.contains(pNode));
    }
}

QList<UIChooserNode*> UIChooserAbstractModel::resetSearch()
{
    /* Prepare resulting nodes: */
    QList<UIChooserNode*> nodes;

    /* Make sure invisible root exists: */
    AssertPtrReturn(invisibleRoot(), nodes);

    /* Calling UIChooserNode::searchForNodes with an empty search string
     * returns a list all nodes (of the whole tree) of the required type: */
    invisibleRoot()->searchForNodes(QString(), UIChooserItemSearchFlag_Machine, nodes);

    /* Reset the disabled flag of the nodes first: */
    foreach (UIChooserNode *pNode, nodes)
    {
        if (!pNode)
            continue;
        pNode->setDisabled(false);
    }

    /* Reset the search result related data: */
    m_searchResults.clear();

    /* Return  nodes: */
    return nodes;
}

QList<UIChooserNode*> UIChooserAbstractModel::searchResult() const
{
    return m_searchResults;
}

void UIChooserAbstractModel::saveGroupSettings()
{
    emit sigStartGroupSaving();
}

bool UIChooserAbstractModel::isGroupSavingInProgress() const
{
    return    UIThreadGroupDefinitionSave::instance()
           || UIThreadGroupOrderSave::instance();
}

void UIChooserAbstractModel::sltMachineStateChanged(const QUuid &uMachineId, const KMachineState)
{
    /* Update machine-nodes with passed id: */
    invisibleRoot()->updateAllNodes(uMachineId);
}

void UIChooserAbstractModel::sltMachineDataChanged(const QUuid &uMachineId)
{
    /* Update machine-nodes with passed id: */
    invisibleRoot()->updateAllNodes(uMachineId);
}

void UIChooserAbstractModel::sltLocalMachineRegistered(const QUuid &uMachineId, const bool fRegistered)
{
    /* Existing VM unregistered? */
    if (!fRegistered)
    {
        /* Remove machine-items with passed id: */
        invisibleRoot()->removeAllNodes(uMachineId);
        /* Wipe out empty groups: */
        wipeOutEmptyGroups();
    }
    /* New VM registered? */
    else
    {
        /* Should we show this VM? */
        if (gEDataManager->showMachineInVirtualBoxManagerChooser(uMachineId))
        {
            /* Add new machine-item: */
            const CMachine comMachine = uiCommon().virtualBox().FindMachine(uMachineId.toString());
            addLocalMachineIntoTheTree(comMachine, true /* make it visible */);
        }
    }
}

void UIChooserAbstractModel::sltCloudMachineRegistered(const QString &strProviderShortName, const QString &strProfileName,
                                                       const QUuid &uMachineId, const bool fRegistered)
{
    /* Existing VM unregistered? */
    if (!fRegistered)
    {
        /* Remove machine-items with passed id: */
        invisibleRoot()->removeAllNodes(uMachineId);
        /// @todo make sure there is fake item if no real item exist, never wipe out empty groups ..
    }
    /* New VM registered? */
    else
    {
        /* Add new machine-item: */
        const QString strGroupName = QString("/%1/%2").arg(strProviderShortName, strProfileName);
        const CCloudMachine comMachine = cloudMachineById(strProviderShortName, strProfileName, uMachineId);
        addCloudMachineIntoTheTree(strGroupName, comMachine, true /* make it visible */);
        /// @todo make sure there is no fake item if at least one real item exists ..
    }
}

void UIChooserAbstractModel::sltSessionStateChanged(const QUuid &uMachineId, const KSessionState)
{
    /* Update machine-nodes with passed id: */
    invisibleRoot()->updateAllNodes(uMachineId);
}

void UIChooserAbstractModel::sltSnapshotChanged(const QUuid &uMachineId, const QUuid &)
{
    /* Update machine-nodes with passed id: */
    invisibleRoot()->updateAllNodes(uMachineId);
}

void UIChooserAbstractModel::sltReloadMachine(const QUuid &uMachineId)
{
    /* Remove machine-items with passed id: */
    invisibleRoot()->removeAllNodes(uMachineId);
    /* Wipe out empty groups: */
    wipeOutEmptyGroups();

    /* Should we show this VM? */
    if (gEDataManager->showMachineInVirtualBoxManagerChooser(uMachineId))
    {
        /* Add new machine-item: */
        const CMachine comMachine = uiCommon().virtualBox().FindMachine(uMachineId.toString());
        addLocalMachineIntoTheTree(comMachine, true /* make it visible */);
    }
}

void UIChooserAbstractModel::sltStartGroupSaving()
{
    saveGroupDefinitions();
    saveGroupOrders();
}

#ifdef VBOX_GUI_WITH_CLOUD_VMS
void UIChooserAbstractModel::sltHandleCloudListMachinesTaskComplete(UITask *pTask)
{
    /* Skip unrelated tasks: */
    if (!pTask || pTask->type() != UITask::Type_CloudListMachines)
        return;

    /* Cast task to corresponding sub-class: */
    UITaskCloudListMachines *pAcquiringTask = static_cast<UITaskCloudListMachines*>(pTask);

    /* Make sure there were no errors: */
    if (!pAcquiringTask->errorInfo().isNull())
        return msgCenter().cannotAcquireCloudInstanceList(pAcquiringTask->errorInfo());

    /* Acquire parent node and make sure it has children: */
    /// @todo rework task to bring parent by id instead of pointer
    ///       to object which maybe deleted to this moment already
    UIChooserNode *pParentNode = pAcquiringTask->parentNode();
    AssertPtrReturnVoid(pParentNode);
    AssertReturnVoid(pParentNode->hasNodes());

    /* Make sure this node have first child: */
    UIChooserNode *pFirstChildNode = pParentNode->nodes().first();
    AssertPtrReturnVoid(pFirstChildNode);

    /* Which is machine node and has cache of fake-cloud-item type: */
    UIChooserNodeMachine *pFirstChildNodeMachine = pFirstChildNode->toMachineNode();
    AssertPtrReturnVoid(pFirstChildNodeMachine);
    AssertPtrReturnVoid(pFirstChildNodeMachine->cache());
    AssertReturnVoid(pFirstChildNodeMachine->cache()->itemType() == UIVirtualMachineItem::ItemType_CloudFake);

    /* And if we have at least one cloud machine: */
    const QVector<CCloudMachine> machines = pAcquiringTask->result();
    if (!machines.isEmpty())
    {
        /* Remove the "Empty" node: */
        delete pFirstChildNodeMachine;

        /* Add real cloud VM nodes: */
        int iPosition = 0;
        foreach (const CCloudMachine &comCloudMachine, machines)
            new UIChooserNodeMachine(pParentNode,
                                     false /* favorite */,
                                     iPosition++ /* position */,
                                     comCloudMachine);
    }
    else
    {
        /* Otherwise toggle and update "Empty" node: */
        UIVirtualMachineItemCloud *pFakeCloudMachineItem = pFirstChildNodeMachine->cache()->toCloud();
        AssertPtrReturnVoid(pFakeCloudMachineItem);
        pFakeCloudMachineItem->setFakeCloudItemState(UIVirtualMachineItemCloud::FakeCloudItemState_Done);
        pFakeCloudMachineItem->recache();
    }
}
#endif /* VBOX_GUI_WITH_CLOUD_VMS */

void UIChooserAbstractModel::sltHandleCloudMachineStateChange()
{
    UIVirtualMachineItem *pCache = qobject_cast<UIVirtualMachineItem*>(sender());
    AssertPtrReturnVoid(pCache);
    emit sigCloudMachineStateChange(pCache->id());
}

void UIChooserAbstractModel::sltGroupDefinitionsSaveComplete()
{
    makeSureGroupDefinitionsSaveIsFinished();
    emit sigGroupSavingStateChanged();
}

void UIChooserAbstractModel::sltGroupOrdersSaveComplete()
{
    makeSureGroupOrdersSaveIsFinished();
    emit sigGroupSavingStateChanged();
}

void UIChooserAbstractModel::prepare()
{
    prepareConnections();
}

void UIChooserAbstractModel::prepareConnections()
{
    /* Setup parent connections: */
    connect(this, &UIChooserAbstractModel::sigGroupSavingStateChanged,
            m_pParent, &UIChooser::sigGroupSavingStateChanged);

    /* Setup temporary connections,
     * this is to be replaced by corresponding Main API event later. */
    connect(&uiCommon(), &UICommon::sigCloudMachineRegistered,
            this, &UIChooserAbstractModel::sltCloudMachineRegistered);

    /* Setup global connections: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineStateChange,
            this, &UIChooserAbstractModel::sltMachineStateChanged);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineDataChange,
            this, &UIChooserAbstractModel::sltMachineDataChanged);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineRegistered,
            this, &UIChooserAbstractModel::sltLocalMachineRegistered);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSessionStateChange,
            this, &UIChooserAbstractModel::sltSessionStateChanged);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSnapshotTake,
            this, &UIChooserAbstractModel::sltSnapshotChanged);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSnapshotDelete,
            this, &UIChooserAbstractModel::sltSnapshotChanged);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSnapshotChange,
            this, &UIChooserAbstractModel::sltSnapshotChanged);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSnapshotRestore,
            this, &UIChooserAbstractModel::sltSnapshotChanged);

    /* Setup group saving connections: */
    connect(this, &UIChooserAbstractModel::sigStartGroupSaving,
            this, &UIChooserAbstractModel::sltStartGroupSaving,
            Qt::QueuedConnection);
}

void UIChooserAbstractModel::addLocalMachineIntoTheTree(const CMachine &comMachine,
                                                        bool fMakeItVisible /* = false */)
{
    /* Make sure passed VM is not NULL: */
    if (comMachine.isNull())
        LogRelFlow(("UIChooserModel: ERROR: Passed local VM is NULL!\n"));
    AssertReturnVoid(!comMachine.isNull());

    /* Which VM we are loading: */
    const QUuid uId = comMachine.GetId();
    LogRelFlow(("UIChooserModel: Loading local VM with ID={%s}...\n",
                toOldStyleUuid(uId).toUtf8().constData()));
    /* Is that machine accessible? */
    if (comMachine.GetAccessible())
    {
        /* Acquire VM name: */
        const QString strName = comMachine.GetName();
        LogRelFlow(("UIChooserModel:  Local VM {%s} is accessible.\n", strName.toUtf8().constData()));
        /* Which groups passed machine attached to? */
        const QVector<QString> groups = comMachine.GetGroups();
        const QStringList groupList = groups.toList();
        const QString strGroups = groupList.join(", ");
        LogRelFlow(("UIChooserModel:  Local VM {%s} has groups: {%s}.\n",
                    strName.toUtf8().constData(), strGroups.toUtf8().constData()));
        foreach (QString strGroup, groups)
        {
            /* Remove last '/' if any: */
            if (strGroup.right(1) == "/")
                strGroup.truncate(strGroup.size() - 1);
            /* Create machine-item with found group-item as parent: */
            LogRelFlow(("UIChooserModel:   Creating node for local VM {%s} in group {%s}.\n",
                        strName.toUtf8().constData(), strGroup.toUtf8().constData()));
            createLocalMachineNode(getLocalGroupNode(strGroup, invisibleRoot(), fMakeItVisible), comMachine);
        }
        /* Update group definitions: */
        m_groups[toOldStyleUuid(uId)] = groupList;
    }
    /* Inaccessible machine: */
    else
    {
        /* VM is accessible: */
        LogRelFlow(("UIChooserModel:  Local VM {%s} is inaccessible.\n",
                    toOldStyleUuid(uId).toUtf8().constData()));
        /* Create machine-item with main-root group-item as parent: */
        createLocalMachineNode(invisibleRoot(), comMachine);
    }
}

void UIChooserAbstractModel::addCloudMachineIntoTheTree(const QString &strGroup,
                                                        const CCloudMachine &comMachine,
                                                        bool fMakeItVisible /* = false */)
{
    /* Make sure passed VM is not NULL: */
    if (comMachine.isNull())
        LogRelFlow(("UIChooserModel: ERROR: Passed cloud VM is NULL!\n"));
    AssertReturnVoid(!comMachine.isNull());

    /* Which VM we are loading: */
    const QUuid uId = comMachine.GetId();
    LogRelFlow(("UIChooserModel: Loading cloud VM with ID={%s}...\n",
                toOldStyleUuid(uId).toUtf8().constData()));
    /* Acquire VM name: */
    QString strName = comMachine.GetName();
    if (strName.isEmpty())
        strName = uId.toString();
    LogRelFlow(("UIChooserModel:  Creating node for cloud VM {%s} in group {%s}.\n",
                strName.toUtf8().constData(), strGroup.toUtf8().constData()));
    /* Create machine-item with found group-item as parent: */
    createCloudMachineNode(getCloudGroupNode(strGroup, invisibleRoot(), fMakeItVisible), comMachine);
    /* Update group definitions: */
    const QStringList groupList(strGroup);
    m_groups[toOldStyleUuid(uId)] = groupList;
}

UIChooserNode *UIChooserAbstractModel::getLocalGroupNode(const QString &strName, UIChooserNode *pParentNode, bool fAllGroupsOpened)
{
    /* Check passed stuff: */
    if (pParentNode->name() == strName)
        return pParentNode;

    /* Prepare variables: */
    const QString strFirstSubName = strName.section('/', 0, 0);
    const QString strFirstSuffix = strName.section('/', 1, -1);
    const QString strSecondSubName = strFirstSuffix.section('/', 0, 0);
    const QString strSecondSuffix = strFirstSuffix.section('/', 1, -1);

    /* Passed group name equal to first sub-name: */
    if (pParentNode->name() == strFirstSubName)
    {
        /* Make sure first-suffix is NOT empty: */
        AssertMsg(!strFirstSuffix.isEmpty(), ("Invalid group name!"));
        /* Trying to get group node among our children: */
        foreach (UIChooserNode *pGroupNode, pParentNode->nodes(UIChooserNodeType_Group))
        {
            if (   pGroupNode->toGroupNode()->groupType() == UIChooserNodeGroupType_Local
                && pGroupNode->name() == strSecondSubName)
            {
                UIChooserNode *pFoundNode = getLocalGroupNode(strFirstSuffix, pGroupNode, fAllGroupsOpened);
                if (UIChooserNodeGroup *pFoundGroupNode = pFoundNode->toGroupNode())
                    if (fAllGroupsOpened && pFoundGroupNode->isClosed())
                        pFoundGroupNode->open();
                return pFoundNode;
            }
        }
    }

    /* Found nothing? Creating: */
    UIChooserNodeGroup *pNewGroupNode =
        new UIChooserNodeGroup(pParentNode,
                               false /* favorite */,
                               getDesiredNodePosition(pParentNode, UIChooserNodeType_Group, strSecondSubName),
                               strSecondSubName,
                               UIChooserNodeGroupType_Local,
                               fAllGroupsOpened || shouldGroupNodeBeOpened(pParentNode, strSecondSubName));
    return strSecondSuffix.isEmpty() ? pNewGroupNode : getLocalGroupNode(strFirstSuffix, pNewGroupNode, fAllGroupsOpened);
}

UIChooserNode *UIChooserAbstractModel::getCloudGroupNode(const QString &strName, UIChooserNode *pParentNode, bool fAllGroupsOpened)
{
    /* Check passed stuff: */
    if (pParentNode->name() == strName)
        return pParentNode;

    /* Prepare variables: */
    const QString strFirstSubName = strName.section('/', 0, 0);
    const QString strFirstSuffix = strName.section('/', 1, -1);
    const QString strSecondSubName = strFirstSuffix.section('/', 0, 0);

    /* Passed group name equal to first sub-name: */
    if (pParentNode->name() == strFirstSubName)
    {
        /* Make sure first-suffix is NOT empty: */
        AssertMsg(!strFirstSuffix.isEmpty(), ("Invalid group name!"));
        /* Trying to get group node among our children: */
        foreach (UIChooserNode *pGroupNode, pParentNode->nodes(UIChooserNodeType_Group))
        {
            if (   pGroupNode->toGroupNode()->groupType() != UIChooserNodeGroupType_Local
                && pGroupNode->name() == strSecondSubName)
            {
                UIChooserNode *pFoundNode = getCloudGroupNode(strFirstSuffix, pGroupNode, fAllGroupsOpened);
                if (UIChooserNodeGroup *pFoundGroupNode = pFoundNode->toGroupNode())
                    if (fAllGroupsOpened && pFoundGroupNode->isClosed())
                        pFoundGroupNode->open();
                return pFoundNode;
            }
        }
    }

    /* Found nothing? Returning parent: */
    AssertFailedReturn(pParentNode);
}

bool UIChooserAbstractModel::shouldGroupNodeBeOpened(UIChooserNode *pParentNode, const QString &strName)
{
    /* Read group definitions: */
    const QStringList definitions = gEDataManager->selectorWindowGroupsDefinitions(pParentNode->fullName());
    /* Return 'false' if no definitions found: */
    if (definitions.isEmpty())
        return false;

    /* Prepare required group definition reg-exp: */
    const QString strDefinitionTemplate = QString("g(\\S)*=%1").arg(strName);
    const QRegExp definitionRegExp(strDefinitionTemplate);
    /* For each the group definition: */
    foreach (const QString &strDefinition, definitions)
    {
        /* Check if this is required definition: */
        if (definitionRegExp.indexIn(strDefinition) == 0)
        {
            /* Get group descriptor: */
            const QString strDescriptor(definitionRegExp.cap(1));
            if (strDescriptor.contains('o'))
                return true;
        }
    }

    /* Return 'false' by default: */
    return false;
}

void UIChooserAbstractModel::wipeOutEmptyGroupsStartingFrom(UIChooserNode *pParent)
{
    /* Cleanup all the group-items recursively first: */
    foreach (UIChooserNode *pNode, pParent->nodes(UIChooserNodeType_Group))
        wipeOutEmptyGroupsStartingFrom(pNode);
    /* If parent has no nodes: */
    if (!pParent->hasNodes())
    {
        /* If that is non-root item: */
        if (!pParent->isRoot())
        {
            /* Delete parent node and item: */
            delete pParent;
        }
    }
}

bool UIChooserAbstractModel::isGlobalNodeFavorite(UIChooserNode *pParentNode) const
{
    /* Read group definitions: */
    const QStringList definitions = gEDataManager->selectorWindowGroupsDefinitions(pParentNode->fullName());
    /* Return 'false' if no definitions found: */
    if (definitions.isEmpty())
        return false;

    /* Prepare required group definition reg-exp: */
    const QString strDefinitionTemplate = QString("n(\\S)*=GLOBAL");
    const QRegExp definitionRegExp = QRegExp(strDefinitionTemplate);
    /* For each the group definition: */
    foreach (const QString &strDefinition, definitions)
    {
        /* Check if this is required definition: */
        if (definitionRegExp.indexIn(strDefinition) == 0)
        {
            /* Get group descriptor: */
            const QString strDescriptor(definitionRegExp.cap(1));
            if (strDescriptor.contains('f'))
                return true;
        }
    }

    /* Return 'false' by default: */
    return false;
}

int UIChooserAbstractModel::getDesiredNodePosition(UIChooserNode *pParentNode, UIChooserNodeType enmType, const QString &strName)
{
    /* End of list (by default)? */
    int iNewNodeDesiredPosition = -1;
    /* Which position should be new node placed by definitions: */
    int iNewNodeDefinitionPosition = getDefinedNodePosition(pParentNode, enmType, strName);

    /* If some position wanted: */
    if (iNewNodeDefinitionPosition != -1)
    {
        /* Start of list if some definition present: */
        iNewNodeDesiredPosition = 0;
        /* We have to check all the existing node positions: */
        QList<UIChooserNode*> nodes = pParentNode->nodes(enmType);
        for (int i = nodes.size() - 1; i >= 0; --i)
        {
            /* Get current node: */
            UIChooserNode *pNode = nodes[i];
            /* Which position should be current node placed by definitions? */
            QString strDefinitionName = pNode->type() == UIChooserNodeType_Group ? pNode->name() :
                                        pNode->type() == UIChooserNodeType_Machine ? toOldStyleUuid(pNode->toMachineNode()->cache()->id()) :
                                        QString();
            AssertMsg(!strDefinitionName.isEmpty(), ("Wrong definition name!"));
            int iNodeDefinitionPosition = getDefinedNodePosition(pParentNode, enmType, strDefinitionName);
            /* If some position wanted: */
            if (iNodeDefinitionPosition != -1)
            {
                AssertMsg(iNodeDefinitionPosition != iNewNodeDefinitionPosition, ("Incorrect definitions!"));
                if (iNodeDefinitionPosition < iNewNodeDefinitionPosition)
                {
                    iNewNodeDesiredPosition = i + 1;
                    break;
                }
            }
        }
    }

    /* Return desired node position: */
    return iNewNodeDesiredPosition;
}

int UIChooserAbstractModel::getDefinedNodePosition(UIChooserNode *pParentNode, UIChooserNodeType enmType, const QString &strName)
{
    /* Read group definitions: */
    const QStringList definitions = gEDataManager->selectorWindowGroupsDefinitions(pParentNode->fullName());
    /* Return 'false' if no definitions found: */
    if (definitions.isEmpty())
        return -1;

    /* Prepare definition reg-exp: */
    QString strDefinitionTemplateShort;
    QString strDefinitionTemplateFull;
    switch (enmType)
    {
        case UIChooserNodeType_Group:
            strDefinitionTemplateShort = QString("^g(\\S)*=");
            strDefinitionTemplateFull = QString("^g(\\S)*=%1$").arg(strName);
            break;
        case UIChooserNodeType_Machine:
            strDefinitionTemplateShort = QString("^m=");
            strDefinitionTemplateFull = QString("^m=%1$").arg(strName);
            break;
        default: return -1;
    }
    QRegExp definitionRegExpShort(strDefinitionTemplateShort);
    QRegExp definitionRegExpFull(strDefinitionTemplateFull);

    /* For each the definition: */
    int iDefinitionIndex = -1;
    foreach (const QString &strDefinition, definitions)
    {
        /* Check if this definition is of required type: */
        if (definitionRegExpShort.indexIn(strDefinition) == 0)
        {
            ++iDefinitionIndex;
            /* Check if this definition is exactly what we need: */
            if (definitionRegExpFull.indexIn(strDefinition) == 0)
                return iDefinitionIndex;
        }
    }

    /* Return result: */
    return -1;
}

void UIChooserAbstractModel::createLocalMachineNode(UIChooserNode *pParentNode, const CMachine &comMachine)
{
    new UIChooserNodeMachine(pParentNode,
                             false /* favorite */,
                             getDesiredNodePosition(pParentNode, UIChooserNodeType_Machine, toOldStyleUuid(comMachine.GetId())),
                             comMachine);
}

void UIChooserAbstractModel::createCloudMachineNode(UIChooserNode *pParentNode, const CCloudMachine &comMachine)
{
    UIChooserNodeMachine *pNode = new UIChooserNodeMachine(pParentNode,
                                                           false /* favorite */,
                                                           getDesiredNodePosition(pParentNode,
                                                                                  UIChooserNodeType_Machine,
                                                                                  toOldStyleUuid(comMachine.GetId())),
                                                           comMachine);
    /* Request for async node update if necessary: */
    if (!comMachine.GetAccessible())
        pNode->cache()->toCloud()->updateInfoAsync(false /* delayed? */);
}

void UIChooserAbstractModel::saveGroupDefinitions()
{
    /* Make sure there is no group save activity: */
    if (UIThreadGroupDefinitionSave::instance())
        return;

    /* Prepare full group map: */
    QMap<QString, QStringList> groups;
    gatherGroupDefinitions(groups, invisibleRoot());

    /* Save information in other thread: */
    UIThreadGroupDefinitionSave::prepare();
    emit sigGroupSavingStateChanged();
    connect(UIThreadGroupDefinitionSave::instance(), &UIThreadGroupDefinitionSave::sigReload,
            this, &UIChooserAbstractModel::sltReloadMachine);
    UIThreadGroupDefinitionSave::instance()->configure(this, m_groups, groups);
    UIThreadGroupDefinitionSave::instance()->start();
    m_groups = groups;
}

void UIChooserAbstractModel::saveGroupOrders()
{
    /* Make sure there is no group save activity: */
    if (UIThreadGroupOrderSave::instance())
        return;

    /* Prepare full group map: */
    QMap<QString, QStringList> groups;
    gatherGroupOrders(groups, invisibleRoot());

    /* Save information in other thread: */
    UIThreadGroupOrderSave::prepare();
    emit sigGroupSavingStateChanged();
    UIThreadGroupOrderSave::instance()->configure(this, groups);
    UIThreadGroupOrderSave::instance()->start();
}

void UIChooserAbstractModel::gatherGroupDefinitions(QMap<QString, QStringList> &definitions,
                                                    UIChooserNode *pParentGroup)
{
    /* Iterate over all the machine-nodes: */
    foreach (UIChooserNode *pNode, pParentGroup->nodes(UIChooserNodeType_Machine))
        if (UIChooserNodeMachine *pMachineNode = pNode->toMachineNode())
            if (   pMachineNode->cache()->itemType() == UIVirtualMachineItem::ItemType_Local
                && pMachineNode->cache()->accessible())
                definitions[toOldStyleUuid(pMachineNode->cache()->id())] << pParentGroup->fullName();
    /* Iterate over all the group-nodes: */
    foreach (UIChooserNode *pNode, pParentGroup->nodes(UIChooserNodeType_Group))
        gatherGroupDefinitions(definitions, pNode);
}

void UIChooserAbstractModel::gatherGroupOrders(QMap<QString, QStringList> &orders,
                                               UIChooserNode *pParentGroup)
{
    /* Prepare extra-data key for current group: */
    const QString strExtraDataKey = pParentGroup->fullName();
    /* Iterate over all the global-nodes: */
    foreach (UIChooserNode *pNode, pParentGroup->nodes(UIChooserNodeType_Global))
    {
        const QString strGlobalDescriptor(pNode->isFavorite() ? "nf" : "n");
        orders[strExtraDataKey] << QString("%1=GLOBAL").arg(strGlobalDescriptor);
    }
    /* Iterate over all the group-nodes: */
    foreach (UIChooserNode *pNode, pParentGroup->nodes(UIChooserNodeType_Group))
    {
        const QString strGroupDescriptor(pNode->toGroupNode()->isOpened() ? "go" : "gc");
        orders[strExtraDataKey] << QString("%1=%2").arg(strGroupDescriptor, pNode->name());
        gatherGroupOrders(orders, pNode);
    }
    /* Iterate over all the machine-nodes: */
    foreach (UIChooserNode *pNode, pParentGroup->nodes(UIChooserNodeType_Machine))
        if (UIChooserNodeMachine *pMachineNode = pNode->toMachineNode())
            if (pMachineNode->cache()->itemType() == UIVirtualMachineItem::ItemType_Local)
                orders[strExtraDataKey] << QString("m=%1").arg(toOldStyleUuid(pMachineNode->cache()->id()));
}

void UIChooserAbstractModel::makeSureGroupDefinitionsSaveIsFinished()
{
    /* Cleanup if necessary: */
    if (UIThreadGroupDefinitionSave::instance())
        UIThreadGroupDefinitionSave::cleanup();
}

void UIChooserAbstractModel::makeSureGroupOrdersSaveIsFinished()
{
    /* Cleanup if necessary: */
    if (UIThreadGroupOrderSave::instance())
        UIThreadGroupOrderSave::cleanup();
}

/* static */
QString UIChooserAbstractModel::toOldStyleUuid(const QUuid &uId)
{
    return uId.toString().remove(QRegExp("[{}]"));
}


/*********************************************************************************************************************************
*   Class UIThreadGroupDefinitionSave implementation.                                                                            *
*********************************************************************************************************************************/

/* static */
UIThreadGroupDefinitionSave *UIThreadGroupDefinitionSave::s_pInstance = 0;

/* static */
UIThreadGroupDefinitionSave *UIThreadGroupDefinitionSave::instance()
{
    return s_pInstance;
}

/* static */
void UIThreadGroupDefinitionSave::prepare()
{
    /* Make sure instance not prepared: */
    if (s_pInstance)
        return;

    /* Crate instance: */
    new UIThreadGroupDefinitionSave;
}

/* static */
void UIThreadGroupDefinitionSave::cleanup()
{
    /* Make sure instance prepared: */
    if (!s_pInstance)
        return;

    /* Crate instance: */
    delete s_pInstance;
}

void UIThreadGroupDefinitionSave::configure(QObject *pParent,
                                            const QMap<QString, QStringList> &oldLists,
                                            const QMap<QString, QStringList> &newLists)
{
    m_oldLists = oldLists;
    m_newLists = newLists;
    UIChooserAbstractModel* pChooserAbstractModel = qobject_cast<UIChooserAbstractModel*>(pParent);
    AssertPtrReturnVoid(pChooserAbstractModel);
    {
        connect(this, &UIThreadGroupDefinitionSave::sigComplete,
                pChooserAbstractModel, &UIChooserAbstractModel::sltGroupDefinitionsSaveComplete);
    }
}

UIThreadGroupDefinitionSave::UIThreadGroupDefinitionSave()
{
    /* Assign instance: */
    s_pInstance = this;
}

UIThreadGroupDefinitionSave::~UIThreadGroupDefinitionSave()
{
    /* Wait: */
    wait();

    /* Erase instance: */
    s_pInstance = 0;
}

void UIThreadGroupDefinitionSave::run()
{
    /* COM prepare: */
    COMBase::InitializeCOM(false);

    /* For every particular machine ID: */
    foreach (const QString &strId, m_newLists.keys())
    {
        /* Get new group list/set: */
        const QStringList &newGroupList = m_newLists.value(strId);
        const UIStringSet &newGroupSet = UIStringSet::fromList(newGroupList);
        /* Get old group list/set: */
        const QStringList &oldGroupList = m_oldLists.value(strId);
        const UIStringSet &oldGroupSet = UIStringSet::fromList(oldGroupList);
        /* Make sure group set changed: */
        if (newGroupSet == oldGroupSet)
            continue;

        /* The next steps are subsequent.
         * Every of them is mandatory in order to continue
         * with common cleanup in case of failure.
         * We have to simulate a try-catch block. */
        CSession session;
        CMachine machine;
        do
        {
            /* 1. Open session: */
            session = uiCommon().openSession(QUuid(strId));
            if (session.isNull())
                break;

            /* 2. Get session machine: */
            machine = session.GetMachine();
            if (machine.isNull())
                break;

            /* 3. Set new groups: */
            machine.SetGroups(newGroupList.toVector());
            if (!machine.isOk())
            {
                msgCenter().cannotSetGroups(machine);
                break;
            }

            /* 4. Save settings: */
            machine.SaveSettings();
            if (!machine.isOk())
            {
                msgCenter().cannotSaveMachineSettings(machine);
                break;
            }
        } while (0);

        /* Cleanup if necessary: */
        if (machine.isNull() || !machine.isOk())
            emit sigReload(QUuid(strId));
        if (!session.isNull())
            session.UnlockMachine();
    }

    /* Notify listeners about completeness: */
    emit sigComplete();

    /* COM cleanup: */
    COMBase::CleanupCOM();
}


/*********************************************************************************************************************************
*   Class UIThreadGroupOrderSave implementation.                                                                                 *
*********************************************************************************************************************************/

/* static */
UIThreadGroupOrderSave *UIThreadGroupOrderSave::s_pInstance = 0;

/* static */
UIThreadGroupOrderSave *UIThreadGroupOrderSave::instance()
{
    return s_pInstance;
}

/* static */
void UIThreadGroupOrderSave::prepare()
{
    /* Make sure instance not prepared: */
    if (s_pInstance)
        return;

    /* Crate instance: */
    new UIThreadGroupOrderSave;
}

/* static */
void UIThreadGroupOrderSave::cleanup()
{
    /* Make sure instance prepared: */
    if (!s_pInstance)
        return;

    /* Crate instance: */
    delete s_pInstance;
}

void UIThreadGroupOrderSave::configure(QObject *pParent,
                                       const QMap<QString, QStringList> &groups)
{
    m_groups = groups;
    UIChooserAbstractModel *pChooserAbstractModel = qobject_cast<UIChooserAbstractModel*>(pParent);
    AssertPtrReturnVoid(pChooserAbstractModel);
    {
        connect(this, &UIThreadGroupOrderSave::sigComplete,
                pChooserAbstractModel, &UIChooserAbstractModel::sltGroupOrdersSaveComplete);
    }
}

UIThreadGroupOrderSave::UIThreadGroupOrderSave()
{
    /* Assign instance: */
    s_pInstance = this;
}

UIThreadGroupOrderSave::~UIThreadGroupOrderSave()
{
    /* Wait: */
    wait();

    /* Erase instance: */
    s_pInstance = 0;
}

void UIThreadGroupOrderSave::run()
{
    /* COM prepare: */
    COMBase::InitializeCOM(false);

    /* Clear all the extra-data records related to group definitions: */
    gEDataManager->clearSelectorWindowGroupsDefinitions();
    /* For every particular group definition: */
    foreach (const QString &strId, m_groups.keys())
        gEDataManager->setSelectorWindowGroupsDefinitions(strId, m_groups[strId]);

    /* Notify listeners about completeness: */
    emit sigComplete();

    /* COM cleanup: */
    COMBase::CleanupCOM();
}
