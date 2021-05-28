/* $Id$ */
/** @file
 * Audio mixing routines for multiplexing audio sources in device emulations.
 *
 * Overview
 * ========
 *
 * This mixer acts as a layer between the audio connector interface and
 * the actual device emulation, providing mechanisms for audio sources (input)
 * and audio sinks (output).
 *
 * Think of this mixer as kind of a high(er) level interface for the audio
 * connector interface, abstracting common tasks such as creating and managing
 * various audio sources and sinks. This mixer class is purely optional and can
 * be left out when implementing a new device emulation, using only the audi
 * connector interface instead.  For example, the SB16 emulation does not use
 * this mixer and does all its stream management on its own.
 *
 * As audio driver instances are handled as LUNs on the device level, this
 * audio mixer then can take care of e.g. mixing various inputs/outputs to/from
 * a specific source/sink.
 *
 * How and which audio streams are connected to sinks/sources depends on how
 * the audio mixer has been set up.
 *
 * A sink can connect multiple output streams together, whereas a source
 * does this with input streams. Each sink / source consists of one or more
 * so-called mixer streams, which then in turn have pointers to the actual
 * PDM audio input/output streams.
 *
 * Playback
 * ========
 *
 * For output sinks there can be one or more mixing stream attached.
 * As the host sets the overall pace for the device emulation (virtual time
 * in the guest OS vs. real time on the host OS), an output mixing sink
 * needs to make sure that all connected output streams are able to accept
 * all the same amount of data at a time.
 *
 * This is called synchronous multiplexing.
 *
 * A mixing sink employs an own audio mixing buffer, which in turn can convert
 * the audio (output) data supplied from the device emulation into the sink's
 * audio format. As all connected mixing streams in theory could have the same
 * audio format as the mixing sink (parent), this can save processing time when
 * it comes to serving a lot of mixing streams at once. That way only one
 * conversion must be done, instead of each stream having to iterate over the
 * data.
 *
 * Recording
 * =========
 *
 * For input sinks only one mixing stream at a time can be the recording
 * source currently. A recording source is optional, e.g. it is possible to
 * have no current recording source set. Switching to a different recording
 * source at runtime is possible.
 */

/*
 * Copyright (C) 2014-2020 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_AUDIO_MIXER
#include <VBox/log.h>
#include "AudioMixer.h"
#include "AudioMixBuffer.h"
#include "AudioHlp.h"

#include <VBox/vmm/pdm.h>
#include <VBox/err.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/pdmaudioifs.h>
#include <VBox/vmm/pdmaudioinline.h>

#include <iprt/alloc.h>
#include <iprt/asm-math.h>
#include <iprt/assert.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#ifdef VBOX_WITH_DTRACE
# include "dtrace/VBoxDD.h"
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int audioMixerAddSinkInternal(PAUDIOMIXER pMixer, PAUDMIXSINK pSink);
static int audioMixerRemoveSinkInternal(PAUDIOMIXER pMixer, PAUDMIXSINK pSink);

static int audioMixerSinkInit(PAUDMIXSINK pSink, PAUDIOMIXER pMixer, const char *pcszName, PDMAUDIODIR enmDir, PPDMDEVINS pDevIns);
static void audioMixerSinkDestroyInternal(PAUDMIXSINK pSink, PPDMDEVINS pDevIns);
static int audioMixerSinkUpdateVolume(PAUDMIXSINK pSink, PCPDMAUDIOVOLUME pVolMaster);
static void audioMixerSinkRemoveAllStreamsInternal(PAUDMIXSINK pSink);
static int audioMixerSinkRemoveStreamInternal(PAUDMIXSINK pSink, PAUDMIXSTREAM pStream);
static void audioMixerSinkReset(PAUDMIXSINK pSink);

static int audioMixerStreamCtlInternal(PAUDMIXSTREAM pMixStream, PDMAUDIOSTREAMCMD enmCmd);
static void audioMixerStreamDestroyInternal(PAUDMIXSTREAM pStream, PPDMDEVINS pDevIns, bool fImmediate);
static int audioMixerStreamUpdateStatus(PAUDMIXSTREAM pMixStream);


/** size of output buffer for dbgAudioMixerSinkStatusToStr.   */
#define AUDIOMIXERSINK_STATUS_STR_MAX sizeof("RUNNING DRAINING DRAINED_DMA DRAINED_MIXBUF DIRTY 0x12345678")

/**
 * Converts a mixer sink status to a string.
 *
 * @returns pszDst
 * @param   fStatus     The mixer sink status.
 * @param   pszDst      The output buffer.  Must be at least
 *                      AUDIOMIXERSINK_STATUS_STR_MAX in length.
 */
static const char *dbgAudioMixerSinkStatusToStr(uint32_t fStatus, char pszDst[AUDIOMIXERSINK_STATUS_STR_MAX])
{
    if (!fStatus)
        return strcpy(pszDst, "NONE");
    static const struct
    {
        const char *pszMnemonic;
        uint32_t    cchMnemonic;
        uint32_t    fStatus;
    } s_aFlags[] =
    {
        { RT_STR_TUPLE("RUNNING "),         AUDMIXSINK_STS_RUNNING },
        { RT_STR_TUPLE("DRAINING "),        AUDMIXSINK_STS_DRAINING },
        { RT_STR_TUPLE("DRAINED_DMA "),     AUDMIXSINK_STS_DRAINED_DMA },
        { RT_STR_TUPLE("DRAINED_MIXBUF "),  AUDMIXSINK_STS_DRAINED_MIXBUF },
        { RT_STR_TUPLE("DIRTY "),           AUDMIXSINK_STS_DIRTY },
    };
    char *psz = pszDst;
    for (size_t i = 0; i < RT_ELEMENTS(s_aFlags); i++)
        if (fStatus & s_aFlags[i].fStatus)
        {
            memcpy(psz, s_aFlags[i].pszMnemonic, s_aFlags[i].cchMnemonic);
            psz += s_aFlags[i].cchMnemonic;
            fStatus &= ~s_aFlags[i].fStatus;
            if (!fStatus)
            {
                psz[-1] = '\0';
                return pszDst;
            }
        }
    RTStrPrintf(psz, AUDIOMIXERSINK_STATUS_STR_MAX - (psz - pszDst), "%#x", fStatus);
    return pszDst;
}


/**
 * Creates an audio mixer.
 *
 * @returns VBox status code.
 * @param   pszName     Name of the audio mixer.
 * @param   fFlags      Creation flags - AUDMIXER_FLAGS_XXX.
 * @param   ppMixer     Pointer which returns the created mixer object.
 */
int AudioMixerCreate(const char *pszName, uint32_t fFlags, PAUDIOMIXER *ppMixer)
{
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    size_t const cchName = strlen(pszName);
    AssertReturn(cchName > 0 && cchName < 128, VERR_INVALID_NAME);
    AssertReturn  (!(fFlags & ~AUDMIXER_FLAGS_VALID_MASK), VERR_INVALID_FLAGS);
    AssertPtrReturn(ppMixer, VERR_INVALID_POINTER);

    int         rc;
    PAUDIOMIXER pMixer = (PAUDIOMIXER)RTMemAllocZ(sizeof(AUDIOMIXER) + cchName + 1);
    if (pMixer)
    {
        rc = RTCritSectInit(&pMixer->CritSect);
        if (RT_SUCCESS(rc))
        {
            pMixer->pszName = (const char *)memcpy(pMixer + 1, pszName, cchName + 1);

            pMixer->cSinks = 0;
            RTListInit(&pMixer->lstSinks);

            pMixer->fFlags = fFlags;
            pMixer->uMagic = AUDIOMIXER_MAGIC;

            if (pMixer->fFlags & AUDMIXER_FLAGS_DEBUG)
                LogRel(("Audio Mixer: Debug mode enabled\n"));

            /* Set master volume to the max. */
            pMixer->VolMaster.fMuted = false;
            pMixer->VolMaster.uLeft  = PDMAUDIO_VOLUME_MAX;
            pMixer->VolMaster.uRight = PDMAUDIO_VOLUME_MAX;

            LogFlowFunc(("Created mixer '%s'\n", pMixer->pszName));
            *ppMixer = pMixer;
            return VINF_SUCCESS;
        }
        RTMemFree(pMixer);
    }
    else
        rc = VERR_NO_MEMORY;
    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Destroys an audio mixer.
 *
 * @param   pMixer      Audio mixer to destroy.  NULL is ignored.
 * @param   pDevIns     The device instance the statistics are associated with.
 */
void AudioMixerDestroy(PAUDIOMIXER pMixer, PPDMDEVINS pDevIns)
{
    if (!pMixer)
        return;
    AssertPtrReturnVoid(pMixer);
    AssertReturnVoid(pMixer->uMagic == AUDIOMIXER_MAGIC);

    int rc2 = RTCritSectEnter(&pMixer->CritSect);
    AssertRCReturnVoid(rc2);

    LogFlowFunc(("Destroying %s ...\n", pMixer->pszName));
    pMixer->uMagic = AUDIOMIXER_MAGIC_DEAD;

    PAUDMIXSINK pSink, pSinkNext;
    RTListForEachSafe(&pMixer->lstSinks, pSink, pSinkNext, AUDMIXSINK, Node)
    {
        audioMixerSinkDestroyInternal(pSink, pDevIns);
        audioMixerRemoveSinkInternal(pMixer, pSink);
        RTMemFree(pSink);
    }
    Assert(pMixer->cSinks == 0);

    rc2 = RTCritSectLeave(&pMixer->CritSect);
    AssertRC(rc2);

    RTCritSectDelete(&pMixer->CritSect);
    RTMemFree(pMixer);
}


/**
 * Helper function for the internal debugger to print the mixer's current
 * state, along with the attached sinks.
 *
 * @param   pMixer              Mixer to print debug output for.
 * @param   pHlp                Debug info helper to use.
 * @param   pszArgs             Optional arguments. Not being used at the moment.
 */
void AudioMixerDebug(PAUDIOMIXER pMixer, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);
    Assert(pMixer->uMagic == AUDIOMIXER_MAGIC);

    int rc2 = RTCritSectEnter(&pMixer->CritSect);
    AssertRCReturnVoid(rc2);

    pHlp->pfnPrintf(pHlp, "[Master] %s: lVol=%u, rVol=%u, fMuted=%RTbool\n", pMixer->pszName,
                    pMixer->VolMaster.uLeft, pMixer->VolMaster.uRight, pMixer->VolMaster.fMuted);

    PAUDMIXSINK pSink;
    unsigned    iSink = 0;
    RTListForEach(&pMixer->lstSinks, pSink, AUDMIXSINK, Node)
    {
        pHlp->pfnPrintf(pHlp, "[Sink %u] %s: lVol=%u, rVol=%u, fMuted=%RTbool\n", iSink, pSink->pszName,
                        pSink->Volume.uLeft, pSink->Volume.uRight, pSink->Volume.fMuted);
        ++iSink;
    }

    rc2 = RTCritSectLeave(&pMixer->CritSect);
    AssertRC(rc2);
}

/**
 * Invalidates all internal data, internal version.
 *
 * @returns VBox status code.
 * @param   pMixer              Mixer to invalidate data for.
 */
static int audioMixerInvalidateInternal(PAUDIOMIXER pMixer)
{
    AssertPtrReturn(pMixer, VERR_INVALID_POINTER);
    LogFlowFunc(("[%s]\n", pMixer->pszName));

    /* Propagate new master volume to all connected sinks. */
    PAUDMIXSINK pSink;
    RTListForEach(&pMixer->lstSinks, pSink, AUDMIXSINK, Node)
    {
        int rc2 = audioMixerSinkUpdateVolume(pSink, &pMixer->VolMaster);
        AssertRC(rc2);
    }

    return VINF_SUCCESS;
}

#if 0 /* unused */
/**
 * Invalidates all internal data.
 *
 * @returns VBox status code.
 * @param   pMixer              Mixer to invalidate data for.
 */
void AudioMixerInvalidate(PAUDIOMIXER pMixer)
{
    AssertPtrReturnVoid(pMixer);
    Assert(pMixer->uMagic == AUDIOMIXER_MAGIC);

    int rc2 = RTCritSectEnter(&pMixer->CritSect);
    AssertRC(rc2);

    LogFlowFunc(("[%s]\n", pMixer->pszName));

    rc2 = audioMixerInvalidateInternal(pMixer);
    AssertRC(rc2);

    rc2 = RTCritSectLeave(&pMixer->CritSect);
    AssertRC(rc2);
}
#endif

/**
 * Adds sink to an existing mixer.
 *
 * @returns VBox status code.
 * @param   pMixer              Mixer to add sink to.
 * @param   pSink               Sink to add.
 */
static int audioMixerAddSinkInternal(PAUDIOMIXER pMixer, PAUDMIXSINK pSink)
{
    AssertPtrReturn(pMixer, VERR_INVALID_POINTER);
    AssertPtrReturn(pSink,  VERR_INVALID_POINTER);

    /** @todo Check upper sink limit? */
    /** @todo Check for double-inserted sinks? */

    RTListAppend(&pMixer->lstSinks, &pSink->Node);
    pMixer->cSinks++;

    LogFlowFunc(("pMixer=%p, pSink=%p, cSinks=%RU8\n",
                 pMixer, pSink, pMixer->cSinks));

    return VINF_SUCCESS;
}

/**
 * Removes a formerly attached audio sink for an audio mixer, internal version.
 *
 * @returns VBox status code.
 * @param   pMixer              Mixer to remove sink from.
 * @param   pSink               Sink to remove.
 */
static int audioMixerRemoveSinkInternal(PAUDIOMIXER pMixer, PAUDMIXSINK pSink)
{
    AssertPtrReturn(pMixer, VERR_INVALID_POINTER);
    if (!pSink)
        return VERR_NOT_FOUND;

    AssertMsgReturn(pSink->pParent == pMixer, ("%s: Is not part of mixer '%s'\n",
                                               pSink->pszName, pMixer->pszName), VERR_NOT_FOUND);

    LogFlowFunc(("[%s] pSink=%s, cSinks=%RU8\n",
                 pMixer->pszName, pSink->pszName, pMixer->cSinks));

    /* Remove sink from mixer. */
    RTListNodeRemove(&pSink->Node);

    Assert(pMixer->cSinks);
    pMixer->cSinks--;

    /* Set mixer to NULL so that we know we're not part of any mixer anymore. */
    pSink->pParent = NULL;

    return VINF_SUCCESS;
}

/**
 * Sets the mixer's master volume.
 *
 * @returns VBox status code.
 * @param   pMixer              Mixer to set master volume for.
 * @param   pVol                Volume to set.
 */
int AudioMixerSetMasterVolume(PAUDIOMIXER pMixer, PPDMAUDIOVOLUME pVol)
{
    AssertPtrReturn(pMixer, VERR_INVALID_POINTER);
    Assert(pMixer->uMagic == AUDIOMIXER_MAGIC);
    AssertPtrReturn(pVol,   VERR_INVALID_POINTER);

    int rc = RTCritSectEnter(&pMixer->CritSect);
    AssertRCReturn(rc, rc);

    memcpy(&pMixer->VolMaster, pVol, sizeof(PDMAUDIOVOLUME));

    LogFlowFunc(("[%s] lVol=%RU32, rVol=%RU32 => fMuted=%RTbool, lVol=%RU32, rVol=%RU32\n",
                 pMixer->pszName, pVol->uLeft, pVol->uRight,
                 pMixer->VolMaster.fMuted, pMixer->VolMaster.uLeft, pMixer->VolMaster.uRight));

    rc = audioMixerInvalidateInternal(pMixer);

    int rc2 = RTCritSectLeave(&pMixer->CritSect);
    AssertRC(rc2);

    return rc;
}


/*********************************************************************************************************************************
*   Mixer Sink implementation.                                                                                                   *
*********************************************************************************************************************************/

/**
 * Creates an audio sink and attaches it to the given mixer.
 *
 * @returns VBox status code.
 * @param   pMixer      Mixer to attach created sink to.
 * @param   pszName     Name of the sink to create.
 * @param   enmDir      Direction of the sink to create.
 * @param   pDevIns     The device instance to register statistics under.
 * @param   ppSink      Pointer which returns the created sink on success.
 */
int AudioMixerCreateSink(PAUDIOMIXER pMixer, const char *pszName, PDMAUDIODIR enmDir, PPDMDEVINS pDevIns, PAUDMIXSINK *ppSink)
{
    AssertPtrReturn(pMixer, VERR_INVALID_POINTER);
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    /* ppSink is optional. */

    int rc = RTCritSectEnter(&pMixer->CritSect);
    AssertRCReturn(rc, rc);

    PAUDMIXSINK pSink = (PAUDMIXSINK)RTMemAllocZ(sizeof(AUDMIXSINK));
    if (pSink)
    {
        rc = audioMixerSinkInit(pSink, pMixer, pszName, enmDir, pDevIns);
        if (RT_SUCCESS(rc))
        {
            rc = audioMixerAddSinkInternal(pMixer, pSink);
            if (RT_SUCCESS(rc))
            {
                RTCritSectLeave(&pMixer->CritSect);

                char szPrefix[128];
                RTStrPrintf(szPrefix, sizeof(szPrefix), "MixerSink-%s/", pSink->pszName);
                PDMDevHlpSTAMRegisterF(pDevIns, &pSink->MixBuf.cFrames, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_NONE,
                                       "Sink mixer buffer size in frames.",         "%sMixBufSize", szPrefix);
                PDMDevHlpSTAMRegisterF(pDevIns, &pSink->MixBuf.cUsed, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_NONE,
                                       "Sink mixer buffer fill size in frames.",    "%sMixBufUsed", szPrefix);
                PDMDevHlpSTAMRegisterF(pDevIns, &pSink->cStreams, STAMTYPE_U8, STAMVISIBILITY_USED, STAMUNIT_NONE,
                                       "Number of streams attached to the sink.",   "%sStreams", szPrefix);

                if (ppSink)
                    *ppSink = pSink;
                return VINF_SUCCESS;
            }
        }

        audioMixerSinkDestroyInternal(pSink, pDevIns);

        RTMemFree(pSink);
        pSink = NULL;
    }
    else
        rc = VERR_NO_MEMORY;

    RTCritSectLeave(&pMixer->CritSect);
    return rc;
}


/**
 * Adds an audio stream to a specific audio sink.
 *
 * @returns VBox status code.
 * @param   pSink               Sink to add audio stream to.
 * @param   pStream             Stream to add.
 */
int AudioMixerSinkAddStream(PAUDMIXSINK pSink, PAUDMIXSTREAM pStream)
{
    LogFlowFuncEnter();
    AssertPtrReturn(pSink,   VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    AssertPtrReturn(pStream, VERR_INVALID_POINTER);
    Assert(pStream->uMagic == AUDMIXSTREAM_MAGIC);
    AssertPtrReturn(pStream->pConn, VERR_AUDIO_STREAM_NOT_READY);
    AssertReturn(pStream->pSink == NULL, VERR_ALREADY_EXISTS);

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, rc);

    AssertLogRelMsgReturnStmt(pSink->cStreams < UINT8_MAX, ("too many streams!\n"), RTCritSectLeave(&pSink->CritSect),
                              VERR_TOO_MANY_OPEN_FILES);

    /*
     * If the sink is running and not in pending disable mode, make sure that
     * the added stream also is enabled.   Ignore any failure to enable it.
     */
    if (    (pSink->fStatus & AUDMIXSINK_STS_RUNNING)
        && !(pSink->fStatus & AUDMIXSINK_STS_DRAINING))
    {
        audioMixerStreamCtlInternal(pStream, PDMAUDIOSTREAMCMD_ENABLE);
    }

    /* Save pointer to sink the stream is attached to. */
    pStream->pSink = pSink;

    /* Append stream to sink's list. */
    RTListAppend(&pSink->lstStreams, &pStream->Node);
    pSink->cStreams++;

    LogFlowFunc(("[%s] cStreams=%RU8, rc=%Rrc\n", pSink->pszName, pSink->cStreams, rc));
    RTCritSectLeave(&pSink->CritSect);
    return rc;
}

/**
 * Creates an audio mixer stream.
 *
 * @returns VBox status code.
 * @param   pSink       Sink to use for creating the stream.
 * @param   pConn       Audio connector interface to use.
 * @param   pCfg        Audio stream configuration to use.  This may be modified
 *                      in some unspecified way (see
 *                      PDMIAUDIOCONNECTOR::pfnStreamCreate).
 * @param   pDevIns     The device instance to register statistics with.
 * @param   ppStream    Pointer which receives the newly created audio stream.
 */
int AudioMixerSinkCreateStream(PAUDMIXSINK pSink, PPDMIAUDIOCONNECTOR pConn, PPDMAUDIOSTREAMCFG pCfg,
                               PPDMDEVINS pDevIns, PAUDMIXSTREAM *ppStream)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    AssertPtrReturn(pConn, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfg, VERR_INVALID_POINTER);
    AssertPtrNullReturn(ppStream, VERR_INVALID_POINTER);
    Assert(pSink->AIO.pDevIns == pDevIns); RT_NOREF(pDevIns); /* we'll probably be adding more statistics */
    AssertReturn(pCfg->enmDir == pSink->enmDir, VERR_MISMATCH);

    /*
     * Check status and get the host driver config.
     */
    if (pConn->pfnGetStatus(pConn, PDMAUDIODIR_DUPLEX) == PDMAUDIOBACKENDSTS_NOT_ATTACHED)
        return VERR_AUDIO_BACKEND_NOT_ATTACHED;

    PDMAUDIOBACKENDCFG BackendCfg;
    int rc = pConn->pfnGetConfig(pConn, &BackendCfg);
    AssertRCReturn(rc, rc);

    /*
     * Allocate the instance.
     */
    PAUDMIXSTREAM pMixStream = (PAUDMIXSTREAM)RTMemAllocZ(sizeof(AUDMIXSTREAM));
    AssertReturn(pMixStream, VERR_NO_MEMORY);

    /* Assign the backend's name to the mixer stream's name for easier identification in the (release) log. */
    pMixStream->pszName = RTStrAPrintf2("[%s] %s", pCfg->szName, BackendCfg.szName);
    pMixStream->pszStatPrefix = RTStrAPrintf2("MixerSink-%s/%s/", pSink->pszName, BackendCfg.szName);
    if (pMixStream->pszName && pMixStream->pszStatPrefix)
    {
        rc = RTCritSectInit(&pMixStream->CritSect);
        if (RT_SUCCESS(rc))
        {
            /*
             * Lock the sink so we can safely get it's properties and call
             * down into the audio driver to create that end of the stream.
             */
            rc = RTCritSectEnter(&pSink->CritSect);
            AssertRC(rc);
            if (RT_SUCCESS(rc))
            {
                LogFlowFunc(("[%s] (enmDir=%ld, %u bits, %RU8 channels, %RU32Hz)\n", pSink->pszName, pCfg->enmDir,
                             PDMAudioPropsSampleBits(&pCfg->Props), PDMAudioPropsChannels(&pCfg->Props), pCfg->Props.uHz));

                /*
                 * Initialize the host-side configuration for the stream to be created,
                 * this is the sink format & direction with the src/dir, layout, name
                 * and device specific config copied from the guest side config (pCfg).
                 */
                AssertMsg(AudioHlpPcmPropsAreValid(&pSink->PCMProps),
                          ("%s: Does not (yet) have a format set when it must\n", pSink->pszName));

                PDMAUDIOSTREAMCFG CfgHost;
                rc = PDMAudioStrmCfgInitWithProps(&CfgHost, &pSink->PCMProps);
                AssertRC(rc); /* cannot fail */
                CfgHost.enmDir    = pSink->enmDir;
                CfgHost.enmPath   = pCfg->enmPath;
                CfgHost.enmLayout = pCfg->enmLayout;
                CfgHost.Device    = pCfg->Device;
                RTStrCopy(CfgHost.szName, sizeof(CfgHost.szName), pCfg->szName);

                /*
                 * Create the stream.
                 *
                 * Output streams are not using any mixing buffers in DrvAudio.  This will
                 * become the norm after we move the input mixing here and convert DevSB16
                 * to use this mixer code too.
                 */
                PPDMAUDIOSTREAM pStream;
                rc = pConn->pfnStreamCreate(pConn, 0 /*fFlags*/, &CfgHost, pCfg, &pStream);
                if (RT_SUCCESS(rc))
                {
                    pMixStream->cFramesBackendBuffer = CfgHost.Backend.cFramesBufferSize;

                    /* Set up the mixing buffer conversion state. */
                    if (pSink->enmDir == PDMAUDIODIR_IN)
                        rc = AudioMixBufInitWriteState(&pSink->MixBuf, &pMixStream->WriteState, &pStream->Props);
                    else
                        rc = AudioMixBufInitPeekState(&pSink->MixBuf, &pMixStream->PeekState, &pStream->Props);
                    if (RT_SUCCESS(rc))
                    {
                        /* Save the audio stream pointer to this mixing stream. */
                        pMixStream->pStream = pStream;

                        /* Increase the stream's reference count to let others know
                         * we're relying on it to be around now. */
                        pConn->pfnStreamRetain(pConn, pStream);
                        pMixStream->pConn  = pConn;
                        pMixStream->uMagic = AUDMIXSTREAM_MAGIC;

                        RTCritSectLeave(&pSink->CritSect);

                        if (ppStream)
                            *ppStream = pMixStream;
                        return VINF_SUCCESS;
                    }

                    rc = pConn->pfnStreamDestroy(pConn, pStream, true /*fImmediate*/);
                }

                /*
                 * Failed.  Tear down the stream.
                 */
                int rc2 = RTCritSectLeave(&pSink->CritSect);
                AssertRC(rc2);
            }
            RTCritSectDelete(&pMixStream->CritSect);
        }
    }
    else
        rc = VERR_NO_STR_MEMORY;

    RTStrFree(pMixStream->pszStatPrefix);
    pMixStream->pszStatPrefix = NULL;
    RTStrFree(pMixStream->pszName);
    pMixStream->pszName = NULL;
    RTMemFree(pMixStream);
    return rc;
}


/**
 * Starts playback/capturing on the mixer sink.
 *
 * @returns VBox status code.  Generally always VINF_SUCCESS unless the input
 *          is invalid.  Individual driver errors are suppressed and ignored.
 * @param   pSink       Mixer sink to control.
 */
int AudioMixerSinkStart(PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, rc);
    char szStatus[AUDIOMIXERSINK_STATUS_STR_MAX];
    LogFunc(("Starting '%s'. Old status: %s\n", pSink->pszName, dbgAudioMixerSinkStatusToStr(pSink->fStatus, szStatus)));

    AssertReturnStmt(pSink->enmDir == PDMAUDIODIR_IN || pSink->enmDir == PDMAUDIODIR_OUT,
                     RTCritSectLeave(&pSink->CritSect), VERR_INTERNAL_ERROR_3);

    /*
     * Make sure the sink and its streams are all stopped.
     */
    if (!(pSink->fStatus & AUDMIXSINK_STS_RUNNING))
        Assert(pSink->fStatus == AUDMIXSINK_STS_NONE);
    else
    {
        LogFunc(("%s: This sink is still running!! Stop it before starting it again.\n", pSink->pszName));

        PAUDMIXSTREAM pStream;
        RTListForEach(&pSink->lstStreams, pStream, AUDMIXSTREAM, Node)
        {
            /** @todo PDMAUDIOSTREAMCMD_STOP_NOW   */
            audioMixerStreamCtlInternal(pStream, PDMAUDIOSTREAMCMD_DISABLE);
        }
        audioMixerSinkReset(pSink);
    }

    /*
     * Send the command to the streams.
     */
    PAUDMIXSTREAM pStream;
    RTListForEach(&pSink->lstStreams, pStream, AUDMIXSTREAM, Node)
    {
        audioMixerStreamCtlInternal(pStream, PDMAUDIOSTREAMCMD_ENABLE);
    }

    /*
     * Update the sink status.
     */
    pSink->fStatus = AUDMIXSINK_STS_RUNNING;

    LogRel2(("Audio Mixer: Started sink '%s': %s\n", pSink->pszName, dbgAudioMixerSinkStatusToStr(pSink->fStatus, szStatus)));

    RTCritSectLeave(&pSink->CritSect);
    return VINF_SUCCESS;
}


/**
 * Helper for AudioMixerSinkDrainAndStop that calculates the max length a drain
 * operation should take.
 *
 * @returns The drain deadline (relative to RTTimeNanoTS).
 * @param   pSink               The sink.
 * @param   cbDmaLeftToDrain    The number of bytes in the DMA buffer left to
 *                              transfer into the mixbuf.
 */
static uint64_t audioMixerSinkDrainDeadline(PAUDMIXSINK pSink, uint32_t cbDmaLeftToDrain)
{
    /*
     * Calculate the max backend buffer size in mixbuf frames.
     * (This is somewhat similar to audioMixerSinkUpdateOutputCalcFramesToRead.)
     */
    uint32_t      cFramesStreamMax = 0;
    PAUDMIXSTREAM pMixStream;
    RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
    {
        /*LogFunc(("Stream '%s': %#x (%u frames)\n", pMixStream->pszName, pMixStream->fStatus, pMixStream->cFramesBackendBuffer));*/
        if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_CAN_WRITE)
        {
            uint32_t cFrames = pMixStream->cFramesBackendBuffer;
            if (PDMAudioPropsHz(&pMixStream->pStream->Props) == PDMAudioPropsHz(&pSink->MixBuf.Props))
            { /* likely */ }
            else
                cFrames = cFrames * PDMAudioPropsHz(&pSink->MixBuf.Props) / PDMAudioPropsHz(&pMixStream->pStream->Props);
            if (cFrames > cFramesStreamMax)
            {
                Log4Func(("%s: cFramesStreamMax %u -> %u; %s\n", pSink->pszName, cFramesStreamMax, cFrames, pMixStream->pszName));
                cFramesStreamMax = cFrames;
            }
        }
    }

    /*
     * Combine that with the pending DMA and mixbuf content, then convert
     * to nanoseconds and apply a fudge factor to get a generous deadline.
     */
    uint32_t const cFramesDmaAndMixBuf = PDMAudioPropsBytesToFrames(&pSink->MixBuf.Props, cbDmaLeftToDrain)
                                       + AudioMixBufUsed(&pSink->MixBuf);
    uint64_t const cNsToDrainMax       = PDMAudioPropsFramesToNano(&pSink->MixBuf.Props, cFramesDmaAndMixBuf + cFramesStreamMax);
    uint64_t const nsDeadline          = cNsToDrainMax * 2;
    LogFlowFunc(("%s: cFramesStreamMax=%#x cFramesDmaAndMixBuf=%#x -> cNsToDrainMax=%RU64 -> %RU64\n",
                 pSink->pszName, cFramesStreamMax, cFramesDmaAndMixBuf, cNsToDrainMax, nsDeadline));
    return nsDeadline;
}


/**
 * Kicks off the draining and stopping playback/capture on the mixer sink.
 *
 * For input streams this causes an immediate stop, as draining only makes sense
 * to output stream in the VBox device context.
 *
 * @returns VBox status code.  Generally always VINF_SUCCESS unless the input
 *          is invalid.  Individual driver errors are suppressed and ignored.
 * @param   pSink       Mixer sink to control.
 * @param   cbComming   The number of bytes still left in the device's DMA
 *                      buffers that the update job has yet to transfer.  This
 *                      is ignored for input streams.
 */
int AudioMixerSinkDrainAndStop(PAUDMIXSINK pSink, uint32_t cbComming)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, rc);
    char szStatus[AUDIOMIXERSINK_STATUS_STR_MAX];
    LogFunc(("Draining '%s' with %#x bytes left. Old status: %s\n",
             pSink->pszName, cbComming, dbgAudioMixerSinkStatusToStr(pSink->fStatus, szStatus) ));

    AssertReturnStmt(pSink->enmDir == PDMAUDIODIR_IN || pSink->enmDir == PDMAUDIODIR_OUT,
                     RTCritSectLeave(&pSink->CritSect), VERR_INTERNAL_ERROR_3);

    if (pSink->fStatus & AUDMIXSINK_STS_RUNNING)
    {
        /*
         * Output streams will be drained then stopped (all by the AIO thread).
         *
         * For streams we define that they shouldn't not be written to after we start draining,
         * so we have to hold back sending the command to them till we've processed all the
         * cbComming remaining bytes in the DMA buffer.
         */
        if (pSink->enmDir == PDMAUDIODIR_OUT)
        {
            if (!(pSink->fStatus & AUDMIXSINK_STS_DRAINING))
            {
                Assert(!(pSink->fStatus & (AUDMIXSINK_STS_DRAINED_DMA | AUDMIXSINK_STS_DRAINED_MIXBUF)));

                /* Update the status and draining member. */
                pSink->cbDmaLeftToDrain = cbComming;
                pSink->nsDrainDeadline  = audioMixerSinkDrainDeadline(pSink, cbComming);
                if (pSink->nsDrainDeadline > 0)
                {
                    pSink->nsDrainStarted   = RTTimeNanoTS();
                    pSink->nsDrainDeadline += pSink->nsDrainStarted;
                    pSink->fStatus         |= AUDMIXSINK_STS_DRAINING;

                    /* Kick the AIO thread so it can keep pushing data till we're out of this
                       status. (The device's DMA timer won't kick it any more, so we must.) */
                    AudioMixerSinkSignalUpdateJob(pSink);
                }
                else
                {
                    LogFunc(("%s: No active streams, doing an immediate stop.\n", pSink->pszName));
                    PAUDMIXSTREAM pStream;
                    RTListForEach(&pSink->lstStreams, pStream, AUDMIXSTREAM, Node)
                    {
                        audioMixerStreamCtlInternal(pStream, PDMAUDIOSTREAMCMD_DISABLE);
                    }
                    audioMixerSinkReset(pSink);
                }
            }
            else
                AssertMsgFailed(("Already draining '%s': %s\n",
                                 pSink->pszName, dbgAudioMixerSinkStatusToStr(pSink->fStatus, szStatus)));
        }
        /*
         * Input sinks are stopped immediately.
         *
         * It's the guest giving order here and we can't force it to accept data that's
         * already in the buffer pipeline or anything.  So, there can be no draining here.
         */
        else
        {
            PAUDMIXSTREAM pStream;
            RTListForEach(&pSink->lstStreams, pStream, AUDMIXSTREAM, Node)
            {
                audioMixerStreamCtlInternal(pStream, PDMAUDIOSTREAMCMD_DISABLE);
            }
            audioMixerSinkReset(pSink);
        }
    }
    else
        LogFunc(("%s: Not running\n", pSink->pszName));

    LogRel2(("Audio Mixer: Started draining sink '%s': %s\n", pSink->pszName, dbgAudioMixerSinkStatusToStr(pSink->fStatus, szStatus)));
    RTCritSectLeave(&pSink->CritSect);
    return VINF_SUCCESS;
}



/**
 * Initializes a sink.
 *
 * @returns VBox status code.
 * @param   pSink           Sink to initialize.
 * @param   pMixer          Mixer the sink is assigned to.
 * @param   pszName         Name of the sink.
 * @param   enmDir          Direction of the sink.
 * @param   pDevIns         The device instance.
 */
static int audioMixerSinkInit(PAUDMIXSINK pSink, PAUDIOMIXER pMixer, const char *pszName, PDMAUDIODIR enmDir, PPDMDEVINS pDevIns)
{
    pSink->pszName = RTStrDup(pszName);
    if (!pSink->pszName)
        return VERR_NO_MEMORY;

    int rc = RTCritSectInit(&pSink->CritSect);
    if (RT_SUCCESS(rc))
    {
        pSink->uMagic   = AUDMIXSINK_MAGIC;
        pSink->pParent  = pMixer;
        pSink->enmDir   = enmDir;

        RTListInit(&pSink->lstStreams);

        /* Set initial volume to max. */
        pSink->Volume.fMuted = false;
        pSink->Volume.uLeft  = PDMAUDIO_VOLUME_MAX;
        pSink->Volume.uRight = PDMAUDIO_VOLUME_MAX;

        /* Ditto for the combined volume. */
        pSink->VolumeCombined.fMuted = false;
        pSink->VolumeCombined.uLeft  = PDMAUDIO_VOLUME_MAX;
        pSink->VolumeCombined.uRight = PDMAUDIO_VOLUME_MAX;

        /* AIO */
        AssertPtr(pDevIns);
        pSink->AIO.pDevIns     = pDevIns;
        pSink->AIO.hThread     = NIL_RTTHREAD;
        pSink->AIO.hEvent      = NIL_RTSEMEVENT;
        pSink->AIO.fStarted    = false;
        pSink->AIO.fShutdown   = false;
        pSink->AIO.cUpdateJobs = 0;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Destroys a mixer sink and removes it from the attached mixer (if any).
 *
 * @param   pSink       Mixer sink to destroy.
 * @param   pDevIns     The device instance that statistics are registered with.
 */
void AudioMixerSinkDestroy(PAUDMIXSINK pSink, PPDMDEVINS pDevIns)
{
    if (!pSink)
        return;

    /** @todo wrong critsect for audioMixerRemoveSinkInternal...   */
    int rc2 = RTCritSectEnter(&pSink->CritSect);
    AssertRC(rc2);

    if (pSink->pParent)
    {
        /* Save mixer pointer, as after audioMixerRemoveSinkInternal() the
         * pointer will be gone from the stream. */
        PAUDIOMIXER pMixer = pSink->pParent;
        AssertPtr(pMixer);
        Assert(pMixer->uMagic == AUDIOMIXER_MAGIC);

        audioMixerRemoveSinkInternal(pMixer, pSink);
    }

    rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    audioMixerSinkDestroyInternal(pSink, pDevIns);

    RTMemFree(pSink);
    pSink = NULL;
}

/**
 * Destroys a mixer sink.
 *
 * @param   pSink       Mixer sink to destroy.
 * @param   pDevIns     The device instance statistics are registered with.
 */
static void audioMixerSinkDestroyInternal(PAUDMIXSINK pSink, PPDMDEVINS pDevIns)
{
    AssertPtrReturnVoid(pSink);

    LogFunc(("%s\n", pSink->pszName));

    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    pSink->uMagic = AUDMIXSINK_MAGIC_DEAD;

    PAUDMIXSTREAM pStream, pStreamNext;
    RTListForEachSafe(&pSink->lstStreams, pStream, pStreamNext, AUDMIXSTREAM, Node)
    {
        audioMixerSinkRemoveStreamInternal(pSink, pStream);
        audioMixerStreamDestroyInternal(pStream, pDevIns, true /*fImmediate*/); /* (Unlike the other two, this frees the stream structure.) */
    }

    if (   pSink->pParent
        && pSink->pParent->fFlags & AUDMIXER_FLAGS_DEBUG)
    {
        AudioHlpFileDestroy(pSink->Dbg.pFile);
        pSink->Dbg.pFile = NULL;
    }

    char szPrefix[128];
    RTStrPrintf(szPrefix, sizeof(szPrefix), "MixerSink-%s/", pSink->pszName);
    PDMDevHlpSTAMDeregisterByPrefix(pDevIns, szPrefix);

    /* Shutdown the AIO thread if started: */
    ASMAtomicWriteBool(&pSink->AIO.fShutdown, true);
    if (pSink->AIO.hEvent != NIL_RTSEMEVENT)
    {
        int rc2 = RTSemEventSignal(pSink->AIO.hEvent);
        AssertRC(rc2);
    }
    if (pSink->AIO.hThread != NIL_RTTHREAD)
    {
        LogFlowFunc(("Waiting for AIO thread for %s...\n", pSink->pszName));
        int rc2 = RTThreadWait(pSink->AIO.hThread, RT_MS_30SEC, NULL);
        AssertRC(rc2);
        pSink->AIO.hThread = NIL_RTTHREAD;
    }
    if (pSink->AIO.hEvent != NIL_RTSEMEVENT)
    {
        int rc2 = RTSemEventDestroy(pSink->AIO.hEvent);
        AssertRC(rc2);
        pSink->AIO.hEvent = NIL_RTSEMEVENT;
    }

    RTStrFree(pSink->pszName);
    pSink->pszName = NULL;

    AudioMixBufDestroy(&pSink->MixBuf);
    RTCritSectDelete(&pSink->CritSect);
}

/**
 * Returns the amount of bytes ready to be read from a sink since the last call
 * to AudioMixerSinkUpdate().
 *
 * @returns Amount of bytes ready to be read from the sink.
 * @param   pSink               Sink to return number of available bytes for.
 */
uint32_t AudioMixerSinkGetReadable(PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, 0);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    AssertMsg(pSink->enmDir == PDMAUDIODIR_IN, ("%s: Can't read from a non-input sink\n", pSink->pszName));

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, 0);

    uint32_t cbReadable = 0;

    if (pSink->fStatus & AUDMIXSINK_STS_RUNNING)
    {
        uint32_t const cFrames = AudioMixBufUsed(&pSink->MixBuf);
        cbReadable = PDMAudioPropsFramesToBytes(&pSink->PCMProps, cFrames);
    }

    Log3Func(("[%s] cbReadable=%RU32\n", pSink->pszName, cbReadable));

    int rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    return cbReadable;
}

/**
 * Returns the amount of bytes ready to be written to a sink since the last call
 * to AudioMixerSinkUpdate().
 *
 * @returns Amount of bytes ready to be written to the sink.
 * @param   pSink               Sink to return number of available bytes for.
 */
uint32_t AudioMixerSinkGetWritable(PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, 0);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    AssertMsg(pSink->enmDir == PDMAUDIODIR_OUT, ("%s: Can't write to a non-output sink\n", pSink->pszName));

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, 0);

    uint32_t cbWritable = 0;

    if (    (pSink->fStatus & AUDMIXSINK_STS_RUNNING)
        && !(pSink->fStatus & AUDMIXSINK_STS_DRAINING))
    {
        cbWritable = AudioMixBufFreeBytes(&pSink->MixBuf);
    }

    Log3Func(("[%s] cbWritable=%RU32 (%RU64ms)\n",
              pSink->pszName, cbWritable, PDMAudioPropsBytesToMilli(&pSink->PCMProps, cbWritable)));

    int rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    return cbWritable;
}

/**
 * Returns the sink's mixing direction.
 *
 * @returns Mixing direction.
 * @param   pSink               Sink to return direction for.
 */
PDMAUDIODIR AudioMixerSinkGetDir(PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, PDMAUDIODIR_INVALID);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);

    /** @todo the sink direction should be static...    */
    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, PDMAUDIODIR_INVALID);

    PDMAUDIODIR const  enmDir = pSink->enmDir;

    int rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    return enmDir;
}

/**
 * Returns the current status of a mixer sink.
 *
 * @returns The sink's current status (AUDMIXSINK_STS_XXX).
 * @param   pSink               Mixer sink to return status for.
 */
uint32_t AudioMixerSinkGetStatus(PAUDMIXSINK pSink)
{
    if (!pSink)
        return AUDMIXSINK_STS_NONE;
    AssertPtr(pSink);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);

    int rc2 = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc2, AUDMIXSINK_STS_NONE);

    /* If the dirty flag is set, there is unprocessed data in the sink. */
    uint32_t const fStsSink = pSink->fStatus;

    rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    return fStsSink;
}

/**
 * Returns whether the sink is in an active state or not.
 *
 * @note The pending disable state also counts as active.
 *
 * @returns True if active, false if not.
 * @param   pSink               Sink to return active state for.
 */
bool AudioMixerSinkIsActive(PAUDMIXSINK pSink)
{
    if (!pSink)
        return false;
    AssertPtr(pSink);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);

    int rc2 = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc2, false);

    const bool fIsActive = pSink->fStatus & AUDMIXSINK_STS_RUNNING;
    /* Note: AUDMIXSINK_STS_PENDING_DISABLE implies AUDMIXSINK_STS_RUNNING. */

    Log3Func(("[%s] fActive=%RTbool\n", pSink->pszName, fIsActive));

    rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    return fIsActive;
}

/**
 * Removes a mixer stream from a mixer sink, internal version.
 *
 * @returns VBox status code.
 * @param   pSink               Sink to remove mixer stream from.
 * @param   pStream             Stream to remove.
 */
static int audioMixerSinkRemoveStreamInternal(PAUDMIXSINK pSink, PAUDMIXSTREAM pStream)
{
    AssertPtrReturn(pSink, VERR_INVALID_PARAMETER);
    if (pStream)
    { /* likely */ }
    else
        return VERR_NOT_FOUND;
    AssertMsgReturn(pStream->pSink == pSink, ("Stream '%s' is not part of sink '%s'\n",
                                              pStream->pszName, pSink->pszName), VERR_NOT_FOUND);

    LogFlowFunc(("[%s] (Stream = %s), cStreams=%RU8\n",
                 pSink->pszName, pStream->pStream->szName, pSink->cStreams));

    /* Remove stream from sink. */
    RTListNodeRemove(&pStream->Node);

    /* Set sink to NULL so that we know we're not part of any sink anymore. */
    pStream->pSink = NULL;

    return VINF_SUCCESS;
}

/**
 * Removes a mixer stream from a mixer sink.
 *
 * @param   pSink               Sink to remove mixer stream from.
 * @param   pStream             Stream to remove.
 */
void AudioMixerSinkRemoveStream(PAUDMIXSINK pSink, PAUDMIXSTREAM pStream)
{
    AssertPtr(pSink);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    if (pStream)
        Assert(pStream->uMagic == AUDMIXSTREAM_MAGIC);

    int rc2 = RTCritSectEnter(&pSink->CritSect);
    AssertRC(rc2);

    rc2 = audioMixerSinkRemoveStreamInternal(pSink, pStream);
    if (RT_SUCCESS(rc2))
    {
        Assert(pSink->cStreams);
        pSink->cStreams--;
    }

    rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);
}

/**
 * Removes all attached streams from a given sink.
 *
 * @param pSink                 Sink to remove attached streams from.
 */
static void audioMixerSinkRemoveAllStreamsInternal(PAUDMIXSINK pSink)
{
    if (!pSink)
        return;

    LogFunc(("%s\n", pSink->pszName));

    PAUDMIXSTREAM pStream, pStreamNext;
    RTListForEachSafe(&pSink->lstStreams, pStream, pStreamNext, AUDMIXSTREAM, Node)
        audioMixerSinkRemoveStreamInternal(pSink, pStream);
}

/**
 * Removes all attached streams from a given sink.
 *
 * @param pSink                 Sink to remove attached streams from.
 */
void AudioMixerSinkRemoveAllStreams(PAUDMIXSINK pSink)
{
    if (!pSink)
        return;

    int rc2 = RTCritSectEnter(&pSink->CritSect);
    AssertRC(rc2);

    audioMixerSinkRemoveAllStreamsInternal(pSink);

    pSink->cStreams = 0;

    rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);
}

/**
 * Resets the sink's state.
 *
 * @param   pSink               Sink to reset.
 */
static void audioMixerSinkReset(PAUDMIXSINK pSink)
{
    if (!pSink)
        return;

    LogFunc(("[%s]\n", pSink->pszName));

    AudioMixBufDrop(&pSink->MixBuf);

    /* Update last updated timestamp. */
    pSink->tsLastUpdatedMs = 0;

    /* Reset status. */
    pSink->fStatus = AUDMIXSINK_STS_NONE;
}

/**
 * Resets a sink. This will immediately stop all processing.
 *
 * @param pSink                 Sink to reset.
 */
void AudioMixerSinkReset(PAUDMIXSINK pSink)
{
    if (!pSink)
        return;

    int rc2 = RTCritSectEnter(&pSink->CritSect);
    AssertRC(rc2);

    LogFlowFunc(("[%s]\n", pSink->pszName));

    audioMixerSinkReset(pSink);

    rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);
}

/**
 * Sets the audio format of a mixer sink.
 *
 * @returns VBox status code.
 * @param   pSink   The sink to set audio format for.
 * @param   pProps  The properties of the new audio format.
 */
int AudioMixerSinkSetFormat(PAUDMIXSINK pSink, PCPDMAUDIOPCMPROPS pProps)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    AssertReturn(pSink->uMagic == AUDMIXSINK_MAGIC, VERR_INVALID_MAGIC);
    AssertPtrReturn(pProps, VERR_INVALID_POINTER);
    AssertReturn(AudioHlpPcmPropsAreValid(pProps), VERR_INVALID_PARAMETER);

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, rc);

    /*
     * Nothing to do here unless the format changed.
     */
    if (!PDMAudioPropsAreEqual(&pSink->PCMProps, pProps))
    {
#ifdef LOG_ENABLED
        char szTmp[PDMAUDIOPROPSTOSTRING_MAX];
#endif
        if (PDMAudioPropsHz(&pSink->PCMProps) != 0)
            LogFlowFunc(("[%s] Old format: %s\n", pSink->pszName, PDMAudioPropsToString(&pSink->PCMProps, szTmp, sizeof(szTmp)) ));

        pSink->PCMProps = *pProps;
        LogFlowFunc(("[%s] New format: %s\n", pSink->pszName, PDMAudioPropsToString(&pSink->PCMProps, szTmp, sizeof(szTmp)) ));

        /*
         * Also update the sink's mixing buffer format.
         */
        AudioMixBufDestroy(&pSink->MixBuf);

        /** @todo r=bird: Make sure we've got more room here than what's expected to
         *        be moved in one guest DMA period. */
        rc = AudioMixBufInit(&pSink->MixBuf, pSink->pszName, &pSink->PCMProps,
                             PDMAudioPropsMilliToFrames(&pSink->PCMProps, 100 /*ms*/)); /** @todo Make this configurable? */
        if (RT_SUCCESS(rc))
        {
            /*
             * Input sinks must init their (mostly dummy) peek state.
             */
            if (pSink->enmDir == PDMAUDIODIR_IN)
                rc = AudioMixBufInitPeekState(&pSink->MixBuf, &pSink->In.State, &pSink->PCMProps);
            else
                rc = AudioMixBufInitWriteState(&pSink->MixBuf, &pSink->Out.State, &pSink->PCMProps);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Re-initialize the peek/write states as the frequency, channel count
                 * and other things may have changed now.
                 */
                PAUDMIXSTREAM pMixStream;
                if (pSink->enmDir == PDMAUDIODIR_IN)
                {
                    RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
                    {
                        int rc2 = AudioMixBufInitWriteState(&pSink->MixBuf, &pMixStream->WriteState, &pMixStream->pStream->Props);
                        /** @todo remember this. */
                        AssertLogRelRC(rc2);
                    }
                }
                else
                {
                    RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
                    {
                        int rc2 = AudioMixBufInitPeekState(&pSink->MixBuf, &pMixStream->PeekState, &pMixStream->pStream->Props);
                        /** @todo remember this. */
                        AssertLogRelRC(rc2);
                    }
                }

                /*
                 * Debug.
                 */
                if (!(pSink->pParent->fFlags & AUDMIXER_FLAGS_DEBUG))
                { /* likely */ }
                else
                {
                    AudioHlpFileClose(pSink->Dbg.pFile);

                    char szName[64];
                    RTStrPrintf(szName, sizeof(szName), "MixerSink-%s", pSink->pszName);
                    AudioHlpFileCreateAndOpen(&pSink->Dbg.pFile, NULL /*pszDir - use temp dir*/, szName,
                                              0 /*iInstance*/, &pSink->PCMProps);
                }
            }
            else
                LogFunc(("%s failed: %Rrc\n",
                         pSink->enmDir == PDMAUDIODIR_IN ? "AudioMixBufInitPeekState" : "AudioMixBufInitWriteState", rc));
        }
        else
            LogFunc(("AudioMixBufInit failed: %Rrc\n", rc));
    }

    RTCritSectLeave(&pSink->CritSect);
    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sets the volume of an individual sink.
 *
 * @returns VBox status code.
 * @param   pSink               Sink to set volume for.
 * @param   pVol                Volume to set.
 */
int AudioMixerSinkSetVolume(PAUDMIXSINK pSink, PPDMAUDIOVOLUME pVol)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    AssertPtrReturn(pVol,  VERR_INVALID_POINTER);

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, rc);

    memcpy(&pSink->Volume, pVol, sizeof(PDMAUDIOVOLUME));

    LogRel2(("Audio Mixer: Setting volume of sink '%s' to %RU8/%RU8 (%s)\n",
             pSink->pszName, pVol->uLeft, pVol->uRight, pVol->fMuted ? "Muted" : "Unmuted"));

    AssertPtr(pSink->pParent);
    rc = audioMixerSinkUpdateVolume(pSink, &pSink->pParent->VolMaster);

    int rc2 = RTCritSectLeave(&pSink->CritSect);
    AssertRC(rc2);

    return rc;
}


/**
 * Helper for audioMixerSinkUpdateInput that determins now many frames it can
 * transfer from the drivers and into the sink's mixer buffer.
 *
 * This also updates the mixer stream status, which may involve stream re-inits.
 *
 * @returns Number of frames.
 * @param   pSink               The sink.
 * @param   pcReadableStreams   Where to return the number of readable streams.
 */
static uint32_t audioMixerSinkUpdateInputCalcFramesToTransfer(PAUDMIXSINK pSink, uint32_t *pcReadableStreams)
{
    uint32_t      cFramesToRead    = AudioMixBufFree(&pSink->MixBuf);
    uint32_t      cReadableStreams = 0;
    PAUDMIXSTREAM pMixStream;
    RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
    {
        int rc2 = audioMixerStreamUpdateStatus(pMixStream);
        AssertRC(rc2);

        if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_CAN_READ)
        {
            PPDMIAUDIOCONNECTOR const pIConnector = pMixStream->pConn;
            PPDMAUDIOSTREAM const     pStream     = pMixStream->pStream;
            pIConnector->pfnStreamIterate(pIConnector, pStream);

            uint32_t const cbReadable = pIConnector->pfnStreamGetReadable(pIConnector, pStream);
            uint32_t cFrames = PDMAudioPropsBytesToFrames(&pStream->Props, cbReadable);
            pMixStream->cFramesLastAvail = cFrames;
            if (PDMAudioPropsHz(&pStream->Props) == PDMAudioPropsHz(&pSink->MixBuf.Props))
            { /* likely */ }
            else
            {
                cFrames = cFrames * PDMAudioPropsHz(&pSink->MixBuf.Props) / PDMAudioPropsHz(&pStream->Props);
                cFrames = cFrames > 2 ? cFrames - 2 : 0; /* rounding safety fudge */
            }
            if (cFramesToRead > cFrames && !pMixStream->fUnreliable)
            {
                Log4Func(("%s: cFramesToRead %u -> %u; %s (%u bytes readable)\n",
                          pSink->pszName, cFramesToRead, cFrames, pMixStream->pszName, cbReadable));
                cFramesToRead = cFrames;
            }
            cReadableStreams++;
        }
    }

    *pcReadableStreams = cReadableStreams;
    return cFramesToRead;
}


/**
 * Updates an input mixer sink.
 *
 * @returns VBox status code.
 * @param   pSink       Mixer sink to update.
 * @param   cbDmaBuf    The number of bytes in the DMA buffer.  For detecting
 *                      underruns.  Zero if we don't know.
 * @param   cbDmaPeriod The minimum number of bytes required for reliable DMA
 *                      operation.  Zero if we don't know.
 */
static int audioMixerSinkUpdateInput(PAUDMIXSINK pSink, uint32_t cbDmaBuf, uint32_t cbDmaPeriod)
{
    PAUDMIXSTREAM pMixStream;
    Assert(!(pSink->fStatus & AUDMIXSINK_STS_DRAINED_MIXBUF)); /* (can't drain input sink) */

    /*
     * Iterate, update status and check each mixing sink stream for how much
     * we can transfer.
     *
     * We're currently using the minimum size of all streams, however this
     * isn't a smart approach as it means one disfunctional stream can block
     * working ones.  So, if we end up with zero frames and a full mixer
     * buffer we'll disregard the stream that accept the smallest amount and
     * try again.
     */
    uint32_t cReadableStreams  = 0;
    uint32_t cFramesToXfer     = audioMixerSinkUpdateInputCalcFramesToTransfer(pSink, &cReadableStreams);
    if (   cFramesToXfer != 0
        || cReadableStreams <= 1
        || cbDmaPeriod == 0 /* Insufficient info to decide. The update function will call us again, at least for HDA. */
        || cbDmaBuf + PDMAudioPropsFramesToBytes(&pSink->PCMProps, AudioMixBufUsed(&pSink->MixBuf)) >= cbDmaPeriod)
        Log3Func(("%s: cFreeFrames=%#x cFramesToXfer=%#x cReadableStreams=%#x\n", pSink->pszName,
                  AudioMixBufFree(&pSink->MixBuf), cFramesToXfer, cReadableStreams));
    else
    {
        Log3Func(("%s: MixBuf is underrunning but one or more streams only provides zero frames.  Try disregarding those...\n", pSink->pszName));
        uint32_t        cReliableStreams  = 0;
        uint32_t        cMarkedUnreliable = 0;
        PAUDMIXSTREAM   pMixStreamMin     = NULL;
        RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
        {
            if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_CAN_READ)
            {
                if (!pMixStream->fUnreliable)
                {
                    if (pMixStream->cFramesLastAvail == 0)
                    {
                        cMarkedUnreliable++;
                        pMixStream->fUnreliable = true;
                        Log3Func(("%s: Marked '%s' as unreliable.\n", pSink->pszName, pMixStream->pszName));
                        pMixStreamMin = pMixStream;
                    }
                    else
                    {
                        if (!pMixStreamMin || pMixStream->cFramesLastAvail < pMixStreamMin->cFramesLastAvail)
                            pMixStreamMin = pMixStream;
                        cReliableStreams++;
                    }
                }
            }
        }

        if (cMarkedUnreliable == 0 && cReliableStreams > 1 && pMixStreamMin != NULL)
        {
            cReliableStreams--;
            cMarkedUnreliable++;
            pMixStreamMin->fUnreliable = true;
            Log3Func(("%s: Marked '%s' as unreliable (%u frames).\n",
                      pSink->pszName, pMixStreamMin->pszName, pMixStreamMin->cFramesLastAvail));
        }

        if (cMarkedUnreliable > 0)
        {
            cReadableStreams = 0;
            cFramesToXfer = audioMixerSinkUpdateInputCalcFramesToTransfer(pSink, &cReadableStreams);
        }

        Log3Func(("%s: cFreeFrames=%#x cFramesToXfer=%#x cReadableStreams=%#x cMarkedUnreliable=%#x cReliableStreams=%#x\n",
                  pSink->pszName, AudioMixBufFree(&pSink->MixBuf), cFramesToXfer,
                  cReadableStreams, cMarkedUnreliable, cReliableStreams));
    }

    if (cReadableStreams > 0)
    {
        if (cFramesToXfer > 0)
        {
/*#define ELECTRIC_INPUT_BUFFER*/ /* if buffer code is misbehaving, enable this to catch overflows. */
#ifndef ELECTRIC_INPUT_BUFFER
            union
            {
                uint8_t  ab[8192];
                uint64_t au64[8192 / sizeof(uint64_t)]; /* Use uint64_t to ensure good alignment. */
            } Buf;
            void * const   pvBuf = &Buf;
            uint32_t const cbBuf = sizeof(Buf);
#else
            uint32_t const cbBuf = 0x2000 - 16;
            void * const   pvBuf = RTMemEfAlloc(cbBuf, RTMEM_TAG, RT_SRC_POS);
#endif

            /*
             * For each of the enabled streams, read cFramesToXfer frames worth
             * of samples from them and merge that into the mixing buffer.
             */
            bool fAssign = true;
            RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
            {
                if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_CAN_READ)
                {
                    PPDMIAUDIOCONNECTOR const pIConnector = pMixStream->pConn;
                    PPDMAUDIOSTREAM const     pStream     = pMixStream->pStream;

                    /* Calculate how many bytes we should read from this stream. */
                    bool const     fResampleSrc = PDMAudioPropsHz(&pStream->Props) != PDMAudioPropsHz(&pSink->MixBuf.Props);
                    uint32_t const cbSrcToXfer  = !fResampleSrc
                                                ? PDMAudioPropsFramesToBytes(&pStream->Props, cFramesToXfer)
                                                : PDMAudioPropsFramesToBytes(&pStream->Props, /** @todo check rounding errors here... */
                                                                             cFramesToXfer * PDMAudioPropsHz(&pSink->MixBuf.Props)
                                                                             / PDMAudioPropsHz(&pStream->Props));

                    /* Do the reading. */
                    uint32_t offSrc      = 0;
                    uint32_t offDstFrame = 0;
                    do
                    {
                        /*
                         * Read a chunk from the backend.
                         */
                        uint32_t const cbSrcToRead = RT_MIN(cbBuf, cbSrcToXfer - offSrc);
                        uint32_t       cbSrcRead   = 0;
                        if (cbSrcToRead > 0)
                        {
                            int rc2 = pIConnector->pfnStreamCapture(pIConnector, pStream, pvBuf, cbSrcToRead, &cbSrcRead);
                            Log3Func(("%s: %#x L %#x => %#x bytes; rc2=%Rrc %s\n",
                                      pSink->pszName, offSrc, cbSrcToRead, cbSrcRead, rc2, pMixStream->pszName));

                            if (RT_SUCCESS(rc2))
                                AssertLogRelMsg(cbSrcRead == cbSrcToRead || pMixStream->fUnreliable,
                                                ("cbSrcRead=%#x cbSrcToRead=%#x - (sink '%s')\n",
                                                 cbSrcRead, cbSrcToRead, pSink->pszName));
                            else if (rc2 == VERR_AUDIO_STREAM_NOT_READY)
                            {
                                LogRel2(("Audio Mixer: '%s' (sink '%s'): Stream not ready - skipping.\n",
                                         pMixStream->pszName, pSink->pszName)); /* must've changed status, stop processing */
                                break;
                            }
                            else
                            {
                                Assert(rc2 != VERR_BUFFER_OVERFLOW);
                                LogRel2(("Audio Mixer: Reading from mixer stream '%s' (sink '%s') failed, rc=%Rrc\n",
                                         pMixStream->pszName, pSink->pszName, rc2));
                                break;
                            }
                            offSrc += cbSrcRead;
                        }
                        else
                            Assert(fResampleSrc); /** @todo test this case */

                        /*
                         * Assign or blend it into the mixer buffer.
                         */
                        uint32_t cFramesDstTransferred = 0;
                        if (fAssign)
                        {
                            /** @todo could complicate this by detecting silence here too and stay in
                             *        assign mode till we get a stream with non-silence... */
                            AudioMixBufWrite(&pSink->MixBuf, &pMixStream->WriteState, pvBuf, cbSrcRead,
                                             offDstFrame, cFramesToXfer - offDstFrame, &cFramesDstTransferred);
                        }
                        /* We don't need to blend silence buffers.  For simplicity, always blend
                           when we're resampling (for rounding). */
                        else if (fResampleSrc || !PDMAudioPropsIsBufferSilence(&pStream->Props, pvBuf, cbSrcRead))
                        {
                            AudioMixBufBlend(&pSink->MixBuf, &pMixStream->WriteState, pvBuf, cbSrcRead,
                                             offDstFrame, cFramesToXfer - offDstFrame, &cFramesDstTransferred);
                        }
                        else
                        {
                            cFramesDstTransferred = PDMAudioPropsBytesToFrames(&pStream->Props, cbSrcRead);
                            AudioMixBufBlendGap(&pSink->MixBuf, &pMixStream->WriteState, cFramesDstTransferred);
                        }
                        AssertBreak(cFramesDstTransferred > 0);

                        /* Advance. */
                        offDstFrame += cFramesDstTransferred;
                    } while (offDstFrame < cFramesToXfer);

                    /*
                     * In case the first stream is misbehaving, make sure we written the entire area.
                     */
                    if (offDstFrame >= cFramesToXfer)
                    { /* likely */ }
                    else if (fAssign)
                        AudioMixBufSilence(&pSink->MixBuf, &pMixStream->WriteState, offDstFrame, cFramesToXfer - offDstFrame);
                    else
                        AudioMixBufBlendGap(&pSink->MixBuf, &pMixStream->WriteState, cFramesToXfer - offDstFrame);
                    fAssign = false;
                }
            }

            /*
             * Commit the buffer area we've written and blended into.
             */
            AudioMixBufCommit(&pSink->MixBuf, cFramesToXfer);

#ifdef ELECTRIC_INPUT_BUFFER
            RTMemEfFree(pvBuf, RT_SRC_POS);
#endif
        }

        /*
         * Set the dirty flag for what it's worth.
         */
        pSink->fStatus |= AUDMIXSINK_STS_DIRTY;
    }
    else
    {
        /*
         * No readable stream. Clear the dirty flag if empty (pointless flag).
         */
        if (!AudioMixBufUsed(&pSink->MixBuf))
            pSink->fStatus &= ~AUDMIXSINK_STS_DIRTY;
    }

    /* Update last updated timestamp. */
    pSink->tsLastUpdatedMs = RTTimeMilliTS();

    return VINF_SUCCESS;
}


/**
 * Helper for audioMixerSinkUpdateOutput that determins now many frames it
 * can transfer from the sink's mixer buffer and to the drivers.
 *
 * This also updates the mixer stream status, which may involve stream re-inits.
 *
 * @returns Number of frames.
 * @param   pSink               The sink.
 * @param   pcWritableStreams   Where to return the number of writable streams.
 */
static uint32_t audioMixerSinkUpdateOutputCalcFramesToRead(PAUDMIXSINK pSink, uint32_t *pcWritableStreams)
{
    uint32_t      cFramesToRead    = AudioMixBufUsed(&pSink->MixBuf); /* (to read from the mixing buffer) */
    uint32_t      cWritableStreams = 0;
    PAUDMIXSTREAM pMixStream;
    RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
    {
#if 0 /** @todo this conceptually makes sense, but may mess up the pending-disable logic ... */
        if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_ENABLED)
            pConn->pfnStreamIterate(pConn, pStream);
#endif

        int rc2 = audioMixerStreamUpdateStatus(pMixStream);
        AssertRC(rc2);

        if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_CAN_WRITE)
        {
            uint32_t const cbWritable = pMixStream->pConn->pfnStreamGetWritable(pMixStream->pConn, pMixStream->pStream);
            uint32_t cFrames = PDMAudioPropsBytesToFrames(&pMixStream->pStream->Props, cbWritable);
            pMixStream->cFramesLastAvail = cFrames;
            if (PDMAudioPropsHz(&pMixStream->pStream->Props) == PDMAudioPropsHz(&pSink->MixBuf.Props))
            { /* likely */ }
            else
            {
                cFrames = cFrames * PDMAudioPropsHz(&pSink->MixBuf.Props) / PDMAudioPropsHz(&pMixStream->pStream->Props);
                cFrames = cFrames > 2 ? cFrames - 2 : 0; /* rounding safety fudge */
            }
            if (cFramesToRead > cFrames && !pMixStream->fUnreliable)
            {
                Log4Func(("%s: cFramesToRead %u -> %u; %s (%u bytes writable)\n",
                          pSink->pszName, cFramesToRead, cFrames, pMixStream->pszName, cbWritable));
                cFramesToRead = cFrames;
            }
            cWritableStreams++;
        }
    }

    *pcWritableStreams = cWritableStreams;
    return cFramesToRead;
}


/**
 * Updates an output mixer sink.
 *
 * @returns VBox status code.
 * @param   pSink               Mixer sink to update.
 */
static int audioMixerSinkUpdateOutput(PAUDMIXSINK pSink)
{
    PAUDMIXSTREAM pMixStream;
    Assert(!(pSink->fStatus & AUDMIXSINK_STS_DRAINED_MIXBUF) || AudioMixBufUsed(&pSink->MixBuf) == 0);

    /*
     * Update each mixing sink stream's status and check how much we can
     * write into them.
     *
     * We're currently using the minimum size of all streams, however this
     * isn't a smart approach as it means one disfunctional stream can block
     * working ones.  So, if we end up with zero frames and a full mixer
     * buffer we'll disregard the stream that accept the smallest amount and
     * try again.
     */
    uint32_t cWritableStreams  = 0;
    uint32_t cFramesToRead     = audioMixerSinkUpdateOutputCalcFramesToRead(pSink, &cWritableStreams);
    if (   cFramesToRead != 0
        || cWritableStreams <= 1
        || AudioMixBufFree(&pSink->MixBuf) > 2)
        Log3Func(("%s: cLiveFrames=%#x cFramesToRead=%#x cWritableStreams=%#x\n", pSink->pszName,
                  AudioMixBufUsed(&pSink->MixBuf), cFramesToRead, cWritableStreams));
    else
    {
        Log3Func(("%s: MixBuf is full but one or more streams only want zero frames.  Try disregarding those...\n", pSink->pszName));
        uint32_t        cReliableStreams  = 0;
        uint32_t        cMarkedUnreliable = 0;
        PAUDMIXSTREAM   pMixStreamMin     = NULL;
        RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
        {
            if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_CAN_WRITE)
            {
                if (!pMixStream->fUnreliable)
                {
                    if (pMixStream->cFramesLastAvail == 0)
                    {
                        cMarkedUnreliable++;
                        pMixStream->fUnreliable = true;
                        Log3Func(("%s: Marked '%s' as unreliable.\n", pSink->pszName, pMixStream->pszName));
                        pMixStreamMin = pMixStream;
                    }
                    else
                    {
                        if (!pMixStreamMin || pMixStream->cFramesLastAvail < pMixStreamMin->cFramesLastAvail)
                            pMixStreamMin = pMixStream;
                        cReliableStreams++;
                    }
                }
            }
        }

        if (cMarkedUnreliable == 0 && cReliableStreams > 1 && pMixStreamMin != NULL)
        {
            cReliableStreams--;
            cMarkedUnreliable++;
            pMixStreamMin->fUnreliable = true;
            Log3Func(("%s: Marked '%s' as unreliable (%u frames).\n",
                      pSink->pszName, pMixStreamMin->pszName, pMixStreamMin->cFramesLastAvail));
        }

        if (cMarkedUnreliable > 0)
        {
            cWritableStreams = 0;
            cFramesToRead = audioMixerSinkUpdateOutputCalcFramesToRead(pSink, &cWritableStreams);
        }

        Log3Func(("%s: cLiveFrames=%#x cFramesToRead=%#x cWritableStreams=%#x cMarkedUnreliable=%#x cReliableStreams=%#x\n",
                  pSink->pszName, AudioMixBufUsed(&pSink->MixBuf), cFramesToRead,
                  cWritableStreams, cMarkedUnreliable, cReliableStreams));
    }

    if (cWritableStreams > 0)
    {
        if (cFramesToRead > 0)
        {
            /*
             * For each of the enabled streams, convert cFramesToRead frames from
             * the mixing buffer and write that to the downstream driver.
             */
            RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
            {
                if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_CAN_WRITE)
                {
                    uint32_t offSrcFrame = 0;
                    do
                    {
                        /* Convert a chunk from the mixer buffer.  */
/*#define ELECTRIC_PEEK_BUFFER*/ /* if buffer code is misbehaving, enable this to catch overflows. */
#ifndef ELECTRIC_PEEK_BUFFER
                        union
                        {
                            uint8_t  ab[8192];
                            uint64_t au64[8192 / sizeof(uint64_t)]; /* Use uint64_t to ensure good alignment. */
                        } Buf;
                        void * const   pvBuf = &Buf;
                        uint32_t const cbBuf = sizeof(Buf);
#else
                        uint32_t const cbBuf = 0x2000 - 16;
                        void * const   pvBuf = RTMemEfAlloc(cbBuf, RTMEM_TAG, RT_SRC_POS);
#endif
                        uint32_t cbDstPeeked      = cbBuf;
                        uint32_t cSrcFramesPeeked = cFramesToRead - offSrcFrame;
                        AudioMixBufPeek(&pSink->MixBuf, offSrcFrame, cSrcFramesPeeked, &cSrcFramesPeeked,
                                        &pMixStream->PeekState, pvBuf, cbBuf, &cbDstPeeked);
                        offSrcFrame += cSrcFramesPeeked;

                        /* Write it to the backend.  Since've checked that there is buffer
                           space available, this should always write the whole buffer unless
                           it's an unreliable stream. */
                        uint32_t cbDstWritten = 0;
                        int rc2 = pMixStream->pConn->pfnStreamPlay(pMixStream->pConn, pMixStream->pStream,
                                                                   pvBuf, cbDstPeeked, &cbDstWritten);
                        Log3Func(("%s: %#x L %#x => %#x bytes; wrote %#x rc2=%Rrc %s\n", pSink->pszName, offSrcFrame,
                                  cSrcFramesPeeked - cSrcFramesPeeked, cbDstPeeked, cbDstWritten, rc2, pMixStream->pszName));
#ifdef ELECTRIC_PEEK_BUFFER
                        RTMemEfFree(pvBuf, RT_SRC_POS);
#endif
                        if (RT_SUCCESS(rc2))
                            AssertLogRelMsg(cbDstWritten == cbDstPeeked || pMixStream->fUnreliable,
                                            ("cbDstWritten=%#x cbDstPeeked=%#x - (sink '%s')\n",
                                             cbDstWritten, cbDstPeeked, pSink->pszName));
                        else if (rc2 == VERR_AUDIO_STREAM_NOT_READY)
                        {
                            LogRel2(("Audio Mixer: '%s' (sink '%s'): Stream not ready - skipping.\n",
                                     pMixStream->pszName, pSink->pszName));
                            break; /* must've changed status, stop processing */
                        }
                        else
                        {
                            Assert(rc2 != VERR_BUFFER_OVERFLOW);
                            LogRel2(("Audio Mixer: Writing to mixer stream '%s' (sink '%s') failed, rc=%Rrc\n",
                                     pMixStream->pszName, pSink->pszName, rc2));
                            break;
                        }
                    } while (offSrcFrame < cFramesToRead);
                }
            }

            AudioMixBufAdvance(&pSink->MixBuf, cFramesToRead);
        }

        /*
         * Update the dirty flag for what it's worth.
         */
        if (AudioMixBufUsed(&pSink->MixBuf) > 0)
            pSink->fStatus |= AUDMIXSINK_STS_DIRTY;
        else
            pSink->fStatus &= ~AUDMIXSINK_STS_DIRTY;
    }
    else
    {
        /*
         * If no writable streams, just drop the mixer buffer content.
         */
        AudioMixBufDrop(&pSink->MixBuf);
        pSink->fStatus &= ~AUDMIXSINK_STS_DIRTY;
    }

    /*
     * Iterate buffers.
     */
    RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
    {
        if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_ENABLED)
            pMixStream->pConn->pfnStreamIterate(pMixStream->pConn, pMixStream->pStream);
    }

    /* Update last updated timestamp. */
    uint64_t const nsNow   = RTTimeNanoTS();
    pSink->tsLastUpdatedMs = nsNow / RT_NS_1MS;

    /*
     * Deal with pending disable.
     * We reset the sink when all streams have been disabled.
     */
    if (!(pSink->fStatus & AUDMIXSINK_STS_DRAINING))
    { /* likely, till we get to the end */ }
    else if (nsNow <= pSink->nsDrainDeadline)
    {
        /* Have we drained the mixbuf now?  If so, update status and send drain
           command to streams.  (As mentioned elsewhere we don't want to confuse
           driver code by sending drain command while there is still data to write.) */
        Assert((pSink->fStatus & AUDMIXSINK_STS_DIRTY) == (AudioMixBufUsed(&pSink->MixBuf) > 0 ? AUDMIXSINK_STS_DIRTY : 0));
        if ((pSink->fStatus & (AUDMIXSINK_STS_DRAINED_MIXBUF | AUDMIXSINK_STS_DIRTY)) == 0)
        {
            LogFunc(("Sink '%s': Setting AUDMIXSINK_STS_DRAINED_MIXBUF and sending drain command to streams (after %RU64 ns).\n",
                     pSink->pszName, nsNow - pSink->nsDrainStarted));
            pSink->fStatus |= AUDMIXSINK_STS_DRAINED_MIXBUF;

            RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
            {
                pMixStream->pConn->pfnStreamControl(pMixStream->pConn, pMixStream->pStream, PDMAUDIOSTREAMCMD_DRAIN);
            }
        }

        /* Check if all streams has stopped, and if so we stop the sink. */
        uint32_t const  cStreams         = pSink->cStreams;
        uint32_t        cStreamsDisabled = pSink->cStreams;
        RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
        {
            if (pMixStream->fStatus & AUDMIXSTREAM_STATUS_ENABLED)
            {
                PDMAUDIOSTREAMSTATE const enmState = pMixStream->pConn->pfnStreamGetState(pMixStream->pConn, pMixStream->pStream);
                if (enmState >= PDMAUDIOSTREAMSTATE_ENABLED)
                    cStreamsDisabled--;
            }
        }

        if (cStreamsDisabled != cStreams)
            Log3Func(("Sink '%s': %u out of %u streams disabled (after %RU64 ns).\n",
                      pSink->pszName, cStreamsDisabled, cStreams, nsNow - pSink->nsDrainStarted));
        else
        {
            LogFunc(("Sink '%s': All %u streams disabled. Drain done after %RU64 ns.\n",
                     pSink->pszName, cStreamsDisabled, nsNow - pSink->nsDrainStarted));
            audioMixerSinkReset(pSink); /* clears the status */
        }
    }
    else
    {
        /* Draining timed out. Just do an instant stop. */
        LogFunc(("Sink '%s': pending disable timed out after %RU64 ns!\n", pSink->pszName, nsNow - pSink->nsDrainStarted));
        RTListForEach(&pSink->lstStreams, pMixStream, AUDMIXSTREAM, Node)
        {
            pMixStream->pConn->pfnStreamControl(pMixStream->pConn, pMixStream->pStream, PDMAUDIOSTREAMCMD_DISABLE);
        }
        audioMixerSinkReset(pSink); /* clears the status */
    }

    return VINF_SUCCESS;
}

/**
 * Updates (invalidates) a mixer sink.
 *
 * @returns VBox status code.
 * @param   pSink           Mixer sink to update.
 * @param   cbDmaUsed       The DMA buffer fill for input stream, ignored for
 *                          output sinks.
 * @param   cbDmaPeriod     The DMA period in bytes for input stream, ignored
 *                          for output sinks.
 */
int AudioMixerSinkUpdate(PAUDMIXSINK pSink, uint32_t cbDmaUsed, uint32_t cbDmaPeriod)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, rc);

#ifdef LOG_ENABLED
    char szStatus[AUDIOMIXERSINK_STATUS_STR_MAX];
#endif
    Log3Func(("[%s] fStatus=%s\n", pSink->pszName, dbgAudioMixerSinkStatusToStr(pSink->fStatus, szStatus)));

    /* Only process running sinks. */
    if (pSink->fStatus & AUDMIXSINK_STS_RUNNING)
    {
        /* Do separate processing for input and output sinks. */
        if (pSink->enmDir == PDMAUDIODIR_OUT)
            rc = audioMixerSinkUpdateOutput(pSink);
        else if (pSink->enmDir == PDMAUDIODIR_IN)
            rc = audioMixerSinkUpdateInput(pSink, cbDmaUsed, cbDmaPeriod);
        else
            AssertFailedStmt(rc = VERR_INTERNAL_ERROR_3);
    }
    else
        rc = VINF_SUCCESS; /* disabled */

    RTCritSectLeave(&pSink->CritSect);
    return rc;
}


/**
 * @callback_method_impl{FNRTTHREAD, Audio Mixer Sink asynchronous I/O thread}
 */
static DECLCALLBACK(int) audioMixerSinkAsyncIoThread(RTTHREAD hThreadSelf, void *pvUser)
{
    PAUDMIXSINK pSink = (PAUDMIXSINK)pvUser;
    AssertPtr(pSink);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    RT_NOREF(hThreadSelf);

    /*
     * The run loop.
     */
    LogFlowFunc(("%s: Entering run loop...\n", pSink->pszName));
    while (!pSink->AIO.fShutdown)
    {
        RTMSINTERVAL cMsSleep = RT_INDEFINITE_WAIT;

        RTCritSectEnter(&pSink->CritSect);
        if (pSink->fStatus & (AUDMIXSINK_STS_RUNNING | AUDMIXSINK_STS_DRAINING))
        {
            /*
             * Before doing jobs, always update input sinks.
             */
            if (pSink->enmDir == PDMAUDIODIR_IN)
                audioMixerSinkUpdateInput(pSink, 0 /*cbDmaUsed*/, 0 /*cbDmaPeriod*/);

            /*
             * Do the device specific updating.
             */
            uintptr_t const cUpdateJobs = RT_MIN(pSink->AIO.cUpdateJobs, RT_ELEMENTS(pSink->AIO.aUpdateJobs));
            for (uintptr_t iJob = 0; iJob < cUpdateJobs; iJob++)
                pSink->AIO.aUpdateJobs[iJob].pfnUpdate(pSink->AIO.pDevIns, pSink, pSink->AIO.aUpdateJobs[iJob].pvUser);

            /*
             * Update output sinks after the updating.
             */
            if (pSink->enmDir == PDMAUDIODIR_OUT)
                audioMixerSinkUpdateOutput(pSink);

            /*
             * If we're in draining mode, we use the smallest typical interval of the
             * jobs for the next wait as we're unlikly to be woken up again by any
             * DMA timer as it has normally stopped running at this point.
             */
            if (!(pSink->fStatus & AUDMIXSINK_STS_DRAINING))
            { /* likely */ }
            else
            {
                /** @todo Also do some kind of timeout here and do a forced stream disable w/o
                 *        any draining if we exceed it. */
                cMsSleep = pSink->AIO.cMsMinTypicalInterval;
            }

        }
        RTCritSectLeave(&pSink->CritSect);

        /*
         * Now block till we're signalled or
         */
        if (!pSink->AIO.fShutdown)
        {
            int rc = RTSemEventWait(pSink->AIO.hEvent, cMsSleep);
            AssertLogRelMsgReturn(RT_SUCCESS(rc) || rc == VERR_TIMEOUT, ("%s: RTSemEventWait -> %Rrc\n", pSink->pszName, rc), rc);
        }
    }

    LogFlowFunc(("%s: returnining normally.\n", pSink->pszName));
    return VINF_SUCCESS;
}


/**
 * Adds an AIO update job to the sink.
 *
 * @returns VBox status code.
 * @retval  VERR_ALREADY_EXISTS if already registered job with same @a pvUser
 *          and @a pfnUpdate.
 *
 * @param   pSink               The mixer sink to remove the AIO job from.
 * @param   pfnUpdate           The update callback for the job.
 * @param   pvUser              The user parameter to pass to @a pfnUpdate.  This should
 *                              identify the job unique together with @a pfnUpdate.
 * @param   cMsTypicalInterval  A typical interval between jobs in milliseconds.
 *                              This is used when draining.
 */
int AudioMixerSinkAddUpdateJob(PAUDMIXSINK pSink, PFNAUDMIXSINKUPDATE pfnUpdate, void *pvUser, uint32_t cMsTypicalInterval)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, rc);

    /*
     * Check that the job hasn't already been added.
     */
    uintptr_t const iEnd = pSink->AIO.cUpdateJobs;
    for (uintptr_t i = 0; i < iEnd; i++)
        AssertReturnStmt(   pvUser    != pSink->AIO.aUpdateJobs[i].pvUser
                         || pfnUpdate != pSink->AIO.aUpdateJobs[i].pfnUpdate,
                         RTCritSectLeave(&pSink->CritSect),
                         VERR_ALREADY_EXISTS);

    AssertReturnStmt(iEnd < RT_ELEMENTS(pSink->AIO.aUpdateJobs),
                     RTCritSectLeave(&pSink->CritSect),
                     VERR_ALREADY_EXISTS);

    /*
     * Create the thread if not already running or if it stopped.
     */
/** @todo move this to the sink "enable" code */
    if (pSink->AIO.hThread != NIL_RTTHREAD)
    {
        int rcThread = VINF_SUCCESS;
        rc = RTThreadWait(pSink->AIO.hThread, 0, &rcThread);
        if (RT_FAILURE_NP(rc))
        { /* likely */ }
        else
        {
            LogRel(("Audio: AIO thread for '%s' died? rcThread=%Rrc\n", pSink->pszName, rcThread));
            pSink->AIO.hThread = NIL_RTTHREAD;
        }
    }
    if (pSink->AIO.hThread == NIL_RTTHREAD)
    {
        LogFlowFunc(("%s: Starting AIO thread...\n", pSink->pszName));
        if (pSink->AIO.hEvent == NIL_RTSEMEVENT)
        {
            rc = RTSemEventCreate(&pSink->AIO.hEvent);
            AssertRCReturnStmt(rc, RTCritSectLeave(&pSink->CritSect), rc);
        }
        static uint32_t volatile s_idxThread = 0;
        uint32_t idxThread = ASMAtomicIncU32(&s_idxThread);
        rc = RTThreadCreateF(&pSink->AIO.hThread, audioMixerSinkAsyncIoThread, pSink, 0 /*cbStack*/, RTTHREADTYPE_IO,
                             RTTHREADFLAGS_WAITABLE | RTTHREADFLAGS_COM_MTA, "MixAIO-%u", idxThread);
        AssertRCReturnStmt(rc, RTCritSectLeave(&pSink->CritSect), rc);
    }

    /*
     * Finally, actually add the job.
     */
    pSink->AIO.aUpdateJobs[iEnd].pfnUpdate          = pfnUpdate;
    pSink->AIO.aUpdateJobs[iEnd].pvUser             = pvUser;
    pSink->AIO.aUpdateJobs[iEnd].cMsTypicalInterval = cMsTypicalInterval;
    pSink->AIO.cUpdateJobs = (uint8_t)(iEnd + 1);
    if (cMsTypicalInterval < pSink->AIO.cMsMinTypicalInterval)
        pSink->AIO.cMsMinTypicalInterval = cMsTypicalInterval;
    LogFlowFunc(("%s: [#%zu]: Added pfnUpdate=%p pvUser=%p typically every %u ms (min %u ms)\n",
                 pSink->pszName, iEnd, pfnUpdate, pvUser, cMsTypicalInterval, pSink->AIO.cMsMinTypicalInterval));

    RTCritSectLeave(&pSink->CritSect);
    return VINF_SUCCESS;

}


/**
 * Removes an update job previously registered via AudioMixerSinkAddUpdateJob().
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_FOUND if not found.
 *
 * @param   pSink       The mixer sink to remove the AIO job from.
 * @param   pfnUpdate   The update callback of the job.
 * @param   pvUser      The user parameter identifying the job.
 */
int AudioMixerSinkRemoveUpdateJob(PAUDMIXSINK pSink, PFNAUDMIXSINKUPDATE pfnUpdate, void *pvUser)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, rc);

    rc = VERR_NOT_FOUND;
    for (uintptr_t iJob = 0; iJob < pSink->AIO.cUpdateJobs; iJob++)
        if (   pvUser    == pSink->AIO.aUpdateJobs[iJob].pvUser
            && pfnUpdate == pSink->AIO.aUpdateJobs[iJob].pfnUpdate)
        {
            pSink->AIO.cUpdateJobs--;
            if (iJob != pSink->AIO.cUpdateJobs)
                memmove(&pSink->AIO.aUpdateJobs[iJob], &pSink->AIO.aUpdateJobs[iJob + 1],
                        (pSink->AIO.cUpdateJobs - iJob) * sizeof(pSink->AIO.aUpdateJobs[0]));
            LogFlowFunc(("%s: [#%zu]: Removed pfnUpdate=%p pvUser=%p => cUpdateJobs=%u\n",
                         pSink->pszName, iJob, pfnUpdate, pvUser, pSink->AIO.cUpdateJobs));
            rc = VINF_SUCCESS;
            break;
        }
    AssertRC(rc);

    /* Recalc the minimum sleep interval (do it always). */
    pSink->AIO.cMsMinTypicalInterval = RT_MS_1SEC / 2;
    for (uintptr_t iJob = 0; iJob < pSink->AIO.cUpdateJobs; iJob++)
        if (pSink->AIO.aUpdateJobs[iJob].cMsTypicalInterval < pSink->AIO.cMsMinTypicalInterval)
            pSink->AIO.cMsMinTypicalInterval = pSink->AIO.aUpdateJobs[iJob].cMsTypicalInterval;


    RTCritSectLeave(&pSink->CritSect);
    return rc;
}


/**
 * Transfer data from the device's DMA buffer and into the sink.
 *
 * The caller is already holding the mixer sink's critical section, either by
 * way of being the AIO thread doing update jobs or by explicit locking calls.
 *
 * @returns The new stream offset.
 * @param   pSink       The mixer sink to transfer samples to.
 * @param   pCircBuf    The internal DMA buffer to move samples from.
 * @param   offStream   The stream current offset (logging, dtrace, return).
 * @param   idStream    Device specific audio stream identifier (logging, dtrace).
 * @param   pDbgFile    Debug file, NULL if disabled.
 */
uint64_t AudioMixerSinkTransferFromCircBuf(PAUDMIXSINK pSink, PRTCIRCBUF pCircBuf, uint64_t offStream,
                                           uint32_t idStream, PAUDIOHLPFILE pDbgFile)
{
    /*
     * Sanity.
     */
    AssertReturn(pSink, offStream);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    AssertReturn(pCircBuf, offStream);
    Assert(RTCritSectIsOwner(&pSink->CritSect));
    RT_NOREF(idStream);

    /*
     * Figure how much that we can push down.
     */
    uint32_t const cbSinkWritable     = AudioMixerSinkGetWritable(pSink);
    uint32_t const cbCircBufReadable  = (uint32_t)RTCircBufUsed(pCircBuf);
    uint32_t       cbToTransfer       = RT_MIN(cbCircBufReadable, cbSinkWritable);
    /* Make sure that we always align the number of bytes when reading to the stream's PCM properties. */
    uint32_t const cbToTransfer2      = cbToTransfer = PDMAudioPropsFloorBytesToFrame(&pSink->PCMProps, cbToTransfer);

    Log3Func(("idStream=%u: cbSinkWritable=%#RX32 cbCircBufReadable=%#RX32 -> cbToTransfer=%#RX32 @%#RX64\n",
              idStream, cbSinkWritable, cbCircBufReadable, cbToTransfer, offStream));
    AssertMsg(!(pSink->fStatus & AUDMIXSINK_STS_DRAINING) || cbCircBufReadable == pSink->cbDmaLeftToDrain,
              ("cbCircBufReadable=%#x cbDmaLeftToDrain=%#x\n", cbCircBufReadable, pSink->cbDmaLeftToDrain));

    /*
     * Do the pushing.
     */
    while (cbToTransfer > 0)
    {
        void /*const*/ *pvSrcBuf;
        size_t          cbSrcBuf;
        RTCircBufAcquireReadBlock(pCircBuf, cbToTransfer, &pvSrcBuf, &cbSrcBuf);

        uint32_t cbWritten = 0;
        int rc = AudioMixerSinkWrite(pSink, pvSrcBuf, (uint32_t)cbSrcBuf, &cbWritten);
        AssertRC(rc);
        Assert(cbWritten <= cbSrcBuf);

        Log2Func(("idStream=%u: %#RX32/%#zx bytes read @%#RX64\n", idStream, cbWritten, cbSrcBuf, offStream));
#ifdef VBOX_WITH_DTRACE
        VBOXDD_AUDIO_MIXER_SINK_AIO_OUT(idStream, cbWritten, offStream);
#endif
        offStream += cbWritten;

        if (!pDbgFile)
        { /* likely */ }
        else
            AudioHlpFileWrite(pDbgFile, pvSrcBuf, cbSrcBuf, 0 /* fFlags */);


        RTCircBufReleaseReadBlock(pCircBuf, cbWritten);

        /* advance */
        cbToTransfer -= cbWritten;
    }

    /*
     * Advance drain status.
     */
    if (!(pSink->fStatus & AUDMIXSINK_STS_DRAINING))
    { /* likely for most of the playback time ... */ }
    else if (!(pSink->fStatus & AUDMIXSINK_STS_DRAINED_DMA))
    {
        if (cbToTransfer2 >= pSink->cbDmaLeftToDrain)
        {
            Assert(cbToTransfer2 == pSink->cbDmaLeftToDrain);
            Log3Func(("idStream=%u/'%s': Setting AUDMIXSINK_STS_DRAINED_DMA.\n", idStream, pSink->pszName));
            pSink->cbDmaLeftToDrain = 0;
            pSink->fStatus         |= AUDMIXSINK_STS_DRAINED_DMA;
        }
        else
        {
            pSink->cbDmaLeftToDrain -= cbToTransfer2;
            Log3Func(("idStream=%u/'%s': still %#x bytes left in the DMA buffer\n",
                      idStream, pSink->pszName, pSink->cbDmaLeftToDrain));
        }
    }
    else
        Assert(cbToTransfer2 == 0);

    return offStream;
}


/**
 * Transfer data to the device's DMA buffer from the sink.
 *
 * The caller is already holding the mixer sink's critical section, either by
 * way of being the AIO thread doing update jobs or by explicit locking calls.
 *
 * @returns The new stream offset.
 * @param   pSink       The mixer sink to transfer samples from.
 * @param   pCircBuf    The internal DMA buffer to move samples to.
 * @param   offStream   The stream current offset (logging, dtrace, return).
 * @param   idStream    Device specific audio stream identifier (logging, dtrace).
 * @param   pDbgFile    Debug file, NULL if disabled.
 */
uint64_t AudioMixerSinkTransferToCircBuf(PAUDMIXSINK pSink, PRTCIRCBUF pCircBuf, uint64_t offStream,
                                         uint32_t idStream, PAUDIOHLPFILE pDbgFile)
{
    /*
     * Sanity.
     */
    AssertReturn(pSink, offStream);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    AssertReturn(pCircBuf, offStream);
    Assert(RTCritSectIsOwner(&pSink->CritSect));

    /*
     * Figure out how much we can transfer.
     */
    const uint32_t cbSinkReadable    = AudioMixerSinkGetReadable(pSink);
    const uint32_t cbCircBufWritable = (uint32_t)RTCircBufFree(pCircBuf);
    uint32_t       cbToTransfer      = RT_MIN(cbCircBufWritable, cbSinkReadable);
    uint32_t       cFramesToTransfer = PDMAudioPropsBytesToFrames(&pSink->PCMProps, cbToTransfer);
    cbToTransfer = PDMAudioPropsFramesToBytes(&pSink->PCMProps, cFramesToTransfer);

    Log3Func(("idStream=%u: cbSinkReadable=%#RX32 cbCircBufWritable=%#RX32 -> cbToTransfer=%#RX32 (%RU32 frames) @%#RX64\n",
              idStream, cbSinkReadable, cbCircBufWritable, cbToTransfer, cFramesToTransfer, offStream));
    RT_NOREF(idStream);

    /** @todo should we throttle (read less) this if we're far ahead? */

    /*
     * Copy loop.
     */
    while (cbToTransfer > 0)
    {
/** @todo We should be able to read straight into the circular buffer here
 *        as it should have a frame aligned size. */

        /* Read a chunk of data. */
        uint8_t  abBuf[4096];
        uint32_t cbRead = 0;
        uint32_t cFramesRead = 0;
        AudioMixBufPeek(&pSink->MixBuf, 0, cFramesToTransfer, &cFramesRead,
                        &pSink->In.State, abBuf, RT_MIN(cbToTransfer, sizeof(abBuf)), &cbRead);
        AssertBreak(cFramesRead > 0);
        Assert(cbRead > 0);

        cFramesToTransfer -= cFramesRead;
        AudioMixBufAdvance(&pSink->MixBuf, cFramesRead);

        /* Write it to the internal DMA buffer. */
        uint32_t off = 0;
        while (off < cbRead)
        {
            void  *pvDstBuf;
            size_t cbDstBuf;
            RTCircBufAcquireWriteBlock(pCircBuf, cbRead - off, &pvDstBuf, &cbDstBuf);

            memcpy(pvDstBuf, &abBuf[off], cbDstBuf);

#ifdef VBOX_WITH_DTRACE
            VBOXDD_AUDIO_MIXER_SINK_AIO_IN(idStream, (uint32_t)cbDstBuf, offStream);
#endif
            offStream += cbDstBuf;

            RTCircBufReleaseWriteBlock(pCircBuf, cbDstBuf);

            off += (uint32_t)cbDstBuf;
        }
        Assert(off == cbRead);

        /* Write to debug file? */
        if (RT_LIKELY(!pDbgFile))
        { /* likely */ }
        else
            AudioHlpFileWrite(pDbgFile, abBuf, cbRead, 0 /* fFlags */);

        /* Advance. */
        Assert(cbRead <= cbToTransfer);
        cbToTransfer -= cbRead;
    }

    return offStream;
}


/**
 * Signals the AIO thread to perform updates.
 *
 * @returns VBox status code.
 * @param   pSink       The mixer sink which AIO thread needs to do chores.
 */
int AudioMixerSinkSignalUpdateJob(PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    return RTSemEventSignal(pSink->AIO.hEvent);
}


/**
 * Locks the mixer sink for purposes of serializing with the AIO thread.
 *
 * @returns VBox status code.
 * @param   pSink       The mixer sink to lock.
 */
int AudioMixerSinkLock(PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    return RTCritSectEnter(&pSink->CritSect);
}


/**
 * Try to lock the mixer sink for purposes of serializing with the AIO thread.
 *
 * @returns VBox status code.
 * @param   pSink       The mixer sink to lock.
 */
int AudioMixerSinkTryLock(PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    return RTCritSectTryEnter(&pSink->CritSect);
}


/**
 * Unlocks the sink.
 *
 * @returns VBox status code.
 * @param   pSink       The mixer sink to unlock.
 */
int     AudioMixerSinkUnlock(PAUDMIXSINK pSink)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    return RTCritSectLeave(&pSink->CritSect);
}


/**
 * Updates the (master) volume of a mixer sink.
 *
 * @returns VBox status code.
 * @param   pSink               Mixer sink to update volume for.
 * @param   pVolMaster          Master volume to set.
 */
static int audioMixerSinkUpdateVolume(PAUDMIXSINK pSink, PCPDMAUDIOVOLUME pVolMaster)
{
    AssertPtrReturn(pSink,      VERR_INVALID_POINTER);
    Assert(pSink->uMagic == AUDMIXSINK_MAGIC);
    AssertPtrReturn(pVolMaster, VERR_INVALID_POINTER);

    LogFlowFunc(("[%s] Master fMuted=%RTbool, lVol=%RU32, rVol=%RU32\n",
                  pSink->pszName, pVolMaster->fMuted, pVolMaster->uLeft, pVolMaster->uRight));
    LogFlowFunc(("[%s] fMuted=%RTbool, lVol=%RU32, rVol=%RU32 ",
                  pSink->pszName, pSink->Volume.fMuted, pSink->Volume.uLeft, pSink->Volume.uRight));

    /** @todo Very crude implementation for now -- needs more work! */

    pSink->VolumeCombined.fMuted  = pVolMaster->fMuted || pSink->Volume.fMuted;

    pSink->VolumeCombined.uLeft   = (  (pSink->Volume.uLeft ? pSink->Volume.uLeft : 1)
                                     * (pVolMaster->uLeft   ? pVolMaster->uLeft   : 1)) / PDMAUDIO_VOLUME_MAX;

    pSink->VolumeCombined.uRight  = (  (pSink->Volume.uRight ? pSink->Volume.uRight : 1)
                                     * (pVolMaster->uRight   ? pVolMaster->uRight   : 1)) / PDMAUDIO_VOLUME_MAX;

    LogFlow(("-> fMuted=%RTbool, lVol=%RU32, rVol=%RU32\n",
             pSink->VolumeCombined.fMuted, pSink->VolumeCombined.uLeft, pSink->VolumeCombined.uRight));

    AudioMixBufSetVolume(&pSink->MixBuf, &pSink->VolumeCombined);
    return VINF_SUCCESS;
}

/**
 * Writes data to a mixer output sink.
 *
 * @returns VBox status code.
 * @param   pSink               Sink to write data to.
 * @param   pvBuf               Buffer containing the audio data to write.
 * @param   cbBuf               Size (in bytes) of the buffer containing the audio data.
 * @param   pcbWritten          Number of bytes written. Optional.
 */
int AudioMixerSinkWrite(PAUDMIXSINK pSink, const void *pvBuf, uint32_t cbBuf, uint32_t *pcbWritten)
{
    AssertPtrReturn(pSink, VERR_INVALID_POINTER);
    AssertReturn(pSink->uMagic == AUDMIXSINK_MAGIC, VERR_INVALID_MAGIC);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pcbWritten, VERR_INVALID_POINTER);
    AssertMsgReturn(pSink->enmDir == PDMAUDIODIR_OUT, ("%s: Can't write to a sink which is not an output sink\n", pSink->pszName),
                    VERR_ACCESS_DENIED);

    int rc = RTCritSectEnter(&pSink->CritSect);
    AssertRCReturn(rc, rc);

    AssertMsgReturnStmt(pSink->fStatus & AUDMIXSINK_STS_RUNNING,
                        ("%s: Can't write to a sink which is not running (anymore) (status 0x%x)\n", pSink->pszName, pSink->fStatus),
                        RTCritSectLeave(&pSink->CritSect), VERR_INVALID_STATE);

    uint32_t cFrames   = AudioMixBufFree(&pSink->MixBuf);
    uint32_t cbToWrite = PDMAudioPropsFramesToBytes(&pSink->PCMProps, cFrames);
    cbToWrite = RT_MIN(cbToWrite, cbBuf);
    AudioMixBufWrite(&pSink->MixBuf, &pSink->Out.State, pvBuf, cbToWrite, 0 /*offDstFrame*/, cFrames, &cFrames);
    Assert(cbToWrite == PDMAudioPropsFramesToBytes(&pSink->PCMProps, cFrames));
    AudioMixBufCommit(&pSink->MixBuf, cFrames);
    if (pcbWritten)
        *pcbWritten = cbToWrite;

    /* Update the sink's last written time stamp. */
    pSink->tsLastReadWrittenNs = RTTimeNanoTS();

    Log3Func(("[%s] cbBuf=%#x -> cbWritten=%#x\n", pSink->pszName, cbBuf, cbToWrite));

    RTCritSectLeave(&pSink->CritSect);
    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
 * Mixer Stream implementation.
 ********************************************************************************************************************************/

/**
 * Controls a mixer stream, internal version.
 *
 * @returns VBox status code (generally ignored).
 * @param   pMixStream          Mixer stream to control.
 * @param   enmCmd              Mixer stream command to use.
 */
static int audioMixerStreamCtlInternal(PAUDMIXSTREAM pMixStream, PDMAUDIOSTREAMCMD enmCmd)
{
    Assert(pMixStream->uMagic == AUDMIXSTREAM_MAGIC);
    AssertPtrReturn(pMixStream->pConn, VERR_AUDIO_STREAM_NOT_READY);
    AssertPtrReturn(pMixStream->pStream, VERR_AUDIO_STREAM_NOT_READY);

    int rc = pMixStream->pConn->pfnStreamControl(pMixStream->pConn, pMixStream->pStream, enmCmd);

    LogFlowFunc(("[%s] enmCmd=%d, rc=%Rrc\n", pMixStream->pszName, enmCmd, rc));

    return rc;
}

/**
 * Updates a mixer stream's internal status.
 *
 * This may perform a stream re-init if the driver requests it, in which case
 * this may take a little while longer than usual...
 *
 * @returns VBox status code.
 * @param   pMixStream          Mixer stream to to update internal status for.
 */
static int audioMixerStreamUpdateStatus(PAUDMIXSTREAM pMixStream)
{
    Assert(pMixStream->uMagic == AUDMIXSTREAM_MAGIC);

    /*
     * Reset the mixer status to start with.
     */
    pMixStream->fStatus = AUDMIXSTREAM_STATUS_NONE;

    PPDMIAUDIOCONNECTOR const pConn = pMixStream->pConn;
    if (pConn) /* Audio connector available? */
    {
        PPDMAUDIOSTREAM const pStream = pMixStream->pStream;

        /*
         * Get the stream status.
         * Do re-init if needed and fetch the status again afterwards.
         */
        PDMAUDIOSTREAMSTATE enmState = pConn->pfnStreamGetState(pConn, pStream);
        if (enmState != PDMAUDIOSTREAMSTATE_NEED_REINIT)
        { /* likely */ }
        else
        {
            LogFunc(("[%s] needs re-init...\n", pMixStream->pszName));
            int rc = pConn->pfnStreamReInit(pConn, pStream);
            enmState = pConn->pfnStreamGetState(pConn, pStream);
            LogFunc(("[%s] re-init returns %Rrc and %s.\n", pMixStream->pszName, rc, PDMAudioStreamStateGetName(enmState)));

            PAUDMIXSINK const pSink = pMixStream->pSink;
            AssertPtr(pSink);
            if (pSink->enmDir == PDMAUDIODIR_OUT)
            {
                rc = AudioMixBufInitPeekState(&pSink->MixBuf, &pMixStream->PeekState, &pStream->Props);
                /** @todo we need to remember this, don't we? */
                AssertLogRelRCReturn(rc, VINF_SUCCESS);
            }
            else
            {
                rc = AudioMixBufInitWriteState(&pSink->MixBuf, &pMixStream->WriteState, &pStream->Props);
                /** @todo we need to remember this, don't we? */
                AssertLogRelRCReturn(rc, VINF_SUCCESS);
            }
        }

        /*
         * Translate the status to mixer speak.
         */
        AssertMsg(enmState > PDMAUDIOSTREAMSTATE_INVALID && enmState < PDMAUDIOSTREAMSTATE_END, ("%d\n", enmState));
        switch (enmState)
        {
            case PDMAUDIOSTREAMSTATE_NOT_WORKING:
            case PDMAUDIOSTREAMSTATE_NEED_REINIT:
            case PDMAUDIOSTREAMSTATE_INACTIVE:
                pMixStream->fStatus = AUDMIXSTREAM_STATUS_NONE;
                break;
            case PDMAUDIOSTREAMSTATE_ENABLED:
                pMixStream->fStatus = AUDMIXSTREAM_STATUS_ENABLED;
                break;
            case PDMAUDIOSTREAMSTATE_ENABLED_READABLE:
                Assert(pMixStream->pSink->enmDir == PDMAUDIODIR_IN);
                pMixStream->fStatus = AUDMIXSTREAM_STATUS_ENABLED | AUDMIXSTREAM_STATUS_CAN_READ;
                break;
            case PDMAUDIOSTREAMSTATE_ENABLED_WRITABLE:
                Assert(pMixStream->pSink->enmDir == PDMAUDIODIR_OUT);
                pMixStream->fStatus = AUDMIXSTREAM_STATUS_ENABLED | AUDMIXSTREAM_STATUS_CAN_WRITE;
                break;
            /* no default */
            case PDMAUDIOSTREAMSTATE_INVALID:
            case PDMAUDIOSTREAMSTATE_END:
            case PDMAUDIOSTREAMSTATE_32BIT_HACK:
                break;
        }
    }

    LogFlowFunc(("[%s] -> 0x%x\n", pMixStream->pszName, pMixStream->fStatus));
    return VINF_SUCCESS;
}

/**
 * Destroys & frees a mixer stream, internal version.
 *
 * Worker for audioMixerSinkDestroyInternal and AudioMixerStreamDestroy.
 *
 * @param   pMixStream  Mixer stream to destroy.
 * @param   pDevIns     The device instance the statistics are registered with.
 * @param   fImmediate  How to handle still draining streams, whether to let
 *                      them complete (@c false) or destroy them immediately (@c
 *                      true).
 */
static void audioMixerStreamDestroyInternal(PAUDMIXSTREAM pMixStream, PPDMDEVINS pDevIns, bool fImmediate)
{
    AssertPtrReturnVoid(pMixStream);

    LogFunc(("%s\n", pMixStream->pszName));
    Assert(pMixStream->uMagic == AUDMIXSTREAM_MAGIC);

    if (pMixStream->pConn) /* Stream has a connector interface present? */
    {
        if (pMixStream->pStream)
        {
            pMixStream->pConn->pfnStreamRelease(pMixStream->pConn, pMixStream->pStream);
            pMixStream->pConn->pfnStreamDestroy(pMixStream->pConn, pMixStream->pStream, fImmediate);

            pMixStream->pStream = NULL;
        }

        pMixStream->pConn = NULL;
    }

    if (pMixStream->pszStatPrefix)
    {
        PDMDevHlpSTAMDeregisterByPrefix(pDevIns, pMixStream->pszStatPrefix);
        RTStrFree(pMixStream->pszStatPrefix);
        pMixStream->pszStatPrefix = NULL;
    }

    RTStrFree(pMixStream->pszName);
    pMixStream->pszName = NULL;

    int rc2 = RTCritSectDelete(&pMixStream->CritSect);
    AssertRC(rc2);

    RTMemFree(pMixStream);
    pMixStream = NULL;
}

/**
 * Destroys a mixer stream.
 *
 * @param   pMixStream  Mixer stream to destroy.
 * @param   pDevIns     The device instance statistics are registered with.
 * @param   fImmediate  How to handle still draining streams, whether to let
 *                      them complete (@c false) or destroy them immediately (@c
 *                      true).
 */
void AudioMixerStreamDestroy(PAUDMIXSTREAM pMixStream, PPDMDEVINS pDevIns, bool fImmediate)
{
    if (!pMixStream)
        return;

/** @todo r=bird: Wrng critsect for audioMixerSinkRemoveStreamInternal   */
    int rc2 = RTCritSectEnter(&pMixStream->CritSect);
    AssertRC(rc2);

    LogFunc(("%s\n", pMixStream->pszName));

    if (pMixStream->pSink) /* Is the stream part of a sink? */
    {
        /* Save sink pointer, as after audioMixerSinkRemoveStreamInternal() the
         * pointer will be gone from the stream. */
        PAUDMIXSINK pSink = pMixStream->pSink;

        rc2 = audioMixerSinkRemoveStreamInternal(pSink, pMixStream);
        if (RT_SUCCESS(rc2))
        {
            Assert(pSink->cStreams);
            pSink->cStreams--;
        }
    }
    else
        rc2 = VINF_SUCCESS;

    int rc3 = RTCritSectLeave(&pMixStream->CritSect);
    AssertRC(rc3);

    if (RT_SUCCESS(rc2))
    {
        audioMixerStreamDestroyInternal(pMixStream, pDevIns, fImmediate);
        pMixStream = NULL;
    }

    LogFlowFunc(("Returning %Rrc\n", rc2));
}

