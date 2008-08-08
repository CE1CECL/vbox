/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla JavaScript code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1999-2001
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Brendan Eich <brendan@mozilla.org> (Original Author)
 *   Chris Waterson <waterson@netscape.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

/*
 * Double hashing implementation.
 * GENERATED BY js/src/plify_jsdhash.sed -- DO NOT EDIT!!!
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "prbit.h"
#include "pldhash.h"
#include "prlog.h"     /* for PR_ASSERT */

#ifdef PL_DHASHMETER
# if defined MOZILLA_CLIENT && defined DEBUG_XXXbrendan
#  include "nsTraceMalloc.h"
# endif
# define METER(x)       x
#else
# define METER(x)       /* nothing */
#endif

PR_IMPLEMENT(void *)
PL_DHashAllocTable(PLDHashTable *table, PRUint32 nbytes)
{
    return malloc(nbytes);
}

PR_IMPLEMENT(void)
PL_DHashFreeTable(PLDHashTable *table, void *ptr)
{
    free(ptr);
}

PR_IMPLEMENT(PLDHashNumber)
PL_DHashStringKey(PLDHashTable *table, const void *key)
{
    PLDHashNumber h;
    const unsigned char *s;

    h = 0;
    for (s = key; *s != '\0'; s++)
        h = (h >> (PL_DHASH_BITS - 4)) ^ (h << 4) ^ *s;
    return h;
}

PR_IMPLEMENT(const void *)
PL_DHashGetKeyStub(PLDHashTable *table, PLDHashEntryHdr *entry)
{
    PLDHashEntryStub *stub = (PLDHashEntryStub *)entry;

    return stub->key;
}

PR_IMPLEMENT(PLDHashNumber)
PL_DHashVoidPtrKeyStub(PLDHashTable *table, const void *key)
{
    return (PLDHashNumber)key >> 2;
}

PR_IMPLEMENT(PRBool)
PL_DHashMatchEntryStub(PLDHashTable *table,
                       const PLDHashEntryHdr *entry,
                       const void *key)
{
    const PLDHashEntryStub *stub = (const PLDHashEntryStub *)entry;

    return stub->key == key;
}

PR_IMPLEMENT(PRBool)
PL_DHashMatchStringKey(PLDHashTable *table,
                       const PLDHashEntryHdr *entry,
                       const void *key)
{
    const PLDHashEntryStub *stub = (const PLDHashEntryStub *)entry;

    /* XXX tolerate null keys on account of sloppy Mozilla callers. */
    return stub->key == key ||
           (stub->key && key && strcmp(stub->key, key) == 0);
}

PR_IMPLEMENT(void)
PL_DHashMoveEntryStub(PLDHashTable *table,
                      const PLDHashEntryHdr *from,
                      PLDHashEntryHdr *to)
{
    memcpy(to, from, table->entrySize);
}

PR_IMPLEMENT(void)
PL_DHashClearEntryStub(PLDHashTable *table, PLDHashEntryHdr *entry)
{
    memset(entry, 0, table->entrySize);
}

PR_IMPLEMENT(void)
PL_DHashFreeStringKey(PLDHashTable *table, PLDHashEntryHdr *entry)
{
    const PLDHashEntryStub *stub = (const PLDHashEntryStub *)entry;

    free((void *) stub->key);
    memset(entry, 0, table->entrySize);
}

PR_IMPLEMENT(void)
PL_DHashFinalizeStub(PLDHashTable *table)
{
}

static const PLDHashTableOps stub_ops = {
    PL_DHashAllocTable,
    PL_DHashFreeTable,
    PL_DHashGetKeyStub,
    PL_DHashVoidPtrKeyStub,
    PL_DHashMatchEntryStub,
    PL_DHashMoveEntryStub,
    PL_DHashClearEntryStub,
    PL_DHashFinalizeStub,
    NULL
};

PR_IMPLEMENT(const PLDHashTableOps *)
PL_DHashGetStubOps(void)
{
    return &stub_ops;
}

PR_IMPLEMENT(PLDHashTable *)
PL_NewDHashTable(const PLDHashTableOps *ops, void *data, PRUint32 entrySize,
                 PRUint32 capacity)
{
    PLDHashTable *table;

    table = (PLDHashTable *) malloc(sizeof *table);
    if (!table)
        return NULL;
    if (!PL_DHashTableInit(table, ops, data, entrySize, capacity)) {
        free(table);
        return NULL;
    }
    return table;
}

PR_IMPLEMENT(void)
PL_DHashTableDestroy(PLDHashTable *table)
{
    PL_DHashTableFinish(table);
    free(table);
}

PR_IMPLEMENT(PRBool)
PL_DHashTableInit(PLDHashTable *table, const PLDHashTableOps *ops, void *data,
                  PRUint32 entrySize, PRUint32 capacity)
{
    int log2;
    PRUint32 nbytes;

#ifdef DEBUG
    if (entrySize > 10 * sizeof(void *)) {
        fprintf(stderr,
                "pldhash: for the table at address %p, the given entrySize"
                " of %lu %s favors chaining over double hashing.\n",
                (void *)table,
                (unsigned long) entrySize,
                (entrySize > 16 * sizeof(void*)) ? "definitely" : "probably");
    }
#endif

    table->ops = ops;
    table->data = data;
    if (capacity < PL_DHASH_MIN_SIZE)
        capacity = PL_DHASH_MIN_SIZE;
    log2 = PR_CeilingLog2(capacity);
    capacity = PR_BIT(log2);
    if (capacity >= PL_DHASH_SIZE_LIMIT)
        return PR_FALSE;
    table->hashShift = PL_DHASH_BITS - log2;
    table->maxAlphaFrac = 0xC0;                 /* .75 */
    table->minAlphaFrac = 0x40;                 /* .25 */
    table->entrySize = entrySize;
    table->entryCount = table->removedCount = 0;
    table->generation = 0;
    nbytes = capacity * entrySize;

    table->entryStore = ops->allocTable(table, nbytes);
    if (!table->entryStore)
        return PR_FALSE;
    memset(table->entryStore, 0, nbytes);
    METER(memset(&table->stats, 0, sizeof table->stats));
    return PR_TRUE;
}

/*
 * Compute max and min load numbers (entry counts) from table params.
 */
#define MAX_LOAD(table, size)   (((table)->maxAlphaFrac * (size)) >> 8)
#define MIN_LOAD(table, size)   (((table)->minAlphaFrac * (size)) >> 8)

PR_IMPLEMENT(void)
PL_DHashTableSetAlphaBounds(PLDHashTable *table,
                            float maxAlpha,
                            float minAlpha)
{
    PRUint32 size;

    /*
     * Reject obviously insane bounds, rather than trying to guess what the
     * buggy caller intended.
     */
    PR_ASSERT(0.5 <= maxAlpha && maxAlpha < 1 && 0 <= minAlpha);
    if (maxAlpha < 0.5 || 1 <= maxAlpha || minAlpha < 0)
        return;

    /*
     * Ensure that at least one entry will always be free.  If maxAlpha at
     * minimum size leaves no entries free, reduce maxAlpha based on minimum
     * size and the precision limit of maxAlphaFrac's fixed point format.
     */
    PR_ASSERT(PL_DHASH_MIN_SIZE - (maxAlpha * PL_DHASH_MIN_SIZE) >= 1);
    if (PL_DHASH_MIN_SIZE - (maxAlpha * PL_DHASH_MIN_SIZE) < 1) {
        maxAlpha = (float)
                   (PL_DHASH_MIN_SIZE - PR_MAX(PL_DHASH_MIN_SIZE / 256, 1))
                   / PL_DHASH_MIN_SIZE;
    }

    /*
     * Ensure that minAlpha is strictly less than half maxAlpha.  Take care
     * not to truncate an entry's worth of alpha when storing in minAlphaFrac
     * (8-bit fixed point format).
     */
    PR_ASSERT(minAlpha < maxAlpha / 2);
    if (minAlpha >= maxAlpha / 2) {
        size = PL_DHASH_TABLE_SIZE(table);
        minAlpha = (size * maxAlpha - PR_MAX(size / 256, 1)) / (2 * size);
    }

    table->maxAlphaFrac = (uint8)(maxAlpha * 256);
    table->minAlphaFrac = (uint8)(minAlpha * 256);
}

/*
 * Double hashing needs the second hash code to be relatively prime to table
 * size, so we simply make hash2 odd.
 */
#define HASH1(hash0, shift)         ((hash0) >> (shift))
#define HASH2(hash0,log2,shift)     ((((hash0) << (log2)) >> (shift)) | 1)

/*
 * Reserve keyHash 0 for free entries and 1 for removed-entry sentinels.  Note
 * that a removed-entry sentinel need be stored only if the removed entry had
 * a colliding entry added after it.  Therefore we can use 1 as the collision
 * flag in addition to the removed-entry sentinel value.  Multiplicative hash
 * uses the high order bits of keyHash, so this least-significant reservation
 * should not hurt the hash function's effectiveness much.
 *
 * If you change any of these magic numbers, also update PL_DHASH_ENTRY_IS_LIVE
 * in pldhash.h.  It used to be private to pldhash.c, but then became public to
 * assist iterator writers who inspect table->entryStore directly.
 */
#define COLLISION_FLAG              ((PLDHashNumber) 1)
#define MARK_ENTRY_FREE(entry)      ((entry)->keyHash = 0)
#define MARK_ENTRY_REMOVED(entry)   ((entry)->keyHash = 1)
#define ENTRY_IS_REMOVED(entry)     ((entry)->keyHash == 1)
#define ENTRY_IS_LIVE(entry)        PL_DHASH_ENTRY_IS_LIVE(entry)
#define ENSURE_LIVE_KEYHASH(hash0)  if (hash0 < 2) hash0 -= 2; else (void)0

/* Match an entry's keyHash against an unstored one computed from a key. */
#define MATCH_ENTRY_KEYHASH(entry,hash0) \
    (((entry)->keyHash & ~COLLISION_FLAG) == (hash0))

/* Compute the address of the indexed entry in table. */
#define ADDRESS_ENTRY(table, index) \
    ((PLDHashEntryHdr *)((table)->entryStore + (index) * (table)->entrySize))

PR_IMPLEMENT(void)
PL_DHashTableFinish(PLDHashTable *table)
{
    char *entryAddr, *entryLimit;
    PRUint32 entrySize;
    PLDHashEntryHdr *entry;

#ifdef DEBUG_XXXbrendan
    static FILE *dumpfp = NULL;
    if (!dumpfp) dumpfp = fopen("/tmp/pldhash.bigdump", "w");
    if (dumpfp) {
#ifdef MOZILLA_CLIENT
        NS_TraceStack(1, dumpfp);
#endif
        PL_DHashTableDumpMeter(table, NULL, dumpfp);
        fputc('\n', dumpfp);
    }
#endif

    /* Call finalize before clearing entries, so it can enumerate them. */
    table->ops->finalize(table);

    /* Clear any remaining live entries. */
    entryAddr = table->entryStore;
    entrySize = table->entrySize;
    entryLimit = entryAddr + PL_DHASH_TABLE_SIZE(table) * entrySize;
    while (entryAddr < entryLimit) {
        entry = (PLDHashEntryHdr *)entryAddr;
        if (ENTRY_IS_LIVE(entry)) {
            METER(table->stats.removeEnums++);
            table->ops->clearEntry(table, entry);
        }
        entryAddr += entrySize;
    }

    /* Free entry storage last. */
    table->ops->freeTable(table, table->entryStore);
}

static PLDHashEntryHdr * PL_DHASH_FASTCALL
SearchTable(PLDHashTable *table, const void *key, PLDHashNumber keyHash,
            PLDHashOperator op)
{
    PLDHashNumber hash1, hash2;
    int hashShift, sizeLog2;
    PLDHashEntryHdr *entry, *firstRemoved;
    PLDHashMatchEntry matchEntry;
    PRUint32 sizeMask;

    METER(table->stats.searches++);
    PR_ASSERT(!(keyHash & COLLISION_FLAG));

    /* Compute the primary hash address. */
    hashShift = table->hashShift;
    hash1 = HASH1(keyHash, hashShift);
    entry = ADDRESS_ENTRY(table, hash1);

    /* Miss: return space for a new entry. */
    if (PL_DHASH_ENTRY_IS_FREE(entry)) {
        METER(table->stats.misses++);
        return entry;
    }

    /* Hit: return entry. */
    matchEntry = table->ops->matchEntry;
    if (MATCH_ENTRY_KEYHASH(entry, keyHash) && matchEntry(table, entry, key)) {
        METER(table->stats.hits++);
        return entry;
    }

    /* Collision: double hash. */
    sizeLog2 = PL_DHASH_BITS - table->hashShift;
    hash2 = HASH2(keyHash, sizeLog2, hashShift);
    sizeMask = PR_BITMASK(sizeLog2);

    /* Save the first removed entry pointer so PL_DHASH_ADD can recycle it. */
    if (ENTRY_IS_REMOVED(entry)) {
        firstRemoved = entry;
    } else {
        firstRemoved = NULL;
        if (op == PL_DHASH_ADD)
            entry->keyHash |= COLLISION_FLAG;
    }

    for (;;) {
        METER(table->stats.steps++);
        hash1 -= hash2;
        hash1 &= sizeMask;

        entry = ADDRESS_ENTRY(table, hash1);
        if (PL_DHASH_ENTRY_IS_FREE(entry)) {
            METER(table->stats.misses++);
            return (firstRemoved && op == PL_DHASH_ADD) ? firstRemoved : entry;
        }

        if (MATCH_ENTRY_KEYHASH(entry, keyHash) &&
            matchEntry(table, entry, key)) {
            METER(table->stats.hits++);
            return entry;
        }

        if (ENTRY_IS_REMOVED(entry)) {
            if (!firstRemoved)
                firstRemoved = entry;
        } else {
            if (op == PL_DHASH_ADD)
                entry->keyHash |= COLLISION_FLAG;
        }
    }

    /* NOTREACHED */
    return NULL;
}

static PRBool
ChangeTable(PLDHashTable *table, int deltaLog2)
{
    int oldLog2, newLog2;
    PRUint32 oldCapacity, newCapacity;
    char *newEntryStore, *oldEntryStore, *oldEntryAddr;
    PRUint32 entrySize, i, nbytes;
    PLDHashEntryHdr *oldEntry, *newEntry;
    PLDHashGetKey getKey;
    PLDHashMoveEntry moveEntry;

#ifdef VBOX /* HACK ALERT! generation == PR_UINT32_MAX during enumeration. */
    PR_ASSERT(table->generation != PR_UINT32_MAX);
    if (table->generation == PR_UINT32_MAX)
        return PR_FALSE;
#endif

    /* Look, but don't touch, until we succeed in getting new entry store. */
    oldLog2 = PL_DHASH_BITS - table->hashShift;
    newLog2 = oldLog2 + deltaLog2;
    oldCapacity = PR_BIT(oldLog2);
    newCapacity = PR_BIT(newLog2);
    if (newCapacity >= PL_DHASH_SIZE_LIMIT)
        return PR_FALSE;
    entrySize = table->entrySize;
    nbytes = newCapacity * entrySize;

    newEntryStore = table->ops->allocTable(table, nbytes);
    if (!newEntryStore)
        return PR_FALSE;

    /* We can't fail from here on, so update table parameters. */
    table->hashShift = PL_DHASH_BITS - newLog2;
    table->removedCount = 0;
    table->generation++;
#ifdef VBOX /* HACK ALERT! generation == PR_UINT32_MAX during enumeration. */
    if (table->generation == PR_UINT32_MAX)
        table->generation++;
#endif

    /* Assign the new entry store to table. */
    memset(newEntryStore, 0, nbytes);
    oldEntryAddr = oldEntryStore = table->entryStore;
    table->entryStore = newEntryStore;
    getKey = table->ops->getKey;
    moveEntry = table->ops->moveEntry;

    /* Copy only live entries, leaving removed ones behind. */
    for (i = 0; i < oldCapacity; i++) {
        oldEntry = (PLDHashEntryHdr *)oldEntryAddr;
        if (ENTRY_IS_LIVE(oldEntry)) {
            oldEntry->keyHash &= ~COLLISION_FLAG;
            newEntry = SearchTable(table, getKey(table, oldEntry),
                                   oldEntry->keyHash, PL_DHASH_ADD);
            PR_ASSERT(PL_DHASH_ENTRY_IS_FREE(newEntry));
            moveEntry(table, oldEntry, newEntry);
            newEntry->keyHash = oldEntry->keyHash;
        }
        oldEntryAddr += entrySize;
    }

    table->ops->freeTable(table, oldEntryStore);
    return PR_TRUE;
}

PR_IMPLEMENT(PLDHashEntryHdr *) PL_DHASH_FASTCALL
PL_DHashTableOperate(PLDHashTable *table, const void *key, PLDHashOperator op)
{
    PLDHashNumber keyHash;
    PLDHashEntryHdr *entry;
    PRUint32 size;
    int deltaLog2;

    keyHash = table->ops->hashKey(table, key);
    keyHash *= PL_DHASH_GOLDEN_RATIO;

    /* Avoid 0 and 1 hash codes, they indicate free and removed entries. */
    ENSURE_LIVE_KEYHASH(keyHash);
    keyHash &= ~COLLISION_FLAG;

    switch (op) {
      case PL_DHASH_LOOKUP:
        METER(table->stats.lookups++);
        entry = SearchTable(table, key, keyHash, op);
        break;

      case PL_DHASH_ADD:
        /*
         * If alpha is >= .75, grow or compress the table.  If key is already
         * in the table, we may grow once more than necessary, but only if we
         * are on the edge of being overloaded.
         */
        size = PL_DHASH_TABLE_SIZE(table);
        if (table->entryCount + table->removedCount >= MAX_LOAD(table, size)) {
            /* Compress if a quarter or more of all entries are removed. */
            if (table->removedCount >= size >> 2) {
                METER(table->stats.compresses++);
                deltaLog2 = 0;
            } else {
                METER(table->stats.grows++);
                deltaLog2 = 1;
            }

            /*
             * Grow or compress table, returning null if ChangeTable fails and
             * falling through might claim the last free entry.
             */
            if (!ChangeTable(table, deltaLog2) &&
                table->entryCount + table->removedCount == size - 1) {
                METER(table->stats.addFailures++);
                return NULL;
            }
        }

        /*
         * Look for entry after possibly growing, so we don't have to add it,
         * then skip it while growing the table and re-add it after.
         */
        entry = SearchTable(table, key, keyHash, op);
        if (!ENTRY_IS_LIVE(entry)) {
            /* Initialize the entry, indicating that it's no longer free. */
            METER(table->stats.addMisses++);
            if (ENTRY_IS_REMOVED(entry)) {
                METER(table->stats.addOverRemoved++);
                table->removedCount--;
                keyHash |= COLLISION_FLAG;
            }
            if (table->ops->initEntry &&
                !table->ops->initEntry(table, entry, key)) {
                /* We haven't claimed entry yet; fail with null return. */
                memset(entry + 1, 0, table->entrySize - sizeof *entry);
                return NULL;
            }
            entry->keyHash = keyHash;
            table->entryCount++;
        }
        METER(else table->stats.addHits++);
        break;

      case PL_DHASH_REMOVE:
        entry = SearchTable(table, key, keyHash, op);
        if (ENTRY_IS_LIVE(entry)) {
            /* Clear this entry and mark it as "removed". */
            METER(table->stats.removeHits++);
            PL_DHashTableRawRemove(table, entry);

            /* Shrink if alpha is <= .25 and table isn't too small already. */
            size = PL_DHASH_TABLE_SIZE(table);
            if (size > PL_DHASH_MIN_SIZE &&
#ifdef VBOX /* HACK ALERT! generation == PR_UINT32_MAX during enumeration. */
            /** @todo This is where IPC screws up, avoid the assertion in ChangeTable until it's fixed. */
                table->generation != PR_UINT32_MAX &&
#endif
                table->entryCount <= MIN_LOAD(table, size)) {
                METER(table->stats.shrinks++);
                (void) ChangeTable(table, -1);
            }
        }
        METER(else table->stats.removeMisses++);
        entry = NULL;
        break;

      default:
        PR_ASSERT(0);
        entry = NULL;
    }

    return entry;
}

PR_IMPLEMENT(void)
PL_DHashTableRawRemove(PLDHashTable *table, PLDHashEntryHdr *entry)
{
    PLDHashNumber keyHash;      /* load first in case clearEntry goofs it */

    PR_ASSERT(PL_DHASH_ENTRY_IS_LIVE(entry));
    keyHash = entry->keyHash;
    table->ops->clearEntry(table, entry);
    if (keyHash & COLLISION_FLAG) {
        MARK_ENTRY_REMOVED(entry);
        table->removedCount++;
    } else {
        METER(table->stats.removeFrees++);
        MARK_ENTRY_FREE(entry);
    }
    table->entryCount--;
}

PR_IMPLEMENT(PRUint32)
PL_DHashTableEnumerate(PLDHashTable *table, PLDHashEnumerator etor, void *arg)
{
    char *entryAddr, *entryLimit;
    PRUint32 i, capacity, entrySize;
    PRBool didRemove;
    PLDHashEntryHdr *entry;
    PLDHashOperator op;
#ifdef VBOX /* HACK ALERT! generation == PR_UINT32_MAX during enumeration. */
    PRUint32 generation;

    /*
     * The hack! Set generation to PR_UINT32_MAX during the enumeration so
     * we can prevent ChangeTable from being called.
     *
     * This happens during ipcDConnectService::OnClientStateChange()
     * / ipcDConnectService::DeleteInstance() now when running
     * java clienttest list hostinfo and vboxwebsrv crashes. It's quite
     * likely that the IPC code isn't following the rules here, but it
     * looks more difficult to fix that just hacking this hash code.
     */
    generation = table->generation;
    table->generation = PR_UINT32_MAX;
#endif /* VBOX */
    entryAddr = table->entryStore;
    entrySize = table->entrySize;
    capacity = PL_DHASH_TABLE_SIZE(table);
    entryLimit = entryAddr + capacity * entrySize;
    i = 0;
    didRemove = PR_FALSE;
    while (entryAddr < entryLimit) {
        entry = (PLDHashEntryHdr *)entryAddr;
        if (ENTRY_IS_LIVE(entry)) {
            op = etor(table, entry, i++, arg);
#ifdef VBOX /* HACK ALERT! generation == PR_UINT32_MAX during enumeration. */
            PR_ASSERT(table->generation == PR_UINT32_MAX);
#endif
            if (op & PL_DHASH_REMOVE) {
                METER(table->stats.removeEnums++);
                PL_DHashTableRawRemove(table, entry);
                didRemove = PR_TRUE;
            }
            if (op & PL_DHASH_STOP)
                break;
        }
        entryAddr += entrySize;
    }
#ifdef VBOX /* HACK ALERT! generation == PR_UINT32_MAX during enumeration. */
    table->generation = generation;
#endif

    /*
     * Shrink or compress if a quarter or more of all entries are removed, or
     * if the table is underloaded according to the configured minimum alpha,
     * and is not minimal-size already.  Do this only if we removed above, so
     * non-removing enumerations can count on stable table->entryStore until
     * the next non-lookup-Operate or removing-Enumerate.
     */
    if (didRemove &&
        (table->removedCount >= capacity >> 2 ||
         (capacity > PL_DHASH_MIN_SIZE &&
          table->entryCount <= MIN_LOAD(table, capacity)))) {
        METER(table->stats.enumShrinks++);
        capacity = table->entryCount;
        capacity += capacity >> 1;
        if (capacity < PL_DHASH_MIN_SIZE)
            capacity = PL_DHASH_MIN_SIZE;
        (void) ChangeTable(table,
                           PR_CeilingLog2(capacity)
                           - (PL_DHASH_BITS - table->hashShift));
    }
    return i;
}

#ifdef PL_DHASHMETER
#include <math.h>

PR_IMPLEMENT(void)
PL_DHashTableDumpMeter(PLDHashTable *table, PLDHashEnumerator dump, FILE *fp)
{
    char *entryAddr;
    PRUint32 entrySize, entryCount;
    int hashShift, sizeLog2;
    PRUint32 i, tableSize, sizeMask, chainLen, maxChainLen, chainCount;
    PLDHashNumber hash1, hash2, saveHash1, maxChainHash1, maxChainHash2;
    double sqsum, mean, variance, sigma;
    PLDHashEntryHdr *entry, *probe;

    entryAddr = table->entryStore;
    entrySize = table->entrySize;
    hashShift = table->hashShift;
    sizeLog2 = PL_DHASH_BITS - hashShift;
    tableSize = PL_DHASH_TABLE_SIZE(table);
    sizeMask = PR_BITMASK(sizeLog2);
    chainCount = maxChainLen = 0;
    hash2 = 0;
    sqsum = 0;

    for (i = 0; i < tableSize; i++) {
        entry = (PLDHashEntryHdr *)entryAddr;
        entryAddr += entrySize;
        if (!ENTRY_IS_LIVE(entry))
            continue;
        hash1 = HASH1(entry->keyHash & ~COLLISION_FLAG, hashShift);
        saveHash1 = hash1;
        probe = ADDRESS_ENTRY(table, hash1);
        chainLen = 1;
        if (probe == entry) {
            /* Start of a (possibly unit-length) chain. */
            chainCount++;
        } else {
            hash2 = HASH2(entry->keyHash & ~COLLISION_FLAG, sizeLog2,
                          hashShift);
            do {
                chainLen++;
                hash1 -= hash2;
                hash1 &= sizeMask;
                probe = ADDRESS_ENTRY(table, hash1);
            } while (probe != entry);
        }
        sqsum += chainLen * chainLen;
        if (chainLen > maxChainLen) {
            maxChainLen = chainLen;
            maxChainHash1 = saveHash1;
            maxChainHash2 = hash2;
        }
    }

    entryCount = table->entryCount;
    if (entryCount && chainCount) {
        mean = (double)entryCount / chainCount;
        variance = chainCount * sqsum - entryCount * entryCount;
        if (variance < 0 || chainCount == 1)
            variance = 0;
        else
            variance /= chainCount * (chainCount - 1);
        sigma = sqrt(variance);
    } else {
        mean = sigma = 0;
    }

    fprintf(fp, "Double hashing statistics:\n");
    fprintf(fp, "    table size (in entries): %u\n", tableSize);
    fprintf(fp, "          number of entries: %u\n", table->entryCount);
    fprintf(fp, "  number of removed entries: %u\n", table->removedCount);
    fprintf(fp, "         number of searches: %u\n", table->stats.searches);
    fprintf(fp, "             number of hits: %u\n", table->stats.hits);
    fprintf(fp, "           number of misses: %u\n", table->stats.misses);
    fprintf(fp, "      mean steps per search: %g\n", table->stats.searches ?
                                                     (double)table->stats.steps
                                                     / table->stats.searches :
                                                     0.);
    fprintf(fp, "     mean hash chain length: %g\n", mean);
    fprintf(fp, "         standard deviation: %g\n", sigma);
    fprintf(fp, "  maximum hash chain length: %u\n", maxChainLen);
    fprintf(fp, "          number of lookups: %u\n", table->stats.lookups);
    fprintf(fp, " adds that made a new entry: %u\n", table->stats.addMisses);
    fprintf(fp, "adds that recycled removeds: %u\n", table->stats.addOverRemoved);
    fprintf(fp, "   adds that found an entry: %u\n", table->stats.addHits);
    fprintf(fp, "               add failures: %u\n", table->stats.addFailures);
    fprintf(fp, "             useful removes: %u\n", table->stats.removeHits);
    fprintf(fp, "            useless removes: %u\n", table->stats.removeMisses);
    fprintf(fp, "removes that freed an entry: %u\n", table->stats.removeFrees);
    fprintf(fp, "  removes while enumerating: %u\n", table->stats.removeEnums);
    fprintf(fp, "            number of grows: %u\n", table->stats.grows);
    fprintf(fp, "          number of shrinks: %u\n", table->stats.shrinks);
    fprintf(fp, "       number of compresses: %u\n", table->stats.compresses);
    fprintf(fp, "number of enumerate shrinks: %u\n", table->stats.enumShrinks);

    if (dump && maxChainLen && hash2) {
        fputs("Maximum hash chain:\n", fp);
        hash1 = maxChainHash1;
        hash2 = maxChainHash2;
        entry = ADDRESS_ENTRY(table, hash1);
        i = 0;
        do {
            if (dump(table, entry, i++, fp) != PL_DHASH_NEXT)
                break;
            hash1 -= hash2;
            hash1 &= sizeMask;
            entry = ADDRESS_ENTRY(table, hash1);
        } while (PL_DHASH_ENTRY_IS_BUSY(entry));
    }
}
#endif /* PL_DHASHMETER */
