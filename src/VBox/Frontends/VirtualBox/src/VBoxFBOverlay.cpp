/* $Id$ */
/** @file
 * VBoxFBOverlay implementaion
 */

/*
 * Copyright (C) 2009 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#if defined (VBOX_GUI_USE_QGL)

#ifdef VBOX_WITH_PRECOMPILED_HEADERS
# include "precomp.h"
#else  /* !VBOX_WITH_PRECOMPILED_HEADERS */
#define LOG_GROUP LOG_GROUP_GUI

#include "VBoxFBOverlay.h"

#include "VBoxProblemReporter.h"
#include "VBoxGlobal.h"

#include <VBox/VBoxGL2D.h>

/* Qt includes */
#include <QGLWidget>

#include <iprt/asm.h>

#ifdef VBOX_WITH_VIDEOHWACCEL
#include <VBox/VBoxVideo.h>
#include <VBox/types.h>
#include <VBox/ssm.h>
#endif
#include <iprt/semaphore.h>

#include <QFile>
#include <QTextStream>
#endif /* !VBOX_WITH_PRECOMPILED_HEADERS */

#ifdef VBOXQGL_PROF_BASE
# ifdef VBOXQGL_DBG_SURF
#  define VBOXQGL_PROF_WIDTH 1400
#  define VBOXQGL_PROF_HEIGHT 1050
# else
# define VBOXQGL_PROF_WIDTH 1400
# define VBOXQGL_PROF_HEIGHT 1050
//#define VBOXQGL_PROF_WIDTH 720
//#define VBOXQGL_PROF_HEIGHT 480
# endif
#endif

#define VBOXQGL_STATE_NAMEBASE "QGLVHWAData"
#define VBOXQGL_STATE_VERSION 2

#ifdef DEBUG
VBoxVHWADbgTimer::VBoxVHWADbgTimer(uint32_t cPeriods) :
        mPeriodSum(0LL),
        mPrevTime(0LL),
        mcFrames(0LL),
        mcPeriods(cPeriods),
        miPeriod(0)
{
    mpaPeriods = new uint64_t[cPeriods];
    memset(mpaPeriods, 0, cPeriods * sizeof(mpaPeriods[0]));
}

VBoxVHWADbgTimer::~VBoxVHWADbgTimer()
{
    delete[] mpaPeriods;
}

void VBoxVHWADbgTimer::frame()
{
    uint64_t cur = VBOXGETTIME();
    if(mPrevTime)
    {
        uint64_t curPeriod = cur - mPrevTime;
        mPeriodSum += curPeriod - mpaPeriods[miPeriod];
        mpaPeriods[miPeriod] = curPeriod;
        ++miPeriod;
        miPeriod %= mcPeriods;
    }
    mPrevTime = cur;
    ++mcFrames;
}
#endif

//#define VBOXQGLOVERLAY_STATE_NAMEBASE "QGLOverlayVHWAData"
//#define VBOXQGLOVERLAY_STATE_VERSION 1

#ifdef DEBUG_misha
//# define VBOXQGL_STATE_DEBUG
#endif

#ifdef VBOXQGL_STATE_DEBUG
#define VBOXQGL_STATE_START_MAGIC        0x12345678
#define VBOXQGL_STATE_STOP_MAGIC         0x87654321

#define VBOXQGL_STATE_SURFSTART_MAGIC    0x9abcdef1
#define VBOXQGL_STATE_SURFSTOP_MAGIC     0x1fedcba9

#define VBOXQGL_STATE_OVERLAYSTART_MAGIC 0x13579bdf
#define VBOXQGL_STATE_OVERLAYSTOP_MAGIC  0xfdb97531

#define VBOXQGL_SAVE_START(_pSSM) do{ int rc = SSMR3PutU32(_pSSM, VBOXQGL_STATE_START_MAGIC); AssertRC(rc);}while(0)
#define VBOXQGL_SAVE_STOP(_pSSM) do{ int rc = SSMR3PutU32(_pSSM, VBOXQGL_STATE_STOP_MAGIC); AssertRC(rc);}while(0)

#define VBOXQGL_SAVE_SURFSTART(_pSSM) do{ int rc = SSMR3PutU32(_pSSM, VBOXQGL_STATE_SURFSTART_MAGIC); AssertRC(rc);}while(0)
#define VBOXQGL_SAVE_SURFSTOP(_pSSM) do{ int rc = SSMR3PutU32(_pSSM, VBOXQGL_STATE_SURFSTOP_MAGIC); AssertRC(rc);}while(0)

#define VBOXQGL_SAVE_OVERLAYSTART(_pSSM) do{ int rc = SSMR3PutU32(_pSSM, VBOXQGL_STATE_OVERLAYSTART_MAGIC); AssertRC(rc);}while(0)
#define VBOXQGL_SAVE_OVERLAYSTOP(_pSSM) do{ int rc = SSMR3PutU32(_pSSM, VBOXQGL_STATE_OVERLAYSTOP_MAGIC); AssertRC(rc);}while(0)

#define VBOXQGL_LOAD_CHECK(_pSSM, _v) \
    do{ \
        uint32_t _u32; \
        int rc = SSMR3GetU32(_pSSM, &_u32); AssertRC(rc); \
        if(_u32 != (_v)) \
        { \
            VBOXQGLLOG(("load error: expected magic (0x%x), but was (0x%x)\n", (_v), _u32));\
        }\
        Assert(_u32 == (_v)); \
    }while(0)

#define VBOXQGL_LOAD_START(_pSSM) VBOXQGL_LOAD_CHECK(_pSSM, VBOXQGL_STATE_START_MAGIC)
#define VBOXQGL_LOAD_STOP(_pSSM) VBOXQGL_LOAD_CHECK(_pSSM, VBOXQGL_STATE_STOP_MAGIC)

#define VBOXQGL_LOAD_SURFSTART(_pSSM) VBOXQGL_LOAD_CHECK(_pSSM, VBOXQGL_STATE_SURFSTART_MAGIC)
#define VBOXQGL_LOAD_SURFSTOP(_pSSM) VBOXQGL_LOAD_CHECK(_pSSM, VBOXQGL_STATE_SURFSTOP_MAGIC)

#define VBOXQGL_LOAD_OVERLAYSTART(_pSSM) VBOXQGL_LOAD_CHECK(_pSSM, VBOXQGL_STATE_OVERLAYSTART_MAGIC)
#define VBOXQGL_LOAD_OVERLAYSTOP(_pSSM) VBOXQGL_LOAD_CHECK(_pSSM, VBOXQGL_STATE_OVERLAYSTOP_MAGIC)

#else

#define VBOXQGL_SAVE_START(_pSSM) do{}while(0)
#define VBOXQGL_SAVE_STOP(_pSSM) do{}while(0)

#define VBOXQGL_SAVE_SURFSTART(_pSSM) do{}while(0)
#define VBOXQGL_SAVE_SURFSTOP(_pSSM) do{}while(0)

#define VBOXQGL_SAVE_OVERLAYSTART(_pSSM) do{}while(0)
#define VBOXQGL_SAVE_OVERLAYSTOP(_pSSM) do{}while(0)

#define VBOXQGL_LOAD_START(_pSSM) do{}while(0)
#define VBOXQGL_LOAD_STOP(_pSSM) do{}while(0)

#define VBOXQGL_LOAD_SURFSTART(_pSSM) do{}while(0)
#define VBOXQGL_LOAD_SURFSTOP(_pSSM) do{}while(0)

#define VBOXQGL_LOAD_OVERLAYSTART(_pSSM) do{}while(0)
#define VBOXQGL_LOAD_OVERLAYSTOP(_pSSM) do{}while(0)

#endif

static VBoxVHWAInfo g_VBoxVHWASupportInfo;
static bool g_bVBoxVHWAChecked = false;
static bool g_bVBoxVHWASupported = false;

static struct _VBOXVHWACMD * vhwaHHCmdCreate(VBOXVHWACMD_TYPE type, size_t size)
{
    char *buf = (char*)malloc(VBOXVHWACMD_SIZE_FROMBODYSIZE(size));
    memset(buf, 0, size);
    VBOXVHWACMD * pCmd = (VBOXVHWACMD*)buf;
    pCmd->enmCmd = type;
    pCmd->Flags = VBOXVHWACMD_FLAG_HH_CMD;
    return pCmd;
}

static const VBoxVHWAInfo & vboxVHWAGetSupportInfo(const QGLContext *pContext)
{
    if(!g_VBoxVHWASupportInfo.isInitialized())
    {
        if(pContext)
        {
            g_VBoxVHWASupportInfo.init(pContext);
        }
        else
        {
            VBoxGLTmpContext ctx;
            const QGLContext *pContext = ctx.makeCurrent();
            Assert(pContext);
            if(pContext)
            {
                g_VBoxVHWASupportInfo.init(pContext);
            }
        }
    }
    return g_VBoxVHWASupportInfo;
}

class VBoxVHWACommandProcessEvent : public QEvent
{
public:
    VBoxVHWACommandProcessEvent ()
        : QEvent ((QEvent::Type) VBoxDefs::VHWACommandProcessType)
    {
#ifdef DEBUG_misha
        g_EventCounter.inc();
#endif
    }
#ifdef DEBUG_misha
    ~VBoxVHWACommandProcessEvent()
    {
        g_EventCounter.dec();
    }

    static uint32_t cPending() { return g_EventCounter.refs(); }
private:
    static VBoxVHWARefCounter g_EventCounter;
#endif
};

#ifdef DEBUG_misha
VBoxVHWARefCounter VBoxVHWACommandProcessEvent::g_EventCounter;
#endif

VBoxVHWAHandleTable::VBoxVHWAHandleTable(uint32_t initialSize)
{
    mTable = new void*[initialSize];
    memset(mTable, 0, initialSize*sizeof(void*));
    mcSize = initialSize;
    mcUsage = 0;
    mCursor = 1; /* 0 is treated as invalid */
}

VBoxVHWAHandleTable::~VBoxVHWAHandleTable()
{
    delete[] mTable;
}

uint32_t VBoxVHWAHandleTable::put(void * data)
{
    Assert(data);
    if(!data)
        return VBOXVHWA_SURFHANDLE_INVALID;

    if(mcUsage == mcSize)
    {
        /* @todo: resize */
        Assert(0);
    }

    Assert(mcUsage < mcSize);
    if(mcUsage >= mcSize)
        return VBOXVHWA_SURFHANDLE_INVALID;

    for(int k = 0; k < 2; ++k)
    {
        Assert(mCursor != 0);
        for(uint32_t i = mCursor; i < mcSize; ++i)
        {
            if(!mTable[i])
            {
                doPut(i, data);
                mCursor = i+1;
                return i;
            }
        }
        mCursor = 1; /* 0 is treated as invalid */
    }

    Assert(0);
    return VBOXVHWA_SURFHANDLE_INVALID;
}

bool VBoxVHWAHandleTable::mapPut(uint32_t h, void * data)
{
    if(mcSize <= h)
        return false;
    if(h == 0)
        return false;
    if(mTable[h])
        return false;

    doPut(h, data);
    return true;
}

void* VBoxVHWAHandleTable::get(uint32_t h)
{
    Assert(h < mcSize);
    Assert(h > 0);
    return mTable[h];
}

void* VBoxVHWAHandleTable::remove(uint32_t h)
{
    Assert(mcUsage);
    Assert(h < mcSize);
    void* val = mTable[h];
    Assert(val);
    if(val)
    {
        doRemove(h);
    }
    return val;
}

void VBoxVHWAHandleTable::doPut(uint32_t h, void * data)
{
    ++mcUsage;
    mTable[h] = data;
}

void VBoxVHWAHandleTable::doRemove(uint32_t h)
{
    mTable[h] = 0;
    --mcUsage;
}

static VBoxVHWATextureImage* vboxVHWAImageCreate(const QRect & aRect, const VBoxVHWAColorFormat & aFormat, class VBoxVHWAGlProgramMngr * pMgr, VBOXVHWAIMG_TYPE flags)
{
    const VBoxVHWAInfo & info = vboxVHWAGetSupportInfo(NULL);
    if((flags & VBOXVHWAIMG_PBO) && !info.getGlInfo().isPBOSupported())
        flags &= ~VBOXVHWAIMG_PBO;

    if((flags & VBOXVHWAIMG_PBOIMG) &&
            (!info.getGlInfo().isPBOSupported() || !info.getGlInfo().isPBOOffsetSupported()))
        flags &= ~VBOXVHWAIMG_PBOIMG;

    if((flags & VBOXVHWAIMG_FBO) && !info.getGlInfo().isFBOSupported())
        flags &= ~VBOXVHWAIMG_FBO;

    /* ensure we don't create a PBO-based texture in case we use a PBO-based image */
    if(flags & VBOXVHWAIMG_PBOIMG)
        flags &= ~VBOXVHWAIMG_PBO;

    if(flags & VBOXVHWAIMG_PBOIMG)
    {
        if(flags & VBOXVHWAIMG_FBO)
        {
            VBOXQGLLOG(("FBO PBO Image\n"));
            return new VBoxVHWATextureImageFBO<VBoxVHWATextureImagePBO>(aRect, aFormat, pMgr, flags);
        }
        VBOXQGLLOG(("PBO Image\n"));
        return new VBoxVHWATextureImagePBO(aRect, aFormat, pMgr, flags);
    }
    if(flags & VBOXVHWAIMG_FBO)
    {
        VBOXQGLLOG(("FBO Generic Image\n"));
        return new VBoxVHWATextureImageFBO<VBoxVHWATextureImage>(aRect, aFormat, pMgr, flags);
    }
    VBOXQGLLOG(("Generic Image\n"));
    return new VBoxVHWATextureImage(aRect, aFormat, pMgr, flags);
}

static VBoxVHWATexture* vboxVHWATextureCreate(const QGLContext * pContext, const QRect & aRect, const VBoxVHWAColorFormat & aFormat, VBOXVHWAIMG_TYPE flags)
{
    const VBoxVHWAInfo & info = vboxVHWAGetSupportInfo(pContext);
    GLint scaleFunc = (flags & VBOXVHWAIMG_LINEAR) ? GL_LINEAR : GL_NEAREST;
    if((flags & VBOXVHWAIMG_PBO) && info.getGlInfo().isPBOSupported())
    {
        VBOXQGLLOG(("VBoxVHWATextureNP2RectPBO\n"));
        return new VBoxVHWATextureNP2RectPBO(aRect, aFormat, scaleFunc);
    }
    else if(info.getGlInfo().isTextureRectangleSupported())
    {
        VBOXQGLLOG(("VBoxVHWATextureNP2Rect\n"));
        return new VBoxVHWATextureNP2Rect(aRect, aFormat, scaleFunc);
    }
    else if(info.getGlInfo().isTextureNP2Supported())
    {
        VBOXQGLLOG(("VBoxVHWATextureNP2\n"));
        return new VBoxVHWATextureNP2(aRect, aFormat, scaleFunc);
    }
    VBOXQGLLOG(("VBoxVHWATexture\n"));
    return new VBoxVHWATexture(aRect, aFormat, scaleFunc);
}

class VBoxVHWAGlShaderComponent
{
public:
    VBoxVHWAGlShaderComponent(const char *aRcName, GLenum aType) :
        mRcName(aRcName),
        mType(aType),
        mInitialized(false)
    {}


    int init();

    const char * contents() { return mSource.constData(); }
    bool isInitialized() { return mInitialized; }
private:
    const char *mRcName;
    GLenum mType;
    QByteArray mSource;
    bool mInitialized;
};

int VBoxVHWAGlShaderComponent::init()
{
    if(isInitialized())
        return VINF_ALREADY_INITIALIZED;

    QFile fi(mRcName);
    if (!fi.open(QIODevice::ReadOnly))
    {
        Assert(0);
        return VERR_GENERAL_FAILURE;
    }

    QTextStream is(&fi);
    QString program = is.readAll();

    mSource = program.toAscii();

    mInitialized = true;
    return VINF_SUCCESS;
}

class VBoxVHWAGlShader
{
public:
    VBoxVHWAGlShader() :
        mType(GL_FRAGMENT_SHADER),
        mcComponents(0)
    {}

    VBoxVHWAGlShader & operator= (const VBoxVHWAGlShader & other)
    {
        mcComponents = other.mcComponents;
        mType = other.mType;
        if(mcComponents)
        {
            maComponents = new VBoxVHWAGlShaderComponent*[mcComponents];
            memcpy(maComponents, other.maComponents, mcComponents * sizeof(maComponents[0]));
        }
        return *this;
    }

    VBoxVHWAGlShader(const VBoxVHWAGlShader & other)
    {
        mcComponents = other.mcComponents;
        mType = other.mType;
        if(mcComponents)
        {
            maComponents = new VBoxVHWAGlShaderComponent*[mcComponents];
            memcpy(maComponents, other.maComponents, mcComponents * sizeof(maComponents[0]));
        }
    }

    VBoxVHWAGlShader(GLenum aType, VBoxVHWAGlShaderComponent ** aComponents, int cComponents)
        : mType(aType)
    {
        maComponents = new VBoxVHWAGlShaderComponent*[cComponents];
        mcComponents = cComponents;
        memcpy(maComponents, aComponents, cComponents * sizeof(maComponents[0]));
    }

    ~VBoxVHWAGlShader() {delete[] maComponents;}
    int init();
    GLenum type() { return mType; }
    GLuint shader() { return mShader; }
private:
    GLenum mType;
    GLuint mShader;
    VBoxVHWAGlShaderComponent ** maComponents;
    int mcComponents;
};

int VBoxVHWAGlShader::init()
{
    int rc = VERR_GENERAL_FAILURE;
    GLint *length;
    const char **sources;
    length = new GLint[mcComponents];
    sources = new const char*[mcComponents];
    for(int i = 0; i < mcComponents; i++)
    {
        length[i] = -1;
        rc = maComponents[i]->init();
        AssertRC(rc);
        if(RT_FAILURE(rc))
            break;
        sources[i] = maComponents[i]->contents();
    }

    if(RT_SUCCESS(rc))
    {
#ifdef DEBUG
        VBOXQGLLOG(("\ncompiling shaders:\n------------\n"));
        for(int i = 0; i < mcComponents; i++)
        {
            VBOXQGLLOG(("**********\n%s\n***********\n", sources[i]));
        }
        VBOXQGLLOG(("------------\n"));
#endif
        mShader = vboxglCreateShader(mType);

        VBOXQGL_CHECKERR(
                vboxglShaderSource(mShader, mcComponents, sources, length);
                );

        VBOXQGL_CHECKERR(
                vboxglCompileShader(mShader);
                );

        GLint compiled;
        VBOXQGL_CHECKERR(
                vboxglGetShaderiv(mShader, GL_COMPILE_STATUS, &compiled);
                );

#ifdef DEBUG
        GLchar * pBuf = new GLchar[16300];
        vboxglGetShaderInfoLog(mShader, 16300, NULL, pBuf);
        VBOXQGLLOG(("\ncompile log:\n-----------\n%s\n---------\n", pBuf));
        delete[] pBuf;
#endif

        Assert(compiled);
        if(compiled)
        {
            rc = VINF_SUCCESS;
        }
        else
        {
            VBOXQGL_CHECKERR(
                    vboxglDeleteShader(mShader);
                    );
            mShader = 0;
        }
    }

    delete[] length;
    delete[] sources;
    return rc;
}

class VBoxVHWAGlProgram
{
public:
    VBoxVHWAGlProgram(VBoxVHWAGlShader ** apShaders, int acShaders);

    ~VBoxVHWAGlProgram();

    virtual int init();
    virtual void uninit();
    virtual int start();
    virtual int stop();
    bool isInitialized() { return mProgram; }
    GLuint program() {return mProgram;}
private:
    GLuint mProgram;
    VBoxVHWAGlShader *mShaders;
    int mcShaders;
};

VBoxVHWAGlProgram::VBoxVHWAGlProgram(VBoxVHWAGlShader ** apShaders, int acShaders) :
       mProgram(0),
       mcShaders(0)
{
    Assert(acShaders);
    if(acShaders)
    {
        mShaders = new VBoxVHWAGlShader[acShaders];
        for(int i = 0; i < acShaders; i++)
        {
            mShaders[i] = *apShaders[i];
        }
        mcShaders = acShaders;
    }
}

VBoxVHWAGlProgram::~VBoxVHWAGlProgram()
{
    uninit();

    if(mShaders)
    {
        delete[] mShaders;
    }
}

int VBoxVHWAGlProgram::init()
{
    Assert(!isInitialized());
    if(isInitialized())
        return VINF_ALREADY_INITIALIZED;

    Assert(mcShaders);
    if(!mcShaders)
        return VERR_GENERAL_FAILURE;

    int rc = VINF_SUCCESS;
    for(int i = 0; i < mcShaders; i++)
    {
        int rc = mShaders[i].init();
        AssertRC(rc);
        if(RT_FAILURE(rc))
        {
            break;
        }
    }
    if(RT_FAILURE(rc))
    {
        return rc;
    }

    mProgram = vboxglCreateProgram();
    Assert(mProgram);
    if(mProgram)
    {
        for(int i = 0; i < mcShaders; i++)
        {
            VBOXQGL_CHECKERR(
                    vboxglAttachShader(mProgram, mShaders[i].shader());
                    );
        }

        VBOXQGL_CHECKERR(
                vboxglLinkProgram(mProgram);
                );


        GLint linked;
        vboxglGetProgramiv(mProgram, GL_LINK_STATUS, &linked);

#ifdef DEBUG
        GLchar * pBuf = new GLchar[16300];
        vboxglGetProgramInfoLog(mProgram, 16300, NULL, pBuf);
        VBOXQGLLOG(("link log: %s\n", pBuf));
        Assert(linked);
        delete pBuf;
#endif

        if(linked)
        {
            return VINF_SUCCESS;
        }

        VBOXQGL_CHECKERR(
                vboxglDeleteProgram(mProgram);
                );
        mProgram = 0;
    }
    return VERR_GENERAL_FAILURE;
}

void VBoxVHWAGlProgram::uninit()
{
    if(!isInitialized())
        return;

    VBOXQGL_CHECKERR(
            vboxglDeleteProgram(mProgram);
            );
    mProgram = 0;
}

int VBoxVHWAGlProgram::start()
{
    VBOXQGL_CHECKERR(
            vboxglUseProgram(mProgram);
            );
    return VINF_SUCCESS;
}

int VBoxVHWAGlProgram::stop()
{
    VBOXQGL_CHECKERR(
            vboxglUseProgram(0);
            );
    return VINF_SUCCESS;
}

#define VBOXVHWA_PROGRAM_DSTCOLORKEY        0x00000001
#define VBOXVHWA_PROGRAM_SRCCOLORKEY        0x00000002
#define VBOXVHWA_PROGRAM_COLORCONV          0x00000004
#define VBOXVHWA_PROGRAM_COLORKEYNODISCARD  0x00000008

#define VBOXVHWA_SUPPORTED_PROGRAM ( \
        VBOXVHWA_PROGRAM_DSTCOLORKEY \
        | VBOXVHWA_PROGRAM_SRCCOLORKEY \
        | VBOXVHWA_PROGRAM_COLORCONV \
        | VBOXVHWA_PROGRAM_COLORKEYNODISCARD \
        )

class VBoxVHWAGlProgramVHWA : public VBoxVHWAGlProgram
{
public:
    VBoxVHWAGlProgramVHWA(uint32_t type, uint32_t fourcc, VBoxVHWAGlShader ** apShaders, int acShaders);

    uint32_t type() const {return mType;}
    uint32_t fourcc() const {return mFourcc;}

    int setDstCKeyUpperRange(GLfloat r, GLfloat g, GLfloat b);

    int setDstCKeyLowerRange(GLfloat r, GLfloat g, GLfloat b);

    int setSrcCKeyUpperRange(GLfloat r, GLfloat g, GLfloat b);

    int setSrcCKeyLowerRange(GLfloat r, GLfloat g, GLfloat b);


    virtual int init();

    bool matches(uint32_t type, uint32_t fourcc)
    {
        return mType == type && mFourcc == fourcc;
    }

    bool equals(const VBoxVHWAGlProgramVHWA & other)
    {
        return matches(other.mType, other.mFourcc);
    }

private:
    uint32_t mType;
    uint32_t mFourcc;

    GLfloat mDstUpperR, mDstUpperG, mDstUpperB;
    GLint mUniDstUpperColor;

    GLfloat mDstLowerR, mDstLowerG, mDstLowerB;
    GLint mUniDstLowerColor;

    GLfloat mSrcUpperR, mSrcUpperG, mSrcUpperB;
    GLint mUniSrcUpperColor;

    GLfloat mSrcLowerR, mSrcLowerG, mSrcLowerB;
    GLint mUniSrcLowerColor;

    GLint mDstTex;
    GLint mUniDstTex;

    GLint mSrcTex;
    GLint mUniSrcTex;

    GLint mVTex;
    GLint mUniVTex;

    GLint mUTex;
    GLint mUniUTex;
};

VBoxVHWAGlProgramVHWA::VBoxVHWAGlProgramVHWA(uint32_t type, uint32_t fourcc, VBoxVHWAGlShader ** apShaders, int acShaders) :
    VBoxVHWAGlProgram(apShaders, acShaders),
    mType(type),
    mFourcc(fourcc),
    mDstUpperR(0.0), mDstUpperG(0.0), mDstUpperB(0.0),
    mUniDstUpperColor(-1),
    mDstLowerR(0.0), mDstLowerG(0.0), mDstLowerB(0.0),
    mUniDstLowerColor(-1),
    mSrcUpperR(0.0), mSrcUpperG(0.0), mSrcUpperB(0.0),
    mUniSrcUpperColor(-1),
    mSrcLowerR(0.0), mSrcLowerG(0.0), mSrcLowerB(0.0),
    mUniSrcLowerColor(-1),
    mDstTex(-1),
    mUniDstTex(-1),
    mSrcTex(-1),
    mUniSrcTex(-1),
    mVTex(-1),
    mUniVTex(-1),
    mUTex(-1),
    mUniUTex(-1)
{}

int VBoxVHWAGlProgramVHWA::init()
{
    int rc = VBoxVHWAGlProgram::init();
    if(RT_FAILURE(rc))
        return rc;
    if(rc == VINF_ALREADY_INITIALIZED)
        return rc;

    start();

    rc = VERR_GENERAL_FAILURE;

    do
    {
        GLint tex = 0;
        mUniSrcTex = vboxglGetUniformLocation(program(), "uSrcTex");
        Assert(mUniSrcTex != -1);
        if(mUniSrcTex == -1)
            break;

        VBOXQGL_CHECKERR(
                vboxglUniform1i(mUniSrcTex, tex);
                );
        mSrcTex = tex;
        ++tex;

        if(type() & VBOXVHWA_PROGRAM_SRCCOLORKEY)
        {
            mUniSrcLowerColor = vboxglGetUniformLocation(program(), "uSrcClr");
            Assert(mUniSrcLowerColor != -1);
            if(mUniSrcLowerColor == -1)
                break;

            mSrcLowerR = 0.0; mSrcLowerG = 0.0; mSrcLowerB = 0.0;

            VBOXQGL_CHECKERR(
                    vboxglUniform4f(mUniSrcLowerColor, 0.0, 0.0, 0.0, 0.0);
                    );
        }

        if(type() & VBOXVHWA_PROGRAM_COLORCONV)
        {
            switch(fourcc())
            {
                case FOURCC_YV12:
                {
                    mUniVTex = vboxglGetUniformLocation(program(), "uVTex");
                    Assert(mUniVTex != -1);
                    if(mUniVTex == -1)
                        break;

                    VBOXQGL_CHECKERR(
                            vboxglUniform1i(mUniVTex, tex);
                            );
                    mVTex = tex;
                    ++tex;

                    mUniUTex = vboxglGetUniformLocation(program(), "uUTex");
                    Assert(mUniUTex != -1);
                    if(mUniUTex == -1)
                        break;
                    VBOXQGL_CHECKERR(
                            vboxglUniform1i(mUniUTex, tex);
                            );
                    mUTex = tex;
                    ++tex;

                    break;
                }
                case FOURCC_UYVY:
                case FOURCC_YUY2:
                case FOURCC_AYUV:
                    break;
                default:
                    Assert(0);
                    break;
            }
        }

        if(type() & VBOXVHWA_PROGRAM_DSTCOLORKEY)
        {

            mUniDstTex = vboxglGetUniformLocation(program(), "uDstTex");
            Assert(mUniDstTex != -1);
            if(mUniDstTex == -1)
                break;
            VBOXQGL_CHECKERR(
                    vboxglUniform1i(mUniDstTex, tex);
                    );
            mDstTex = tex;
            ++tex;

            mUniDstLowerColor = vboxglGetUniformLocation(program(), "uDstClr");
            Assert(mUniDstLowerColor != -1);
            if(mUniDstLowerColor == -1)
                break;

            mDstLowerR = 0.0; mDstLowerG = 0.0; mDstLowerB = 0.0;

            VBOXQGL_CHECKERR(
                    vboxglUniform4f(mUniDstLowerColor, 0.0, 0.0, 0.0, 0.0);
                    );
        }

        rc = VINF_SUCCESS;
    } while(0);


    stop();
    if(rc == VINF_SUCCESS)
        return VINF_SUCCESS;

    Assert(0);
    VBoxVHWAGlProgram::uninit();
    return VERR_GENERAL_FAILURE;
}

int VBoxVHWAGlProgramVHWA::setDstCKeyUpperRange(GLfloat r, GLfloat g, GLfloat b)
{
    Assert(isInitialized());
    if(!isInitialized())
        return VERR_GENERAL_FAILURE;
    if(mDstUpperR == r && mDstUpperG == g && mDstUpperB == b)
        return VINF_ALREADY_INITIALIZED;
    vboxglUniform4f(mUniDstUpperColor, r, g, b, 0.0);
    mDstUpperR = r;
    mDstUpperG = g;
    mDstUpperB = b;
    return VINF_SUCCESS;
}

int VBoxVHWAGlProgramVHWA::setDstCKeyLowerRange(GLfloat r, GLfloat g, GLfloat b)
{
    Assert(isInitialized());
    if(!isInitialized())
        return VERR_GENERAL_FAILURE;
    if(mDstLowerR == r && mDstLowerG == g && mDstLowerB == b)
        return VINF_ALREADY_INITIALIZED;

    VBOXQGL_CHECKERR(
            vboxglUniform4f(mUniDstLowerColor, r, g, b, 0.0);
            );

    mDstLowerR = r;
    mDstLowerG = g;
    mDstLowerB = b;
    return VINF_SUCCESS;
}

int VBoxVHWAGlProgramVHWA::setSrcCKeyUpperRange(GLfloat r, GLfloat g, GLfloat b)
{
    Assert(isInitialized());
    if(!isInitialized())
        return VERR_GENERAL_FAILURE;
    if(mSrcUpperR == r && mSrcUpperG == g && mSrcUpperB == b)
        return VINF_ALREADY_INITIALIZED;
    vboxglUniform4f(mUniSrcUpperColor, r, g, b, 0.0);
    mSrcUpperR = r;
    mSrcUpperG = g;
    mSrcUpperB = b;
    return VINF_SUCCESS;
}

int VBoxVHWAGlProgramVHWA::setSrcCKeyLowerRange(GLfloat r, GLfloat g, GLfloat b)
{
    Assert(isInitialized());
    if(!isInitialized())
        return VERR_GENERAL_FAILURE;
    if(mSrcLowerR == r && mSrcLowerG == g && mSrcLowerB == b)
        return VINF_ALREADY_INITIALIZED;
    VBOXQGL_CHECKERR(
            vboxglUniform4f(mUniSrcLowerColor, r, g, b, 0.0);
            );
    mSrcLowerR = r;
    mSrcLowerG = g;
    mSrcLowerB = b;
    return VINF_SUCCESS;
}

class VBoxVHWAGlProgramMngr
{
public:
    VBoxVHWAGlProgramMngr() :
        mShaderCConvApplyAYUV(":/cconvApplyAYUV.c", GL_FRAGMENT_SHADER),
        mShaderCConvAYUV(":/cconvAYUV.c", GL_FRAGMENT_SHADER),
        mShaderCConvBGR(":/cconvBGR.c", GL_FRAGMENT_SHADER),
        mShaderCConvUYVY(":/cconvUYVY.c", GL_FRAGMENT_SHADER),
        mShaderCConvYUY2(":/cconvYUY2.c", GL_FRAGMENT_SHADER),
        mShaderCConvYV12(":/cconvYV12.c", GL_FRAGMENT_SHADER),
        mShaderSplitBGRA(":/splitBGRA.c", GL_FRAGMENT_SHADER),
        mShaderCKeyDst(":/ckeyDst.c", GL_FRAGMENT_SHADER),
        mShaderCKeyDst2(":/ckeyDst2.c", GL_FRAGMENT_SHADER),
        mShaderMainOverlay(":/mainOverlay.c", GL_FRAGMENT_SHADER),
        mShaderMainOverlayNoCKey(":/mainOverlayNoCKey.c", GL_FRAGMENT_SHADER),
        mShaderMainOverlayNoDiscard(":/mainOverlayNoDiscard.c", GL_FRAGMENT_SHADER),
        mShaderMainOverlayNoDiscard2(":/mainOverlayNoDiscard2.c", GL_FRAGMENT_SHADER)
    {}

    VBoxVHWAGlProgramVHWA * getProgram(uint32_t type, const VBoxVHWAColorFormat * pFrom, const VBoxVHWAColorFormat * pTo);

    void stopCurrentProgram()
    {
        VBOXQGL_CHECKERR(
            vboxglUseProgram(0);
            );
    }
private:
    VBoxVHWAGlProgramVHWA * searchProgram(uint32_t type, uint32_t fourcc, bool bCreate);

    VBoxVHWAGlProgramVHWA * createProgram(uint32_t type, uint32_t fourcc);

    typedef std::list <VBoxVHWAGlProgramVHWA*> ProgramList;

    ProgramList mPrograms;

    VBoxVHWAGlShaderComponent mShaderCConvApplyAYUV;

    VBoxVHWAGlShaderComponent mShaderCConvAYUV;
    VBoxVHWAGlShaderComponent mShaderCConvBGR;
    VBoxVHWAGlShaderComponent mShaderCConvUYVY;
    VBoxVHWAGlShaderComponent mShaderCConvYUY2;
    VBoxVHWAGlShaderComponent mShaderCConvYV12;
    VBoxVHWAGlShaderComponent mShaderSplitBGRA;

    /* expected the dst surface texture to be bound to the 1-st tex unit */
    VBoxVHWAGlShaderComponent mShaderCKeyDst;
    /* expected the dst surface texture to be bound to the 2-nd tex unit */
    VBoxVHWAGlShaderComponent mShaderCKeyDst2;
    VBoxVHWAGlShaderComponent mShaderMainOverlay;
    VBoxVHWAGlShaderComponent mShaderMainOverlayNoCKey;
    VBoxVHWAGlShaderComponent mShaderMainOverlayNoDiscard;
    VBoxVHWAGlShaderComponent mShaderMainOverlayNoDiscard2;

    friend class VBoxVHWAGlProgramVHWA;
};

VBoxVHWAGlProgramVHWA * VBoxVHWAGlProgramMngr::createProgram(uint32_t type, uint32_t fourcc)
{
    VBoxVHWAGlShaderComponent * apShaders[16];
    uint32_t cShaders = 0;

    /* workaround for NVIDIA driver bug: ensure we attach the shader before those it is used in */
    /* reserve a slot for the mShaderCConvApplyAYUV,
     * in case it is not used the slot will be occupied by mShaderCConvBGR , which is ok */
    cShaders++;

    if(!!(type & VBOXVHWA_PROGRAM_DSTCOLORKEY)
            && !(type & VBOXVHWA_PROGRAM_COLORKEYNODISCARD))
    {
        if(fourcc == FOURCC_YV12)
        {
            apShaders[cShaders++] = &mShaderCKeyDst2;
        }
        else
        {
            apShaders[cShaders++] = &mShaderCKeyDst;
        }
    }

    if(type & VBOXVHWA_PROGRAM_SRCCOLORKEY)
    {
        Assert(0);
        /* disabled for now, not really necessary for video overlaying */
    }

    bool bFound = false;

//    if(type & VBOXVHWA_PROGRAM_COLORCONV)
    {
        if(fourcc == FOURCC_UYVY)
        {
            apShaders[cShaders++] = &mShaderCConvUYVY;
            bFound = true;
        }
        else if(fourcc == FOURCC_YUY2)
        {
            apShaders[cShaders++] = &mShaderCConvYUY2;
            bFound = true;
        }
        else if(fourcc == FOURCC_YV12)
        {
            apShaders[cShaders++] = &mShaderCConvYV12;
            bFound = true;
        }
        else if(fourcc == FOURCC_AYUV)
        {
            apShaders[cShaders++] = &mShaderCConvAYUV;
            bFound = true;
        }
    }

    if(bFound)
    {
        type |= VBOXVHWA_PROGRAM_COLORCONV;
        apShaders[0] = &mShaderCConvApplyAYUV;
    }
    else
    {
        type &= (~VBOXVHWA_PROGRAM_COLORCONV);
        apShaders[0] = &mShaderCConvBGR;
    }

    if(type & VBOXVHWA_PROGRAM_DSTCOLORKEY)
    {
        if(type & VBOXVHWA_PROGRAM_COLORKEYNODISCARD)
        {
            if(fourcc == FOURCC_YV12)
            {
                apShaders[cShaders++] = &mShaderMainOverlayNoDiscard2;
            }
            else
            {
                apShaders[cShaders++] = &mShaderMainOverlayNoDiscard;
            }
        }
        else
        {
            apShaders[cShaders++] = &mShaderMainOverlay;
        }
    }
    else
    {
        // ensure we don't have empty functions /* paranoya for for ATI on linux */
        apShaders[cShaders++] = &mShaderMainOverlayNoCKey;
    }

    Assert(cShaders <= RT_ELEMENTS(apShaders));

    VBoxVHWAGlShader shader(GL_FRAGMENT_SHADER, apShaders, cShaders);
    VBoxVHWAGlShader *pShader = &shader;

    VBoxVHWAGlProgramVHWA *pProgram =  new VBoxVHWAGlProgramVHWA(/*this, */type, fourcc, &pShader, 1);
    pProgram->init();

    return pProgram;
}

VBoxVHWAGlProgramVHWA * VBoxVHWAGlProgramMngr::getProgram(uint32_t type, const VBoxVHWAColorFormat * pFrom, const VBoxVHWAColorFormat * pTo)
{
    Q_UNUSED(pTo);
    uint32_t fourcc = 0;
    type &= VBOXVHWA_SUPPORTED_PROGRAM;

    if(pFrom && pFrom->fourcc())
    {
        fourcc = pFrom->fourcc();
        type |= VBOXVHWA_PROGRAM_COLORCONV;
    }
    else
    {
        type &= (~VBOXVHWA_PROGRAM_COLORCONV);
    }

    if(!(type & VBOXVHWA_PROGRAM_DSTCOLORKEY)
            && !(type & VBOXVHWA_PROGRAM_SRCCOLORKEY))
    {
        type &= (~VBOXVHWA_PROGRAM_COLORKEYNODISCARD);
    }

    if(type)
        return searchProgram(type, fourcc, true);
    return NULL;
}

VBoxVHWAGlProgramVHWA * VBoxVHWAGlProgramMngr::searchProgram(uint32_t type, uint32_t fourcc, bool bCreate)
{
    for (ProgramList::const_iterator it = mPrograms.begin();
         it != mPrograms.end(); ++ it)
    {
        if (!(*it)->matches(type, fourcc))
        {
            continue;
        }
        return *it;
    }
    if(bCreate)
    {
        VBoxVHWAGlProgramVHWA *pProgram = createProgram(type, fourcc);
        if(pProgram)
        {
            mPrograms.push_back(pProgram);
            return pProgram;
        }
    }
    return NULL;
}

void VBoxVHWASurfaceBase::setAddress(uchar * addr)
{
    Assert(addr);
    if(!addr) return;
    if(addr == mAddress) return;

    if(mFreeAddress)
    {
        free(mAddress);
    }

    mAddress = addr;
    mFreeAddress = false;

    mImage->setAddress(mAddress);

    mUpdateMem2TexRect.set(mRect);
    Assert(!mUpdateMem2TexRect.isClear());
    Assert(mRect.contains(mUpdateMem2TexRect.rect()));
}

void VBoxVHWASurfaceBase::globalInit()
{
    VBOXQGLLOG(("globalInit\n"));

    glEnable(GL_TEXTURE_RECTANGLE);
    glDisable(GL_DEPTH_TEST);

    VBOXQGL_CHECKERR(
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            );
    VBOXQGL_CHECKERR(
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            );
}

VBoxVHWASurfaceBase::VBoxVHWASurfaceBase(class VBoxVHWAImage *pImage,
        const QSize & aSize,
        const QRect & aTargRect,
        const QRect & aSrcRect,
        const QRect & aVisTargRect,
        VBoxVHWAColorFormat & aColorFormat,
        VBoxVHWAColorKey * pSrcBltCKey, VBoxVHWAColorKey * pDstBltCKey,
                    VBoxVHWAColorKey * pSrcOverlayCKey, VBoxVHWAColorKey * pDstOverlayCKey,
                    VBOXVHWAIMG_TYPE aImgFlags) :
                mRect(0,0,aSize.width(),aSize.height()),
                mAddress(NULL),
                mpSrcBltCKey(NULL),
                mpDstBltCKey(NULL),
                mpSrcOverlayCKey(NULL),
                mpDstOverlayCKey(NULL),
                mpDefaultDstOverlayCKey(NULL),
                mpDefaultSrcOverlayCKey(NULL),
                mLockCount(0),
                mFreeAddress(false),
                mbNotIntersected(false),
                mComplexList(NULL),
                mpPrimary(NULL),
                mHGHandle(VBOXVHWA_SURFHANDLE_INVALID),
                mpImage(pImage)
#ifdef DEBUG
                ,
                cFlipsCurr(0),
                cFlipsTarg(0)
#endif
{
    setDstBltCKey(pDstBltCKey);
    setSrcBltCKey(pSrcBltCKey);

    setDefaultDstOverlayCKey(pDstOverlayCKey);
    resetDefaultDstOverlayCKey();

    setDefaultSrcOverlayCKey(pSrcOverlayCKey);
    resetDefaultSrcOverlayCKey();

    mImage = vboxVHWAImageCreate(QRect(0,0,aSize.width(),aSize.height()), aColorFormat, getGlProgramMngr(), aImgFlags);

    setRectValues(aTargRect, aSrcRect);
    setVisibleRectValues(aVisTargRect);
}

VBoxVHWASurfaceBase::~VBoxVHWASurfaceBase()
{
    uninit();
}

GLsizei VBoxVHWASurfaceBase::makePowerOf2(GLsizei val)
{
    int last = ASMBitLastSetS32(val);
    if(last>1)
    {
        last--;
        if((1 << last) != val)
        {
            Assert((1 << last) < val);
            val = (1 << (last+1));
        }
    }
    return val;
}

ulong VBoxVHWASurfaceBase::calcBytesPerPixel(GLenum format, GLenum type)
{
    /* we now support only common byte-aligned data */
    int numComponents = 0;
    switch(format)
    {
    case GL_COLOR_INDEX:
    case GL_RED:
    case GL_GREEN:
    case GL_BLUE:
    case GL_ALPHA:
    case GL_LUMINANCE:
        numComponents = 1;
        break;
    case GL_RGB:
    case GL_BGR_EXT:
        numComponents = 3;
        break;
    case GL_RGBA:
    case GL_BGRA_EXT:
        numComponents = 4;
        break;
    case GL_LUMINANCE_ALPHA:
        numComponents = 2;
        break;
    default:
        Assert(0);
        break;
    }

    int componentSize = 0;
    switch(type)
    {
    case GL_UNSIGNED_BYTE:
    case GL_BYTE:
        componentSize = 1;
        break;
    //case GL_BITMAP:
    case  GL_UNSIGNED_SHORT:
    case GL_SHORT:
        componentSize = 2;
        break;
    case GL_UNSIGNED_INT:
    case GL_INT:
    case GL_FLOAT:
        componentSize = 4;
        break;
    default:
        Assert(0);
        break;
    }
    return numComponents * componentSize;
}

void VBoxVHWASurfaceBase::uninit()
{
    delete mImage;

    if(mAddress && mFreeAddress)
    {
        free(mAddress);
        mAddress = NULL;
    }
}

ulong VBoxVHWASurfaceBase::memSize()
{
    return (ulong)mImage->memSize();
}

void VBoxVHWASurfaceBase::init(VBoxVHWASurfaceBase * pPrimary, uchar *pvMem)
{
    if(pPrimary)
    {
        VBOXQGL_CHECKERR(
                vboxglActiveTexture(GL_TEXTURE1);
            );
    }

    int size = memSize();
    uchar * address = (uchar *)malloc(size);
#ifdef DEBUG_misha
    int tex0Size = mImage->component(0)->memSize();
    if(pPrimary)
    {
        memset(address, 0xff, tex0Size);
        Assert(size >= tex0Size);
        if(size > tex0Size)
        {
            memset(address + tex0Size, 0x0, size - tex0Size);
        }
    }
    else
    {
        memset(address, 0x0f, tex0Size);
        Assert(size >= tex0Size);
        if(size > tex0Size)
        {
            memset(address + tex0Size, 0x3f, size - tex0Size);
        }
    }
#else
    memset(address, 0, size);
#endif

    mImage->init(address);
    mpPrimary = pPrimary;

    if(pvMem)
    {
        mAddress = pvMem;
        free(address);
        mFreeAddress = false;

    }
    else
    {
        mAddress = address;
        mFreeAddress = true;
    }

    mImage->setAddress(mAddress);

    initDisplay();

    mUpdateMem2TexRect.set(mRect);
    Assert(!mUpdateMem2TexRect.isClear());
    Assert(mRect.contains(mUpdateMem2TexRect.rect()));

    if(pPrimary)
    {
        VBOXQGLLOG(("restoring to tex 0"));
        VBOXQGL_CHECKERR(
                vboxglActiveTexture(GL_TEXTURE0);
            );
    }

}

void VBoxVHWATexture::doUpdate(uchar * pAddress, const QRect * pRect)
{
    GLenum tt = texTarget();
    if(pRect)
    {
        Assert(mRect.contains(*pRect));
    }
    else
    {
        pRect = &mRect;
    }

    Assert(glIsTexture(mTexture));
    VBOXQGL_CHECKERR(
            glBindTexture(tt, mTexture);
            );

    int x = pRect->x()/mColorFormat.widthCompression();
    int y = pRect->y()/mColorFormat.heightCompression();
    int width = pRect->width()/mColorFormat.widthCompression();
    int height = pRect->height()/mColorFormat.heightCompression();

    uchar * address = pAddress + pointOffsetTex(x, y);

    VBOXQGL_CHECKERR(
            glPixelStorei(GL_UNPACK_ROW_LENGTH, mRect.width()/mColorFormat.widthCompression());
            );

    VBOXQGL_CHECKERR(
            glTexSubImage2D(tt,
                0,
                x, y, width, height,
                mColorFormat.format(),
                mColorFormat.type(),
                address);
            );
}

void VBoxVHWATexture::texCoord(int x, int y)
{
    glTexCoord2f(((float)x)/mTexRect.width()/mColorFormat.widthCompression(), ((float)y)/mTexRect.height()/mColorFormat.heightCompression());
}

void VBoxVHWATexture::multiTexCoord(GLenum texUnit, int x, int y)
{
    vboxglMultiTexCoord2f(texUnit, ((float)x)/mTexRect.width()/mColorFormat.widthCompression(), ((float)y)/mTexRect.height()/mColorFormat.heightCompression());
}

void VBoxVHWATexture::uninit()
{
    if(mTexture)
    {
        glDeleteTextures(1,&mTexture);
    }
}

VBoxVHWATexture::VBoxVHWATexture(const QRect & aRect, const VBoxVHWAColorFormat &aFormat, GLint scaleFuncttion) :
            mAddress(NULL),
            mTexture(0),
            mBytesPerPixel(0),
            mBytesPerPixelTex(0),
            mBytesPerLine(0),
            mScaleFuncttion(scaleFuncttion)
{
    mColorFormat = aFormat;
    mRect = aRect;
    mBytesPerPixel = mColorFormat.bitsPerPixel()/8;
    mBytesPerPixelTex = mColorFormat.bitsPerPixelTex()/8;
    mBytesPerLine = mBytesPerPixel * mRect.width();
    GLsizei wdt = VBoxVHWASurfaceBase::makePowerOf2(mRect.width()/mColorFormat.widthCompression());
    GLsizei hgt = VBoxVHWASurfaceBase::makePowerOf2(mRect.height()/mColorFormat.heightCompression());
    mTexRect = QRect(0,0,wdt,hgt);
}

void VBoxVHWATexture::initParams()
{
    GLenum tt = texTarget();

    glTexParameteri(tt, GL_TEXTURE_MIN_FILTER, mScaleFuncttion);
    VBOXQGL_ASSERTNOERR();
    glTexParameteri(tt, GL_TEXTURE_MAG_FILTER, mScaleFuncttion);
    VBOXQGL_ASSERTNOERR();
    glTexParameteri(tt, GL_TEXTURE_WRAP_S, GL_CLAMP);
    VBOXQGL_ASSERTNOERR();

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    VBOXQGL_ASSERTNOERR();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    VBOXQGL_ASSERTNOERR();

    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    VBOXQGL_ASSERTNOERR();
}

void VBoxVHWATexture::load()
{
    VBOXQGL_CHECKERR(
            glPixelStorei(GL_UNPACK_ROW_LENGTH, mTexRect.width());
            );

    VBOXQGL_CHECKERR(
        glTexImage2D(texTarget(),
                0,
                  mColorFormat.internalFormat(),
                  mTexRect.width(),
                  mTexRect.height(),
                  0,
                  mColorFormat.format(),
                  mColorFormat.type(),
                  (GLvoid *)mAddress);
        );
}

void VBoxVHWATexture::init(uchar *pvMem)
{
//    GLsizei wdt = mTexRect.width();
//    GLsizei hgt = mTexRect.height();

    VBOXQGL_CHECKERR(
            glGenTextures(1, &mTexture);
        );

    VBOXQGLLOG(("tex: %d", mTexture));

    bind();

    initParams();

    setAddress(pvMem);

    load();
}

VBoxVHWATexture::~VBoxVHWATexture()
{
    uninit();
}

void VBoxVHWATextureNP2Rect::texCoord(int x, int y)
{
    glTexCoord2i(x/mColorFormat.widthCompression(), y/mColorFormat.heightCompression());
}

void VBoxVHWATextureNP2Rect::multiTexCoord(GLenum texUnit, int x, int y)
{
    vboxglMultiTexCoord2i(texUnit, x/mColorFormat.widthCompression(), y/mColorFormat.heightCompression());
}

GLenum VBoxVHWATextureNP2Rect::texTarget() {return GL_TEXTURE_RECTANGLE; }

bool VBoxVHWASurfaceBase::synchTexMem(const QRect * pRect)
{
    if(pRect)
    {
        Assert(mRect.contains(*pRect));
    }

    if(mUpdateMem2TexRect.isClear())
        return false;

    if(pRect && !mUpdateMem2TexRect.rect().intersects(*pRect))
        return false;

#ifdef VBOXVHWA_PROFILE_FPS
    mpImage->reportNewFrame();
#endif

    mImage->update(&mUpdateMem2TexRect.rect());

    mUpdateMem2TexRect.clear();
    Assert(mUpdateMem2TexRect.isClear());

    return true;
}

void VBoxVHWATextureNP2RectPBO::init(uchar *pvMem)
{
    VBOXQGL_CHECKERR(
            vboxglGenBuffers(1, &mPBO);
            );
    VBoxVHWATextureNP2Rect::init(pvMem);
}

void VBoxVHWATextureNP2RectPBO::doUpdate(uchar * pAddress, const QRect * pRect)
{
    Q_UNUSED(pAddress);
    Q_UNUSED(pRect);

    vboxglBindBuffer(GL_PIXEL_UNPACK_BUFFER, mPBO);

    GLvoid *buf;

    VBOXQGL_CHECKERR(
            buf = vboxglMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
            );
    Assert(buf);
    if(buf)
    {
        memcpy(buf, mAddress, memSize());

        bool unmapped;
        VBOXQGL_CHECKERR(
                unmapped = vboxglUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
                );

        Assert(unmapped);

        VBoxVHWATextureNP2Rect::doUpdate(0, &mRect);

        vboxglBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }
    else
    {
        VBOXQGLLOGREL(("failed to map PBO, trying fallback to non-PBO approach\n"));
        /* try fallback to non-PBO approach */
        vboxglBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        VBoxVHWATextureNP2Rect::doUpdate(pAddress, pRect);
    }
}

VBoxVHWATextureNP2RectPBO::~VBoxVHWATextureNP2RectPBO()
{
    VBOXQGL_CHECKERR(
            vboxglDeleteBuffers(1, &mPBO);
            );
}


void VBoxVHWATextureNP2RectPBO::load()
{
    VBoxVHWATextureNP2Rect::load();

    VBOXQGL_CHECKERR(
            vboxglBindBuffer(GL_PIXEL_UNPACK_BUFFER, mPBO);
        );

    VBOXQGL_CHECKERR(
            vboxglBufferData(GL_PIXEL_UNPACK_BUFFER, memSize(), NULL, GL_STREAM_DRAW);
        );

    GLvoid *buf = vboxglMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
    Assert(buf);
    if(buf)
    {
        memcpy(buf, mAddress, memSize());

        bool unmapped = vboxglUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
        Assert(unmapped); NOREF(unmapped);
    }

    vboxglBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

uchar* VBoxVHWATextureNP2RectPBOMapped::mapAlignedBuffer()
{
    Assert(!mpMappedAllignedBuffer);
    if(!mpMappedAllignedBuffer)
    {
        VBOXQGL_CHECKERR(
                vboxglBindBuffer(GL_PIXEL_UNPACK_BUFFER, mPBO);
            );

        uchar* buf;
        VBOXQGL_CHECKERR(
                buf = (uchar*)vboxglMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_READ_WRITE);
        );

        Assert(buf);

        VBOXQGL_CHECKERR(
                vboxglBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            );

        mpMappedAllignedBuffer = (uchar*)alignBuffer(buf);

        mcbOffset = calcOffset(buf, mpMappedAllignedBuffer);
    }
    return mpMappedAllignedBuffer;
}

void VBoxVHWATextureNP2RectPBOMapped::unmapBuffer()
{
    Assert(mpMappedAllignedBuffer);
    if(mpMappedAllignedBuffer)
    {
        VBOXQGL_CHECKERR(
                vboxglBindBuffer(GL_PIXEL_UNPACK_BUFFER, mPBO);
        );

        bool unmapped;
        VBOXQGL_CHECKERR(
                unmapped = vboxglUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
                );

        Assert(unmapped);

        VBOXQGL_CHECKERR(
                vboxglBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        );

        mpMappedAllignedBuffer = NULL;
    }
}

void VBoxVHWATextureNP2RectPBOMapped::load()
{
    VBoxVHWATextureNP2Rect::load();

    VBOXQGL_CHECKERR(
            vboxglBindBuffer(GL_PIXEL_UNPACK_BUFFER, mPBO);
        );

    VBOXQGL_CHECKERR(
            vboxglBufferData(GL_PIXEL_UNPACK_BUFFER, mcbActualBufferSize, NULL, GL_STREAM_DRAW);
        );

    vboxglBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

void VBoxVHWATextureNP2RectPBOMapped::doUpdate(uchar * pAddress, const QRect * pRect)
{
    Q_UNUSED(pAddress);
    Q_UNUSED(pRect);

    VBOXQGL_CHECKERR(
            vboxglBindBuffer(GL_PIXEL_UNPACK_BUFFER, mPBO);
    );

    if(mpMappedAllignedBuffer)
    {
        bool unmapped;
        VBOXQGL_CHECKERR(
                unmapped = vboxglUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
                );

        Assert(unmapped);

        mpMappedAllignedBuffer = NULL;
    }

    VBoxVHWATextureNP2Rect::doUpdate((uchar *)mcbOffset, &mRect);

    VBOXQGL_CHECKERR(
            vboxglBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    );
}

int VBoxVHWASurfaceBase::lock(const QRect * pRect, uint32_t flags)
{
    Q_UNUSED(flags);

    if(pRect)
    {
        Assert(mRect.contains(*pRect));
    }

    Assert(mLockCount >= 0);
    if(pRect && pRect->isEmpty())
        return VERR_GENERAL_FAILURE;
    if(mLockCount < 0)
        return VERR_GENERAL_FAILURE;

    VBOXQGLLOG(("lock (0x%x)", this));
    VBOXQGLLOG_QRECT("rect: ", pRect ? pRect : &mRect, "\n");
    VBOXQGLLOG_METHODTIME("time ");

    mUpdateMem2TexRect.add(pRect ? *pRect : mRect);

    Assert(!mUpdateMem2TexRect.isClear());
    Assert(mRect.contains(mUpdateMem2TexRect.rect()));
    return VINF_SUCCESS;
}

int VBoxVHWASurfaceBase::unlock()
{
    VBOXQGLLOG(("unlock (0x%x)\n", this));
    mLockCount = 0;
    return VINF_SUCCESS;
}

void VBoxVHWASurfaceBase::setRectValues (const QRect & aTargRect, const QRect & aSrcRect)
{
    mTargRect = aTargRect;
    mSrcRect = aSrcRect;
}

void VBoxVHWASurfaceBase::setVisibleRectValues (const QRect & aVisTargRect)
{
    mVisibleTargRect = aVisTargRect.intersected(mTargRect);
    if(mVisibleTargRect.isEmpty() || mTargRect.isEmpty())
    {
        mVisibleSrcRect.setSize(QSize(0, 0));
    }
    else
    {
        float stretchX = float(mSrcRect.width()) / mTargRect.width();
        float stretchY = float(mSrcRect.height()) / mTargRect.height();
        int tx1, tx2, ty1, ty2, vtx1, vtx2, vty1, vty2;
        int sx1, sx2, sy1, sy2;
        mVisibleTargRect.getCoords(&vtx1, &vty1, &vtx2, &vty2);
        mTargRect.getCoords(&tx1, &ty1, &tx2, &ty2);
        mSrcRect.getCoords(&sx1, &sy1, &sx2, &sy2);
        int dx1 = vtx1 - tx1;
        int dy1 = vty1 - ty1;
        int dx2 = vtx2 - tx2;
        int dy2 = vty2 - ty2;
        int vsx1, vsy1, vsx2, vsy2;
        Assert(dx1 >= 0);
        Assert(dy1 >= 0);
        Assert(dx2 <= 0);
        Assert(dy2 <= 0);
        vsx1 = sx1 + int(dx1*stretchX);
        vsy1 = sy1 + int(dy1*stretchY);
        vsx2 = sx2 + int(dx2*stretchX);
        vsy2 = sy2 + int(dy2*stretchY);
        mVisibleSrcRect.setCoords(vsx1, vsy1, vsx2, vsy2);
        Assert(!mVisibleSrcRect.isEmpty());
        Assert(mSrcRect.contains(mVisibleSrcRect));
    }
}


void VBoxVHWASurfaceBase::setRects(const QRect & aTargRect, const QRect & aSrcRect)
{
    if(mTargRect != aTargRect || mSrcRect != aSrcRect)
    {
        setRectValues(aTargRect, aSrcRect);
    }
}

void VBoxVHWASurfaceBase::setTargRectPosition(const QPoint & aPoint)
{
    QRect tRect = targRect();
    tRect.moveTopLeft(aPoint);
    setRects(tRect, srcRect());
}

void VBoxVHWASurfaceBase::updateVisibility (VBoxVHWASurfaceBase *pPrimary, const QRect & aVisibleTargRect, bool bNotIntersected, bool bForce)
{
    if(bForce || aVisibleTargRect.intersected(mTargRect) != mVisibleTargRect)
    {
        setVisibleRectValues(aVisibleTargRect);
    }

    mpPrimary = pPrimary;
    mbNotIntersected = bNotIntersected;

    initDisplay();
}

void VBoxVHWASurfaceBase::initDisplay()
{
    if(mVisibleTargRect.isEmpty() || mVisibleSrcRect.isEmpty())
    {
        Assert(mVisibleTargRect.isEmpty() && mVisibleSrcRect.isEmpty());
        mImage->deleteDisplay();
        return;
    }

    int rc = mImage->initDisplay(mpPrimary ? mpPrimary->mImage : NULL, &mVisibleTargRect, &mVisibleSrcRect, getActiveDstOverlayCKey(mpPrimary), getActiveSrcOverlayCKey(), mbNotIntersected);
    AssertRC(rc);
}

void VBoxVHWASurfaceBase::updatedMem(const QRect *rec)
{
    if(rec)
    {
        Assert(mRect.contains(*rec));
    }
    mUpdateMem2TexRect.add(*rec);
}

bool VBoxVHWASurfaceBase::performDisplay(VBoxVHWASurfaceBase *pPrimary, bool bForce)
{
    Assert(mImage->displayInitialized());

    if(mVisibleTargRect.isEmpty())
    {
        /* nothing to display, i.e. the surface is not visible,
         * in the sense that it's located behind the viewport ranges */
        Assert(mVisibleSrcRect.isEmpty());
        return false;
    }
    else
    {
        Assert(!mVisibleSrcRect.isEmpty());
    }

    bForce |= synchTexMem(&mVisibleSrcRect);

    const VBoxVHWAColorKey * pDstCKey = getActiveDstOverlayCKey(pPrimary);
    if(pPrimary && pDstCKey)
    {
        bForce |= pPrimary->synchTexMem(&mVisibleTargRect);
    }

    if(!bForce)
        return false;

    mImage->display();

    Assert(bForce);
    return true;
}

class VBoxVHWAGlProgramMngr * VBoxVHWASurfaceBase::getGlProgramMngr()
{
    return mpImage->vboxVHWAGetGlProgramMngr();
}

class VBoxGLContext : public QGLContext
{
public:
    VBoxGLContext (const QGLFormat & format ) :
        QGLContext(format),
        mAllowDoneCurrent(true)
    {
    }

    void doneCurrent()
    {
        if(!mAllowDoneCurrent)
            return;
        QGLContext::doneCurrent();
    }

    bool isDoneCurrentAllowed() { return mAllowDoneCurrent; }
    void allowDoneCurrent(bool bAllow) { mAllowDoneCurrent = bAllow; }
private:
    bool mAllowDoneCurrent;
};


VBoxGLWgt::VBoxGLWgt(VBoxVHWAImage * pImage,
            QWidget* parent, const QGLWidget* shareWidget)

        : QGLWidget(new VBoxGLContext(shareWidget->format()), parent, shareWidget),
          mpImage(pImage)
{
    /* work-around to disable done current needed to old ATI drivers on Linux */
    VBoxGLContext *pc = (VBoxGLContext*)context();
    pc->allowDoneCurrent (false);
    Assert(isSharing());
}


VBoxVHWAImage::VBoxVHWAImage ()
    : mSurfHandleTable(128), /* 128 should be enough */
    mRepaintNeeded(false),
//    mbVGASurfCreated(false),
    mConstructingList(NULL),
    mcRemaining2Contruct(0),
    mSettings(NULL)
#ifdef VBOXVHWA_PROFILE_FPS
    ,
    mFPSCounter(64),
    mbNewFrame(false)
#endif
{
    mpMngr = new VBoxVHWAGlProgramMngr();
//        /* No need for background drawing */
//        setAttribute (Qt::WA_OpaquePaintEvent);
}

int VBoxVHWAImage::init(VBoxVHWASettings *aSettings)
{
    mSettings = aSettings;
    return VINF_SUCCESS;
}

const QGLFormat & VBoxVHWAImage::vboxGLFormat()
{
    static QGLFormat vboxFormat = QGLFormat();
    vboxFormat.setAlpha(true);
    Assert(vboxFormat.alpha());
    vboxFormat.setSwapInterval(0);
    Assert(vboxFormat.swapInterval() == 0);
    vboxFormat.setAccum(false);
    Assert(!vboxFormat.accum());
    vboxFormat.setDepth(false);
    Assert(!vboxFormat.depth());
//  vboxFormat.setRedBufferSize(8);
//  vboxFormat.setGreenBufferSize(8);
//  vboxFormat.setBlueBufferSize(8);
    return vboxFormat;
}


VBoxVHWAImage::~VBoxVHWAImage()
{
    delete mpMngr;
}

#ifdef VBOXVHWA_OLD_COORD
void VBoxVHWAImage::doSetupMatrix(const QSize & aSize, bool bInverted)
{
    VBOXQGL_CHECKERR(
            glLoadIdentity();
            );
    if(bInverted)
    {
        VBOXQGL_CHECKERR(
                glScalef(1.0f/aSize.width(), 1.0f/aSize.height(), 1.0f);
                );
    }
    else
    {
        /* make display coordinates be scaled to pixel coordinates */
        VBOXQGL_CHECKERR(
                glTranslatef(0.0f, 1.0f, 0.0f);
                );
        VBOXQGL_CHECKERR(
                glScalef(1.0f/aSize.width(), 1.0f/aSize.height(), 1.0f);
                );
        VBOXQGL_CHECKERR(
                glRotatef(180.0f, 1.0f, 0.0f, 0.0f);
                );
    }
}
#endif

void VBoxVHWAImage::adjustViewport(const QSize &display, const QRect &viewport)
{
    glViewport(-viewport.x(),
               viewport.height() + viewport.y() - display.height(),
               display.width(),
               display.height());
}

void VBoxVHWAImage::setupMatricies(const QSize &display, bool bInverted)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    if(bInverted)
        glOrtho(0., (GLdouble)display.width(), (GLdouble)display.height(), 0., -1., 1.);
    else
        glOrtho(0., (GLdouble)display.width(), 0., (GLdouble)display.height(), -1., 1.);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

int VBoxVHWAImage::reset(VHWACommandList * pCmdList)
{
    VBOXVHWACMD * pCmd;
    const OverlayList & overlays = mDisplay.overlays();
    for (OverlayList::const_iterator oIt = overlays.begin();
            oIt != overlays.end(); ++ oIt)
    {
        VBoxVHWASurfList * pSurfList = *oIt;
        if(pSurfList->current())
        {
            /* 1. hide overlay */
            pCmd = vhwaHHCmdCreate(VBOXVHWACMD_TYPE_SURF_OVERLAY_UPDATE, sizeof(VBOXVHWACMD_SURF_OVERLAY_UPDATE));
            VBOXVHWACMD_SURF_OVERLAY_UPDATE *pOUCmd = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_OVERLAY_UPDATE);
            pOUCmd->u.in.hSrcSurf = pSurfList->current()->handle();
            pOUCmd->u.in.flags = VBOXVHWA_OVER_HIDE;

            pCmdList->push_back(pCmd);
        }

        /* 2. destroy overlay */
        const SurfList & surfaces = pSurfList->surfaces();

        for (SurfList::const_iterator sIt = surfaces.begin();
                sIt != surfaces.end(); ++ sIt)
        {
            VBoxVHWASurfaceBase *pCurSurf = (*sIt);
            pCmd = vhwaHHCmdCreate(VBOXVHWACMD_TYPE_SURF_DESTROY, sizeof(VBOXVHWACMD_SURF_DESTROY));
            VBOXVHWACMD_SURF_DESTROY *pSDCmd = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_DESTROY);
            pSDCmd->u.in.hSurf = pCurSurf->handle();

            pCmdList->push_back(pCmd);
        }
    }

    /* 3. destroy primaries */
    const SurfList & surfaces = mDisplay.primaries().surfaces();
    for (SurfList::const_iterator sIt = surfaces.begin();
            sIt != surfaces.end(); ++ sIt)
    {
        VBoxVHWASurfaceBase *pCurSurf = (*sIt);
        if(pCurSurf->handle() != VBOXVHWA_SURFHANDLE_INVALID)
        {
            pCmd = vhwaHHCmdCreate(VBOXVHWACMD_TYPE_SURF_DESTROY, sizeof(VBOXVHWACMD_SURF_DESTROY));
            VBOXVHWACMD_SURF_DESTROY *pSDCmd = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_DESTROY);
            pSDCmd->u.in.hSurf = pCurSurf->handle();

            pCmdList->push_back(pCmd);
        }
    }

    return VINF_SUCCESS;
}

#ifdef VBOX_WITH_VIDEOHWACCEL
int VBoxVHWAImage::vhwaSurfaceCanCreate(struct _VBOXVHWACMD_SURF_CANCREATE *pCmd)
{
    VBOXQGLLOG_ENTER(("\n"));

    const VBoxVHWAInfo & info = vboxVHWAGetSupportInfo(NULL);

    if(!(pCmd->SurfInfo.flags & VBOXVHWA_SD_CAPS))
    {
        Assert(0);
        pCmd->u.out.ErrInfo = -1;
        return VINF_SUCCESS;
    }
#ifdef VBOXVHWA_ALLOW_PRIMARY_AND_OVERLAY_ONLY
    if(pCmd->SurfInfo.surfCaps & VBOXVHWA_SCAPS_OFFSCREENPLAIN)
    {
#ifdef DEBUGVHWASTRICT
        Assert(0);
#endif
        pCmd->u.out.ErrInfo = -1;
        return VINF_SUCCESS;
    }
#endif

    if(pCmd->SurfInfo.surfCaps & VBOXVHWA_SCAPS_PRIMARYSURFACE)
    {
        if(pCmd->SurfInfo.surfCaps & VBOXVHWA_SCAPS_COMPLEX)
        {
#ifdef DEBUG_misha
            Assert(0);
#endif
            pCmd->u.out.ErrInfo = -1;
        }
        else
        {
            pCmd->u.out.ErrInfo = 0;
        }
        return VINF_SUCCESS;
    }

#ifdef VBOXVHWA_ALLOW_PRIMARY_AND_OVERLAY_ONLY
    if ((pCmd->SurfInfo.surfCaps & VBOXVHWA_SCAPS_OVERLAY) == 0)
    {
#ifdef DEBUGVHWASTRICT
        Assert (0);
#endif
        pCmd->u.out.ErrInfo = -1;
        return VINF_SUCCESS;
    }
#endif

    if (pCmd->u.in.bIsDifferentPixelFormat)
    {
        if (!(pCmd->SurfInfo.flags & VBOXVHWA_SD_PIXELFORMAT))
        {
            Assert (0);
            pCmd->u.out.ErrInfo = -1;
            return VINF_SUCCESS;
        }

        if (pCmd->SurfInfo.PixelFormat.flags & VBOXVHWA_PF_RGB)
        {
            if (pCmd->SurfInfo.PixelFormat.c.rgbBitCount != 32
                    || pCmd->SurfInfo.PixelFormat.c.rgbBitCount != 24)
            {
                Assert (0);
                pCmd->u.out.ErrInfo = -1;
                return VINF_SUCCESS;
            }
        }
        else if (pCmd->SurfInfo.PixelFormat.flags & VBOXVHWA_PF_FOURCC)
        {
            /* detect whether we support this format */
            bool bFound = mSettings->isSupported (info, pCmd->SurfInfo.PixelFormat.fourCC);

            if (!bFound)
            {
                VBOXQGLLOG (("!!unsupported fourcc!!!: %c%c%c%c\n",
                             (pCmd->SurfInfo.PixelFormat.fourCC & 0x000000ff),
                             (pCmd->SurfInfo.PixelFormat.fourCC & 0x0000ff00) >> 8,
                             (pCmd->SurfInfo.PixelFormat.fourCC & 0x00ff0000) >> 16,
                             (pCmd->SurfInfo.PixelFormat.fourCC & 0xff000000) >> 24
                             ));
                pCmd->u.out.ErrInfo = -1;
                return VINF_SUCCESS;
            }
        }
        else
        {
            Assert (0);
            pCmd->u.out.ErrInfo = -1;
            return VINF_SUCCESS;
        }
    }

    pCmd->u.out.ErrInfo = 0;
    return VINF_SUCCESS;
}

int VBoxVHWAImage::vhwaSurfaceCreate (struct _VBOXVHWACMD_SURF_CREATE *pCmd)
{
    VBOXQGLLOG_ENTER (("\n"));

    uint32_t handle = VBOXVHWA_SURFHANDLE_INVALID;
    if(pCmd->SurfInfo.hSurf != VBOXVHWA_SURFHANDLE_INVALID)
    {
        handle = pCmd->SurfInfo.hSurf;
        if(mSurfHandleTable.get(handle))
        {
            Assert(0);
            return VERR_GENERAL_FAILURE;
        }
    }

    VBoxVHWASurfaceBase *surf = NULL;
    /* in case the Framebuffer is working in "not using VRAM" mode,
     * we need to report the pitch, etc. info of the form guest expects from us*/
    VBoxVHWAColorFormat reportedFormat;
    /* paranoya to ensure the VBoxVHWAColorFormat API works properly */
    Assert(!reportedFormat.isValid());
    bool bNoPBO = false;
    bool bPrimary = false;

    VBoxVHWAColorKey *pDstBltCKey = NULL, DstBltCKey;
    VBoxVHWAColorKey *pSrcBltCKey = NULL, SrcBltCKey;
    VBoxVHWAColorKey *pDstOverlayCKey = NULL, DstOverlayCKey;
    VBoxVHWAColorKey *pSrcOverlayCKey = NULL, SrcOverlayCKey;
    if(pCmd->SurfInfo.flags & VBOXVHWA_SD_CKDESTBLT)
    {
        DstBltCKey = VBoxVHWAColorKey(pCmd->SurfInfo.DstBltCK.high, pCmd->SurfInfo.DstBltCK.low);
        pDstBltCKey = &DstBltCKey;
    }
    if(pCmd->SurfInfo.flags & VBOXVHWA_SD_CKSRCBLT)
    {
        SrcBltCKey = VBoxVHWAColorKey(pCmd->SurfInfo.SrcBltCK.high, pCmd->SurfInfo.SrcBltCK.low);
        pSrcBltCKey = &SrcBltCKey;
    }
    if(pCmd->SurfInfo.flags & VBOXVHWA_SD_CKDESTOVERLAY)
    {
        DstOverlayCKey = VBoxVHWAColorKey(pCmd->SurfInfo.DstOverlayCK.high, pCmd->SurfInfo.DstOverlayCK.low);
        pDstOverlayCKey = &DstOverlayCKey;
    }
    if(pCmd->SurfInfo.flags & VBOXVHWA_SD_CKSRCOVERLAY)
    {
        SrcOverlayCKey = VBoxVHWAColorKey(pCmd->SurfInfo.SrcOverlayCK.high, pCmd->SurfInfo.SrcOverlayCK.low);
        pSrcOverlayCKey = &SrcOverlayCKey;
    }

    if (pCmd->SurfInfo.surfCaps & VBOXVHWA_SCAPS_PRIMARYSURFACE)
    {
        bNoPBO = true;
        bPrimary = true;
        VBoxVHWASurfaceBase * pVga = vgaSurface();
#ifdef VBOX_WITH_WDDM
        uchar * addr = vboxVRAMAddressFromOffset(pCmd->SurfInfo.offSurface);
        Assert(addr);
        if (addr)
        {
            pVga->setAddress(addr);
        }
#endif

        Assert((pCmd->SurfInfo.surfCaps & VBOXVHWA_SCAPS_OFFSCREENPLAIN) == 0);

        reportedFormat = VBoxVHWAColorFormat(pCmd->SurfInfo.PixelFormat.c.rgbBitCount,
                                    pCmd->SurfInfo.PixelFormat.m1.rgbRBitMask,
                                    pCmd->SurfInfo.PixelFormat.m2.rgbGBitMask,
                                    pCmd->SurfInfo.PixelFormat.m3.rgbBBitMask);

        if (pVga->handle() == VBOXVHWA_SURFHANDLE_INVALID
                && (pCmd->SurfInfo.surfCaps & VBOXVHWA_SCAPS_OFFSCREENPLAIN) == 0)
        {
            Assert(pCmd->SurfInfo.PixelFormat.flags & VBOXVHWA_PF_RGB);
//            if(pCmd->SurfInfo.PixelFormat.flags & VBOXVHWA_PF_RGB)
            {
                Assert(pCmd->SurfInfo.width == pVga->width());
                Assert(pCmd->SurfInfo.height == pVga->height());
//                if(pCmd->SurfInfo.width == pVga->width()
//                        && pCmd->SurfInfo.height == pVga->height())
                {
                    // the assert below is incorrect in case the Framebuffer is working in "not using VRAM" mode
//                    Assert(pVga->pixelFormat().equals(format));
//                    if(pVga->pixelFormat().equals(format))
                    {
                        surf = pVga;

                        surf->setDstBltCKey(pDstBltCKey);
                        surf->setSrcBltCKey(pSrcBltCKey);

                        surf->setDefaultDstOverlayCKey(pDstOverlayCKey);
                        surf->resetDefaultDstOverlayCKey();

                        surf->setDefaultSrcOverlayCKey(pSrcOverlayCKey);
                        surf->resetDefaultSrcOverlayCKey();
//                        mbVGASurfCreated = true;
                    }
                }
            }
        }
    }
    else if(pCmd->SurfInfo.surfCaps & VBOXVHWA_SCAPS_OFFSCREENPLAIN)
    {
        bNoPBO = true;
    }

    if(!surf)
    {
        VBOXVHWAIMG_TYPE fFlags = 0;
        if(!bNoPBO)
        {
            fFlags |= VBOXVHWAIMG_PBO | VBOXVHWAIMG_PBOIMG;
            if(mSettings->isStretchLinearEnabled())
                fFlags |= VBOXVHWAIMG_FBO;
        }

        QSize surfSize(pCmd->SurfInfo.width, pCmd->SurfInfo.height);
        QRect primaryRect = mDisplay.getPrimary()->rect();
        VBoxVHWAColorFormat format;
        if (bPrimary)
            format = mDisplay.getVGA()->pixelFormat();
        else if (pCmd->SurfInfo.PixelFormat.flags & VBOXVHWA_PF_RGB)
            format = VBoxVHWAColorFormat(pCmd->SurfInfo.PixelFormat.c.rgbBitCount,
                                            pCmd->SurfInfo.PixelFormat.m1.rgbRBitMask,
                                            pCmd->SurfInfo.PixelFormat.m2.rgbGBitMask,
                                            pCmd->SurfInfo.PixelFormat.m3.rgbBBitMask);
        else if (pCmd->SurfInfo.PixelFormat.flags & VBOXVHWA_PF_FOURCC)
            format = VBoxVHWAColorFormat(pCmd->SurfInfo.PixelFormat.fourCC);
        else
            AssertBreakpoint();

        if (format.isValid())
        {
            surf = new VBoxVHWASurfaceBase(this,
                        surfSize,
                        primaryRect,
                        QRect(0, 0, surfSize.width(), surfSize.height()),
                        mViewport,
                        format,
                        pSrcBltCKey, pDstBltCKey, pSrcOverlayCKey, pDstOverlayCKey,
#ifdef VBOXVHWA_USE_TEXGROUP
                        0,
#endif
                        fFlags);
        }
        else
        {
            AssertBreakpoint();
            VBOXQGLLOG_EXIT(("pSurf (0x%p)\n",surf));
            return VERR_GENERAL_FAILURE;
        }

        uchar * addr = vboxVRAMAddressFromOffset(pCmd->SurfInfo.offSurface);
        surf->init(mDisplay.getPrimary(), addr);

        if(pCmd->SurfInfo.surfCaps & VBOXVHWA_SCAPS_OVERLAY)
        {
#ifdef DEBUG_misha
            Assert(!bNoPBO);
#endif

            if(!mConstructingList)
            {
                mConstructingList = new VBoxVHWASurfList();
                mcRemaining2Contruct = pCmd->SurfInfo.cBackBuffers+1;
                mDisplay.addOverlay(mConstructingList);
            }

            mConstructingList->add(surf);
            mcRemaining2Contruct--;
            if(!mcRemaining2Contruct)
            {
                mConstructingList = NULL;
            }
        }
        else
        {
            VBoxVHWASurfaceBase * pVga = vgaSurface();
            Assert(pVga->handle() != VBOXVHWA_SURFHANDLE_INVALID);
            Assert(pVga != surf); NOREF(pVga);
            mDisplay.getVGA()->getComplexList()->add(surf);
#ifdef DEBUGVHWASTRICT
            Assert(pCmd->SurfInfo.surfCaps & VBOXVHWA_SCAPS_VISIBLE);
#endif
            if(bPrimary)
            {
                Assert(surf->getComplexList() == mDisplay.getVGA()->getComplexList());
                surf->getComplexList()->setCurrentVisible(surf);
                mDisplay.updateVGA(surf);
            }
        }
    }
    else
    {
        Assert(pCmd->SurfInfo.surfCaps & VBOXVHWA_SCAPS_PRIMARYSURFACE);
    }

    Assert(mDisplay.getVGA() == mDisplay.getPrimary());

    /* tell the guest how we think the memory is organized */
    VBOXQGLLOG(("bps: %d\n", surf->bitsPerPixel()));

    if (!reportedFormat.isValid())
    {
        pCmd->SurfInfo.pitch = surf->bitsPerPixel() * surf->width() / 8;
        pCmd->SurfInfo.sizeX = surf->memSize();
        pCmd->SurfInfo.sizeY = 1;
    }
    else
    {
        /* this is the case of Framebuffer not using Gueat VRAM */
        /* can happen for primary surface creation only */
        Assert(pCmd->SurfInfo.surfCaps & VBOXVHWA_SCAPS_PRIMARYSURFACE);
        pCmd->SurfInfo.pitch = (reportedFormat.bitsPerPixel() * surf->width() + 7) / 8;
        /* we support only RGB case now, otherwise we would need more complicated mechanism of memsize calculation */
        Assert(!reportedFormat.fourcc());
        pCmd->SurfInfo.sizeX = (reportedFormat.bitsPerPixel() * surf->width() + 7) / 8 * surf->height();
        pCmd->SurfInfo.sizeY = 1;
    }

    if(handle != VBOXVHWA_SURFHANDLE_INVALID)
    {
        bool bSuccess = mSurfHandleTable.mapPut(handle, surf);
        Assert(bSuccess);
        if(!bSuccess)
        {
            /* @todo: this is very bad, should not be here */
            return VERR_GENERAL_FAILURE;
        }
    }
    else
    {
        /* tell the guest our handle */
        handle = mSurfHandleTable.put(surf);
        pCmd->SurfInfo.hSurf = (VBOXVHWA_SURFHANDLE)handle;
    }

    Assert(handle != VBOXVHWA_SURFHANDLE_INVALID);
    Assert(surf->handle() == VBOXVHWA_SURFHANDLE_INVALID);
    surf->setHandle(handle);
    Assert(surf->handle() == handle);

    VBOXQGLLOG_EXIT(("pSurf (0x%p)\n",surf));

    return VINF_SUCCESS;
}

#ifdef VBOX_WITH_WDDM
int VBoxVHWAImage::vhwaSurfaceGetInfo(struct _VBOXVHWACMD_SURF_GETINFO *pCmd)
{
    VBoxVHWAColorFormat format;
    Assert(!format.isValid());
    if (pCmd->SurfInfo.PixelFormat.flags & VBOXVHWA_PF_RGB)
        format = VBoxVHWAColorFormat(pCmd->SurfInfo.PixelFormat.c.rgbBitCount,
                                        pCmd->SurfInfo.PixelFormat.m1.rgbRBitMask,
                                        pCmd->SurfInfo.PixelFormat.m2.rgbGBitMask,
                                        pCmd->SurfInfo.PixelFormat.m3.rgbBBitMask);
    else if (pCmd->SurfInfo.PixelFormat.flags & VBOXVHWA_PF_FOURCC)
        format = VBoxVHWAColorFormat(pCmd->SurfInfo.PixelFormat.fourCC);
    else
        AssertBreakpoint();

    Assert(format.isValid());
    if (format.isValid())
    {
        pCmd->SurfInfo.pitch = format.bitsPerPixel() * pCmd->SurfInfo.width / 8;
//        pCmd->SurfInfo.pitch = ((pCmd->SurfInfo.pitch + 3) & (~3));
        pCmd->SurfInfo.sizeX = format.bitsPerPixelMem() * pCmd->SurfInfo.width / 8;
//        pCmd->SurfInfo.sizeX = ((pCmd->SurfInfo.sizeX + 3) & (~3));
        pCmd->SurfInfo.sizeX *= pCmd->SurfInfo.height;
        pCmd->SurfInfo.sizeY = 1;
        return VINF_SUCCESS;
    }
    return VERR_INVALID_PARAMETER;
}
#endif
int VBoxVHWAImage::vhwaSurfaceDestroy(struct _VBOXVHWACMD_SURF_DESTROY *pCmd)
{
    VBoxVHWASurfaceBase *pSurf = handle2Surface(pCmd->u.in.hSurf);
    VBoxVHWASurfList *pList = pSurf->getComplexList();
    Assert(pSurf->handle() != VBOXVHWA_SURFHANDLE_INVALID);

    VBOXQGLLOG_ENTER(("pSurf (0x%x)\n",pSurf));
    if(pList != mDisplay.getVGA()->getComplexList())
    {
        Assert(pList);
        pList->remove(pSurf);
        if(pList->surfaces().empty())
        {
            mDisplay.removeOverlay(pList);
            if(pList == mConstructingList)
            {
                mConstructingList = NULL;
                mcRemaining2Contruct = 0;
            }
            delete pList;
        }

        delete(pSurf);
    }
    else
    {
        Assert(pList->size() >= 1);
        if(pList->size() > 1)
        {
            if(pSurf == mDisplay.getVGA())
            {
                const SurfList & surfaces = pList->surfaces();

                for (SurfList::const_iterator it = surfaces.begin();
                         it != surfaces.end(); ++ it)
                {
                    VBoxVHWASurfaceBase *pCurSurf = (*it);
                    Assert(pCurSurf);
                    if (pCurSurf != pSurf)
                    {
                        mDisplay.updateVGA(pCurSurf);
                        pList->setCurrentVisible(pCurSurf);
                        break;
                    }
                }
            }

            pList->remove(pSurf);
            delete(pSurf);
        }
        else
        {
            pSurf->setHandle(VBOXVHWA_SURFHANDLE_INVALID);
        }
    }

    /* just in case we destroy a visible overlay sorface */
    mRepaintNeeded = true;

    void * test = mSurfHandleTable.remove(pCmd->u.in.hSurf);
    Assert(test); NOREF(test);

    return VINF_SUCCESS;
}

#define VBOXVHWA_CONSTRUCT_QRECT_FROM_RECTL_RB(_pr) \
             QRect((_pr)->left,                     \
                 (_pr)->top,                        \
                 (_pr)->right - (_pr)->left + 1,    \
                 (_pr)->bottom - (_pr)->top + 1)

#define VBOXVHWA_CONSTRUCT_QRECT_FROM_RECTL_WH(_pr) \
             QRect((_pr)->left,                     \
                 (_pr)->top,                        \
                 (_pr)->right - (_pr)->left,        \
                 (_pr)->bottom - (_pr)->top)

int VBoxVHWAImage::vhwaSurfaceLock(struct _VBOXVHWACMD_SURF_LOCK *pCmd)
{
    VBoxVHWASurfaceBase *pSurf = handle2Surface(pCmd->u.in.hSurf);
    VBOXQGLLOG_ENTER(("pSurf (0x%x)\n",pSurf));
    vboxCheckUpdateAddress (pSurf, pCmd->u.in.offSurface);
    if (pCmd->u.in.rectValid)
    {
        QRect r = VBOXVHWA_CONSTRUCT_QRECT_FROM_RECTL_WH(&pCmd->u.in.rect);
        return pSurf->lock(&r, pCmd->u.in.flags);
    }
    return pSurf->lock(NULL, pCmd->u.in.flags);
}

int VBoxVHWAImage::vhwaSurfaceUnlock(struct _VBOXVHWACMD_SURF_UNLOCK *pCmd)
{
    VBoxVHWASurfaceBase *pSurf = handle2Surface(pCmd->u.in.hSurf);
#ifdef DEBUG_misha
    /* for performance reasons we should receive unlock for visible surfaces only
     * other surfaces receive unlock only once becoming visible, e.g. on DdFlip
     * Ensure this is so*/
    if(pSurf != mDisplay.getPrimary())
    {
        const OverlayList & overlays = mDisplay.overlays();
        bool bFound = false;

        if(!mDisplay.isPrimary(pSurf))
        {
            for (OverlayList::const_iterator it = overlays.begin();
                 it != overlays.end(); ++ it)
            {
                VBoxVHWASurfList * pSurfList = *it;
                if(pSurfList->current() == pSurf)
                {
                    bFound = true;
                    break;
                }
            }

            Assert(bFound);
        }

//        Assert(bFound);
    }
#endif
    VBOXQGLLOG_ENTER(("pSurf (0x%x)\n",pSurf));
    if(pCmd->u.in.xUpdatedMemValid)
    {
        QRect r = VBOXVHWA_CONSTRUCT_QRECT_FROM_RECTL_WH(&pCmd->u.in.xUpdatedMemRect);
        pSurf->updatedMem(&r);
    }

    return pSurf->unlock();
}

int VBoxVHWAImage::vhwaSurfaceBlt(struct _VBOXVHWACMD_SURF_BLT *pCmd)
{
    Q_UNUSED(pCmd);
    return VERR_NOT_IMPLEMENTED;
}

int VBoxVHWAImage::vhwaSurfaceFlip(struct _VBOXVHWACMD_SURF_FLIP *pCmd)
{
    VBoxVHWASurfaceBase *pTargSurf = handle2Surface(pCmd->u.in.hTargSurf);
    VBoxVHWASurfaceBase *pCurrSurf = handle2Surface(pCmd->u.in.hCurrSurf);
    VBOXQGLLOG_ENTER(("pTargSurf (0x%x), pCurrSurf (0x%x)\n",pTargSurf,pCurrSurf));
    vboxCheckUpdateAddress (pCurrSurf, pCmd->u.in.offCurrSurface);
    vboxCheckUpdateAddress (pTargSurf, pCmd->u.in.offTargSurface);

    if(pCmd->u.in.xUpdatedTargMemValid)
    {
        QRect r = VBOXVHWA_CONSTRUCT_QRECT_FROM_RECTL_WH(&pCmd->u.in.xUpdatedTargMemRect);
        pTargSurf->updatedMem(&r);
    }
    pTargSurf->getComplexList()->setCurrentVisible(pTargSurf);

    mRepaintNeeded = true;
#ifdef DEBUG
    pCurrSurf->cFlipsCurr++;
    pTargSurf->cFlipsTarg++;
#endif

    return VINF_SUCCESS;
}

int VBoxVHWAImage::vhwaSurfaceColorFill(struct _VBOXVHWACMD_SURF_COLORFILL *pCmd)
{
    return VERR_NOT_IMPLEMENTED;
}

void VBoxVHWAImage::vhwaDoSurfaceOverlayUpdate(VBoxVHWASurfaceBase *pDstSurf, VBoxVHWASurfaceBase *pSrcSurf, struct _VBOXVHWACMD_SURF_OVERLAY_UPDATE *pCmd)
{
    if(pCmd->u.in.flags & VBOXVHWA_OVER_KEYDEST)
    {
        VBOXQGLLOG((", KEYDEST"));
        /* we use src (overlay) surface to maintain overridden dst ckey info
         * to allow multiple overlays have different overridden dst keys for one primary surface */
        /* non-null dstOverlayCKey for overlay would mean the overlay surface contains the overridden
         * dst ckey value in defaultDstOverlayCKey
         * this allows the NULL to be a valid overridden value as well
         *  i.e.
         * 1. indicate the value is NUL overridden, just set NULL*/
        pSrcSurf->setOverriddenDstOverlayCKey(NULL);
    }
    else if(pCmd->u.in.flags & VBOXVHWA_OVER_KEYDESTOVERRIDE)
    {
        VBOXQGLLOG((", KEYDESTOVERRIDE"));
        /* we use src (overlay) surface to maintain overridden dst ckey info
         * to allow multiple overlays have different overridden dst keys for one primary surface */
        /* non-null dstOverlayCKey for overlay would mean the overlay surface contains the overridden
         * dst ckey value in defaultDstOverlayCKey
         * this allows the NULL to be a valid overridden value as well
         *  i.e.
         * 1. indicate the value is overridden (no matter what we write here, bu it should be not NULL)*/
        VBoxVHWAColorKey ckey(pCmd->u.in.desc.DstCK.high, pCmd->u.in.desc.DstCK.low);
        VBOXQGLLOG_CKEY(" ckey: ",&ckey, "\n");
        pSrcSurf->setOverriddenDstOverlayCKey(&ckey);
        /* tell the ckey is enabled */
        pSrcSurf->setDefaultDstOverlayCKey(&ckey);
    }
    else
    {
        VBOXQGLLOG((", no KEYDEST"));
        /* we use src (overlay) surface to maintain overridden dst ckey info
         * to allow multiple overlays have different overridden dst keys for one primary surface */
        /* non-null dstOverlayCKey for overlay would mean the overlay surface contains the overridden
         * dst ckey value in defaultDstOverlayCKey
         * this allows the NULL to be a valid overridden value as well
         * i.e.
         * 1. indicate the value is overridden (no matter what we write here, bu it should be not NULL)*/
        VBoxVHWAColorKey dummyCKey(0, 0);
        pSrcSurf->setOverriddenDstOverlayCKey(&dummyCKey);
        /* tell the ckey is disabled */
        pSrcSurf->setDefaultDstOverlayCKey(NULL);
    }

    if(pCmd->u.in.flags & VBOXVHWA_OVER_KEYSRC)
    {
        VBOXQGLLOG((", KEYSRC"));
        pSrcSurf->resetDefaultSrcOverlayCKey();
    }
    else if(pCmd->u.in.flags & VBOXVHWA_OVER_KEYSRCOVERRIDE)
    {
        VBOXQGLLOG((", KEYSRCOVERRIDE"));
        VBoxVHWAColorKey ckey(pCmd->u.in.desc.SrcCK.high, pCmd->u.in.desc.SrcCK.low);
        pSrcSurf->setOverriddenSrcOverlayCKey(&ckey);
    }
    else
    {
        VBOXQGLLOG((", no KEYSRC"));
        pSrcSurf->setOverriddenSrcOverlayCKey(NULL);
    }
    VBOXQGLLOG(("\n"));
    if(pDstSurf)
    {
        QRect dstRect = VBOXVHWA_CONSTRUCT_QRECT_FROM_RECTL_WH(&pCmd->u.in.dstRect);
        QRect srcRect = VBOXVHWA_CONSTRUCT_QRECT_FROM_RECTL_WH(&pCmd->u.in.srcRect);

        VBOXQGLLOG(("*******overlay update*******\n"));
        VBOXQGLLOG(("dstSurfSize: w(%d), h(%d)\n", pDstSurf->width(), pDstSurf->height()));
        VBOXQGLLOG(("srcSurfSize: w(%d), h(%d)\n", pSrcSurf->width(), pSrcSurf->height()));
        VBOXQGLLOG_QRECT("dstRect:", &dstRect, "\n");
        VBOXQGLLOG_QRECT("srcRect:", &srcRect, "\n");

        pSrcSurf->setPrimary(pDstSurf);

        pSrcSurf->setRects(dstRect, srcRect);
    }
}

int VBoxVHWAImage::vhwaSurfaceOverlayUpdate(struct _VBOXVHWACMD_SURF_OVERLAY_UPDATE *pCmd)
{
    VBoxVHWASurfaceBase *pSrcSurf = handle2Surface(pCmd->u.in.hSrcSurf);
    VBoxVHWASurfList *pList = pSrcSurf->getComplexList();
    vboxCheckUpdateAddress (pSrcSurf, pCmd->u.in.offSrcSurface);
    VBOXQGLLOG(("OverlayUpdate: pSrcSurf (0x%x)\n",pSrcSurf));
    VBoxVHWASurfaceBase *pDstSurf = NULL;

    if(pCmd->u.in.hDstSurf)
    {
        pDstSurf = handle2Surface(pCmd->u.in.hDstSurf);
        vboxCheckUpdateAddress (pDstSurf, pCmd->u.in.offDstSurface);
        VBOXQGLLOG(("pDstSurf (0x%x)\n",pDstSurf));
#ifdef DEBUGVHWASTRICT
        Assert(pDstSurf == mDisplay.getVGA());
        Assert(mDisplay.getVGA() == mDisplay.getPrimary());
#endif
        Assert(pDstSurf->getComplexList() == mDisplay.getVGA()->getComplexList());

        if(pCmd->u.in.flags & VBOXVHWA_OVER_SHOW)
        {
            if(pDstSurf != mDisplay.getPrimary())
            {
                mDisplay.updateVGA(pDstSurf);
                pDstSurf->getComplexList()->setCurrentVisible(pDstSurf);
            }
        }
    }

#ifdef VBOX_WITH_WDDM
    if(pCmd->u.in.xUpdatedSrcMemValid)
    {
        QRect r = VBOXVHWA_CONSTRUCT_QRECT_FROM_RECTL_WH(&pCmd->u.in.xUpdatedSrcMemRect);
        pSrcSurf->updatedMem(&r);
    }
#endif

    const SurfList & surfaces = pList->surfaces();

    for (SurfList::const_iterator it = surfaces.begin();
             it != surfaces.end(); ++ it)
    {
        VBoxVHWASurfaceBase *pCurSrcSurf = (*it);
        vhwaDoSurfaceOverlayUpdate(pDstSurf, pCurSrcSurf, pCmd);
    }

    if(pCmd->u.in.flags & VBOXVHWA_OVER_HIDE)
    {
        VBOXQGLLOG(("hide\n"));
        pList->setCurrentVisible(NULL);
    }
    else if(pCmd->u.in.flags & VBOXVHWA_OVER_SHOW)
    {
        VBOXQGLLOG(("show\n"));
        pList->setCurrentVisible(pSrcSurf);
    }

    mRepaintNeeded = true;

    return VINF_SUCCESS;
}

int VBoxVHWAImage::vhwaSurfaceOverlaySetPosition(struct _VBOXVHWACMD_SURF_OVERLAY_SETPOSITION *pCmd)
{
    VBoxVHWASurfaceBase *pDstSurf = handle2Surface(pCmd->u.in.hDstSurf);
    VBoxVHWASurfaceBase *pSrcSurf = handle2Surface(pCmd->u.in.hSrcSurf);

    VBOXQGLLOG_ENTER(("pDstSurf (0x%x), pSrcSurf (0x%x)\n",pDstSurf,pSrcSurf));

    vboxCheckUpdateAddress (pSrcSurf, pCmd->u.in.offSrcSurface);
    vboxCheckUpdateAddress (pDstSurf, pCmd->u.in.offDstSurface);

    VBoxVHWASurfList *pList = pSrcSurf->getComplexList();
    const SurfList & surfaces = pList->surfaces();

    QPoint pos(pCmd->u.in.xPos, pCmd->u.in.yPos);

#ifdef DEBUGVHWASTRICT
    Assert(pDstSurf == mDisplay.getVGA());
    Assert(mDisplay.getVGA() == mDisplay.getPrimary());
#endif
    if (pSrcSurf->getComplexList()->current() != NULL)
    {
        Assert(pDstSurf);
        if (pDstSurf != mDisplay.getPrimary())
        {
            mDisplay.updateVGA(pDstSurf);
            pDstSurf->getComplexList()->setCurrentVisible(pDstSurf);
        }
    }

    mRepaintNeeded = true;

    for (SurfList::const_iterator it = surfaces.begin();
             it != surfaces.end(); ++ it)
    {
        VBoxVHWASurfaceBase *pCurSrcSurf = (*it);
        pCurSrcSurf->setTargRectPosition(pos);
    }

    return VINF_SUCCESS;
}

int VBoxVHWAImage::vhwaSurfaceColorkeySet(struct _VBOXVHWACMD_SURF_COLORKEY_SET *pCmd)
{
    VBoxVHWASurfaceBase *pSurf = handle2Surface(pCmd->u.in.hSurf);

    VBOXQGLLOG_ENTER(("pSurf (0x%x)\n",pSurf));

    vboxCheckUpdateAddress (pSurf, pCmd->u.in.offSurface);

    mRepaintNeeded = true;

    if (pCmd->u.in.flags & VBOXVHWA_CKEY_DESTBLT)
    {
        VBoxVHWAColorKey ckey(pCmd->u.in.CKey.high, pCmd->u.in.CKey.low);
        pSurf->setDstBltCKey(&ckey);
    }
    if (pCmd->u.in.flags & VBOXVHWA_CKEY_DESTOVERLAY)
    {
        VBoxVHWAColorKey ckey(pCmd->u.in.CKey.high, pCmd->u.in.CKey.low);
        pSurf->setDefaultDstOverlayCKey(&ckey);
    }
    if (pCmd->u.in.flags & VBOXVHWA_CKEY_SRCBLT)
    {
        VBoxVHWAColorKey ckey(pCmd->u.in.CKey.high, pCmd->u.in.CKey.low);
        pSurf->setSrcBltCKey(&ckey);

    }
    if (pCmd->u.in.flags & VBOXVHWA_CKEY_SRCOVERLAY)
    {
        VBoxVHWAColorKey ckey(pCmd->u.in.CKey.high, pCmd->u.in.CKey.low);
        pSurf->setDefaultSrcOverlayCKey(&ckey);
    }

    return VINF_SUCCESS;
}

int VBoxVHWAImage::vhwaQueryInfo1(struct _VBOXVHWACMD_QUERYINFO1 *pCmd)
{
    VBOXQGLLOG_ENTER(("\n"));
    bool bEnabled = false;
    const VBoxVHWAInfo & info = vboxVHWAGetSupportInfo(NULL);
    if(info.isVHWASupported())
    {
        Assert(pCmd->u.in.guestVersion.maj == VBOXVHWA_VERSION_MAJ);
        if(pCmd->u.in.guestVersion.maj == VBOXVHWA_VERSION_MAJ)
        {
            Assert(pCmd->u.in.guestVersion.min == VBOXVHWA_VERSION_MIN);
            if(pCmd->u.in.guestVersion.min == VBOXVHWA_VERSION_MIN)
            {
                Assert(pCmd->u.in.guestVersion.bld == VBOXVHWA_VERSION_BLD);
                if(pCmd->u.in.guestVersion.bld == VBOXVHWA_VERSION_BLD)
                {
                    Assert(pCmd->u.in.guestVersion.reserved == VBOXVHWA_VERSION_RSV);
                    if(pCmd->u.in.guestVersion.reserved == VBOXVHWA_VERSION_RSV)
                    {
                        bEnabled = true;
                    }
                }
            }
        }
    }

    memset(pCmd, 0, sizeof(VBOXVHWACMD_QUERYINFO1));
    if(bEnabled)
    {
        pCmd->u.out.cfgFlags = VBOXVHWA_CFG_ENABLED;

        pCmd->u.out.caps =
                    /* we do not support blitting for now */
//                        VBOXVHWA_CAPS_BLT | VBOXVHWA_CAPS_BLTSTRETCH | VBOXVHWA_CAPS_BLTQUEUE
//                                 | VBOXVHWA_CAPS_BLTCOLORFILL not supported, although adding it is trivial
//                                 | VBOXVHWA_CAPS_BLTFOURCC set below if shader support is available
                                 VBOXVHWA_CAPS_OVERLAY
                                 | VBOXVHWA_CAPS_OVERLAYSTRETCH
                                 | VBOXVHWA_CAPS_OVERLAYCANTCLIP
                                 // | VBOXVHWA_CAPS_OVERLAYFOURCC set below if shader support is available
                                 ;

        /* @todo: check if we could use DDSCAPS_ALPHA instead of colorkeying */

        pCmd->u.out.caps2 = VBOXVHWA_CAPS2_CANRENDERWINDOWED
                                    | VBOXVHWA_CAPS2_WIDESURFACES;

        //TODO: setup stretchCaps
        pCmd->u.out.stretchCaps = 0;

        pCmd->u.out.numOverlays = 1;
        /* TODO: set curOverlays properly */
        pCmd->u.out.curOverlays = 0;

        pCmd->u.out.surfaceCaps =
                            VBOXVHWA_SCAPS_PRIMARYSURFACE
#ifndef VBOXVHWA_ALLOW_PRIMARY_AND_OVERLAY_ONLY
                            | VBOXVHWA_SCAPS_OFFSCREENPLAIN
#endif
                            | VBOXVHWA_SCAPS_FLIP
                            | VBOXVHWA_SCAPS_LOCALVIDMEM
                            | VBOXVHWA_SCAPS_OVERLAY
                    //        | VBOXVHWA_SCAPS_BACKBUFFER
                    //        | VBOXVHWA_SCAPS_FRONTBUFFER
                    //        | VBOXVHWA_SCAPS_VIDEOMEMORY
                    //        | VBOXVHWA_SCAPS_COMPLEX
                    //        | VBOXVHWA_SCAPS_VISIBLE
                            ;

        if(info.getGlInfo().isFragmentShaderSupported() && info.getGlInfo().getMultiTexNumSupported() >= 2)
        {
            pCmd->u.out.caps |= VBOXVHWA_CAPS_COLORKEY
                            | VBOXVHWA_CAPS_COLORKEYHWASSIST
                            ;

            pCmd->u.out.colorKeyCaps =
//                          VBOXVHWA_CKEYCAPS_DESTBLT | VBOXVHWA_CKEYCAPS_DESTBLTCLRSPACE | VBOXVHWA_CKEYCAPS_SRCBLT| VBOXVHWA_CKEYCAPS_SRCBLTCLRSPACE |
//                          VBOXVHWA_CKEYCAPS_SRCOVERLAY | VBOXVHWA_CKEYCAPS_SRCOVERLAYONEACTIVE |
                            VBOXVHWA_CKEYCAPS_DESTOVERLAY          |
                            VBOXVHWA_CKEYCAPS_DESTOVERLAYONEACTIVE;
                            ;

            if(info.getGlInfo().isTextureRectangleSupported())
            {
                pCmd->u.out.caps |= VBOXVHWA_CAPS_OVERLAYFOURCC
//                              | VBOXVHWA_CAPS_BLTFOURCC
                                ;

                pCmd->u.out.colorKeyCaps |=
//                               VBOXVHWA_CKEYCAPS_SRCOVERLAYYUV |
                                 VBOXVHWA_CKEYCAPS_DESTOVERLAYYUV;
                                 ;

//              pCmd->u.out.caps2 |= VBOXVHWA_CAPS2_COPYFOURCC;

                pCmd->u.out.numFourCC = mSettings->getIntersection(info, 0, NULL);
            }
        }
    }

    return VINF_SUCCESS;
}

int VBoxVHWAImage::vhwaQueryInfo2(struct _VBOXVHWACMD_QUERYINFO2 *pCmd)
{
    VBOXQGLLOG_ENTER(("\n"));

    const VBoxVHWAInfo & info = vboxVHWAGetSupportInfo(NULL);
    uint32_t aFourcc[VBOXVHWA_NUMFOURCC];
    int num = mSettings->getIntersection(info, VBOXVHWA_NUMFOURCC, aFourcc);
    Assert(pCmd->numFourCC >= (uint32_t)num);
    if(pCmd->numFourCC < (uint32_t)num)
        return VERR_GENERAL_FAILURE;

    pCmd->numFourCC = (uint32_t)num;
    memcpy(pCmd->FourCC, aFourcc, num*sizeof(aFourcc[0]));
    return VINF_SUCCESS;
}

//static DECLCALLBACK(void) vboxQGLSaveExec(PSSMHANDLE pSSM, void *pvUser)
//{
//    VBoxVHWAImage * pw = (VBoxVHWAImage*)pvUser;
//    pw->vhwaSaveExec(pSSM);
//}
//
//static DECLCALLBACK(int) vboxQGLLoadExec(PSSMHANDLE pSSM, void *pvUser, uint32_t u32Version, uint32_t uPass)
//{
//    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);
//    VBoxVHWAImage * pw = (VBoxVHWAImage*)pvUser;
//    return VBoxVHWAImage::vhwaLoadExec(&pw->onResizeCmdList(), pSSM, u32Version);
//}

int VBoxVHWAImage::vhwaSaveSurface(struct SSMHANDLE * pSSM, VBoxVHWASurfaceBase *pSurf, uint32_t surfCaps)
{
    VBOXQGL_SAVE_SURFSTART(pSSM);

    uint64_t u64 = vboxVRAMOffset(pSurf);
    int rc;
    rc = SSMR3PutU32(pSSM, pSurf->handle());         AssertRC(rc);
    rc = SSMR3PutU64(pSSM, u64);         AssertRC(rc);
    rc = SSMR3PutU32(pSSM, pSurf->width());         AssertRC(rc);
    rc = SSMR3PutU32(pSSM, pSurf->height());         AssertRC(rc);
    rc = SSMR3PutU32(pSSM, surfCaps);         AssertRC(rc);
    uint32_t flags = 0;
    const VBoxVHWAColorKey * pDstBltCKey = pSurf->dstBltCKey();
    const VBoxVHWAColorKey * pSrcBltCKey = pSurf->srcBltCKey();
    const VBoxVHWAColorKey * pDstOverlayCKey = pSurf->dstOverlayCKey();
    const VBoxVHWAColorKey * pSrcOverlayCKey = pSurf->srcOverlayCKey();
    if(pDstBltCKey)
    {
        flags |= VBOXVHWA_SD_CKDESTBLT;
    }
    if(pSrcBltCKey)
    {
        flags |= VBOXVHWA_SD_CKSRCBLT;
    }
    if(pDstOverlayCKey)
    {
        flags |= VBOXVHWA_SD_CKDESTOVERLAY;
    }
    if(pSrcOverlayCKey)
    {
        flags |= VBOXVHWA_SD_CKSRCOVERLAY;
    }

    rc = SSMR3PutU32(pSSM, flags);         AssertRC(rc);
    if(pDstBltCKey)
    {
        rc = SSMR3PutU32(pSSM, pDstBltCKey->lower());         AssertRC(rc);
        rc = SSMR3PutU32(pSSM, pDstBltCKey->upper());         AssertRC(rc);
    }
    if(pSrcBltCKey)
    {
        rc = SSMR3PutU32(pSSM, pSrcBltCKey->lower());         AssertRC(rc);
        rc = SSMR3PutU32(pSSM, pSrcBltCKey->upper());         AssertRC(rc);
    }
    if(pDstOverlayCKey)
    {
        rc = SSMR3PutU32(pSSM, pDstOverlayCKey->lower());         AssertRC(rc);
        rc = SSMR3PutU32(pSSM, pDstOverlayCKey->upper());         AssertRC(rc);
    }
    if(pSrcOverlayCKey)
    {
        rc = SSMR3PutU32(pSSM, pSrcOverlayCKey->lower());         AssertRC(rc);
        rc = SSMR3PutU32(pSSM, pSrcOverlayCKey->upper());         AssertRC(rc);
    }

    const VBoxVHWAColorFormat & format = pSurf->pixelFormat();
    flags = 0;
    if(format.fourcc())
    {
        flags |= VBOXVHWA_PF_FOURCC;
        rc = SSMR3PutU32(pSSM, flags);         AssertRC(rc);
        rc = SSMR3PutU32(pSSM, format.fourcc());         AssertRC(rc);
    }
    else
    {
        flags |= VBOXVHWA_PF_RGB;
        rc = SSMR3PutU32(pSSM, flags);         AssertRC(rc);
        rc = SSMR3PutU32(pSSM, format.bitsPerPixel());         AssertRC(rc);
        rc = SSMR3PutU32(pSSM, format.r().mask());         AssertRC(rc);
        rc = SSMR3PutU32(pSSM, format.g().mask());         AssertRC(rc);
        rc = SSMR3PutU32(pSSM, format.b().mask());         AssertRC(rc);
        rc = SSMR3PutU32(pSSM, format.a().mask());         AssertRC(rc);
    }

    VBOXQGL_SAVE_SURFSTOP(pSSM);

    return rc;
}

int VBoxVHWAImage::vhwaLoadSurface(VHWACommandList * pCmdList, struct SSMHANDLE * pSSM, uint32_t cBackBuffers, uint32_t u32Version)
{
    Q_UNUSED(u32Version);

    VBOXQGL_LOAD_SURFSTART(pSSM);

    char *buf = (char*)malloc(VBOXVHWACMD_SIZE(VBOXVHWACMD_SURF_CREATE));
    memset(buf, 0, sizeof(VBOXVHWACMD_SIZE(VBOXVHWACMD_SURF_CREATE)));
    VBOXVHWACMD * pCmd = (VBOXVHWACMD*)buf;
    pCmd->enmCmd = VBOXVHWACMD_TYPE_SURF_CREATE;
    pCmd->Flags = VBOXVHWACMD_FLAG_HH_CMD;

    VBOXVHWACMD_SURF_CREATE * pCreateSurf = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_CREATE);
    int rc;
    uint32_t u32;
    rc = SSMR3GetU32(pSSM, &u32);         AssertRC(rc);
    pCreateSurf->SurfInfo.hSurf = (VBOXVHWA_SURFHANDLE)u32;
    if(RT_SUCCESS(rc))
    {
        rc = SSMR3GetU64(pSSM, &pCreateSurf->SurfInfo.offSurface);         AssertRC(rc);
        rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.width);         AssertRC(rc);
        rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.height);         AssertRC(rc);
        rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.surfCaps);         AssertRC(rc);
        rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.flags);         AssertRC(rc);
        if(pCreateSurf->SurfInfo.flags & VBOXVHWA_SD_CKDESTBLT)
        {
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.DstBltCK.low);         AssertRC(rc);
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.DstBltCK.high);         AssertRC(rc);
        }
        if(pCreateSurf->SurfInfo.flags & VBOXVHWA_SD_CKSRCBLT)
        {
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.SrcBltCK.low);         AssertRC(rc);
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.SrcBltCK.high);         AssertRC(rc);
        }
        if(pCreateSurf->SurfInfo.flags & VBOXVHWA_SD_CKDESTOVERLAY)
        {
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.DstOverlayCK.low);         AssertRC(rc);
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.DstOverlayCK.high);         AssertRC(rc);
        }
        if(pCreateSurf->SurfInfo.flags & VBOXVHWA_SD_CKSRCOVERLAY)
        {
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.SrcOverlayCK.low);         AssertRC(rc);
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.SrcOverlayCK.high);         AssertRC(rc);
        }

        rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.PixelFormat.flags);         AssertRC(rc);
        if(pCreateSurf->SurfInfo.PixelFormat.flags & VBOXVHWA_PF_RGB)
        {
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.PixelFormat.c.rgbBitCount);         AssertRC(rc);
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.PixelFormat.m1.rgbRBitMask);         AssertRC(rc);
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.PixelFormat.m2.rgbGBitMask);         AssertRC(rc);
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.PixelFormat.m3.rgbBBitMask);         AssertRC(rc);
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.PixelFormat.m4.rgbABitMask);         AssertRC(rc);
        }
        else if(pCreateSurf->SurfInfo.PixelFormat.flags & VBOXVHWA_PF_FOURCC)
        {
            rc = SSMR3GetU32(pSSM, &pCreateSurf->SurfInfo.PixelFormat.fourCC);         AssertRC(rc);
        }
        else
        {
            Assert(0);
        }

        if(cBackBuffers)
        {
            pCreateSurf->SurfInfo.cBackBuffers = cBackBuffers;
            pCreateSurf->SurfInfo.surfCaps |= VBOXVHWA_SCAPS_COMPLEX;
        }

        pCmdList->push_back(pCmd);
//        vboxExecOnResize(&VBoxVHWAImage::vboxDoVHWACmdAndFree, pCmd); AssertRC(rc);
//        if(RT_SUCCESS(rc))
//        {
//            rc = pCmd->rc;
//            AssertRC(rc);
//        }
    }

    VBOXQGL_LOAD_SURFSTOP(pSSM);

    return rc;
}

int VBoxVHWAImage::vhwaSaveOverlayData(struct SSMHANDLE * pSSM, VBoxVHWASurfaceBase *pSurf, bool bVisible)
{
    VBOXQGL_SAVE_OVERLAYSTART(pSSM);

    uint32_t flags = 0;
    const VBoxVHWAColorKey * dstCKey = pSurf->dstOverlayCKey();
    const VBoxVHWAColorKey * defaultDstCKey = pSurf->defaultDstOverlayCKey();
    const VBoxVHWAColorKey * srcCKey = pSurf->srcOverlayCKey();;
    const VBoxVHWAColorKey * defaultSrcCKey = pSurf->defaultSrcOverlayCKey();
    bool bSaveDstCKey = false;
    bool bSaveSrcCKey = false;

    if(bVisible)
    {
        flags |= VBOXVHWA_OVER_SHOW;
    }
    else
    {
        flags |= VBOXVHWA_OVER_HIDE;
    }

    if(!dstCKey)
    {
        flags |= VBOXVHWA_OVER_KEYDEST;
    }
    else if(defaultDstCKey)
    {
        flags |= VBOXVHWA_OVER_KEYDESTOVERRIDE;
        bSaveDstCKey = true;
    }

    if(srcCKey == defaultSrcCKey)
    {
        flags |= VBOXVHWA_OVER_KEYSRC;
    }
    else if(srcCKey)
    {
        flags |= VBOXVHWA_OVER_KEYSRCOVERRIDE;
        bSaveSrcCKey = true;
    }

    int rc = SSMR3PutU32(pSSM, flags); AssertRC(rc);

    rc = SSMR3PutU32(pSSM, mDisplay.getPrimary()->handle()); AssertRC(rc);
    rc = SSMR3PutU32(pSSM, pSurf->handle()); AssertRC(rc);

    if(bSaveDstCKey)
    {
        rc = SSMR3PutU32(pSSM, dstCKey->lower()); AssertRC(rc);
        rc = SSMR3PutU32(pSSM, dstCKey->upper()); AssertRC(rc);
    }
    if(bSaveSrcCKey)
    {
        rc = SSMR3PutU32(pSSM, srcCKey->lower()); AssertRC(rc);
        rc = SSMR3PutU32(pSSM, srcCKey->upper()); AssertRC(rc);
    }

    int x1, x2, y1, y2;
    pSurf->targRect().getCoords(&x1, &y1, &x2, &y2);
    rc = SSMR3PutS32(pSSM, x1); AssertRC(rc);
    rc = SSMR3PutS32(pSSM, x2+1); AssertRC(rc);
    rc = SSMR3PutS32(pSSM, y1); AssertRC(rc);
    rc = SSMR3PutS32(pSSM, y2+1); AssertRC(rc);

    pSurf->srcRect().getCoords(&x1, &y1, &x2, &y2);
    rc = SSMR3PutS32(pSSM, x1); AssertRC(rc);
    rc = SSMR3PutS32(pSSM, x2+1); AssertRC(rc);
    rc = SSMR3PutS32(pSSM, y1); AssertRC(rc);
    rc = SSMR3PutS32(pSSM, y2+1); AssertRC(rc);

    VBOXQGL_SAVE_OVERLAYSTOP(pSSM);

    return rc;
}

int VBoxVHWAImage::vhwaLoadOverlayData(VHWACommandList * pCmdList, struct SSMHANDLE * pSSM, uint32_t u32Version)
{
    Q_UNUSED(u32Version);

    VBOXQGL_LOAD_OVERLAYSTART(pSSM);

    char *buf = new char[VBOXVHWACMD_SIZE(VBOXVHWACMD_SURF_CREATE)];
    memset(buf, 0, VBOXVHWACMD_SIZE(VBOXVHWACMD_SURF_CREATE));
    VBOXVHWACMD * pCmd = (VBOXVHWACMD*)buf;
    pCmd->enmCmd = VBOXVHWACMD_TYPE_SURF_OVERLAY_UPDATE;
    pCmd->Flags = VBOXVHWACMD_FLAG_HH_CMD;

    VBOXVHWACMD_SURF_OVERLAY_UPDATE * pUpdateOverlay = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_OVERLAY_UPDATE);
    int rc;

    rc = SSMR3GetU32(pSSM, &pUpdateOverlay->u.in.flags); AssertRC(rc);
    uint32_t hSrc, hDst;
    rc = SSMR3GetU32(pSSM, &hDst); AssertRC(rc);
    rc = SSMR3GetU32(pSSM, &hSrc); AssertRC(rc);
    pUpdateOverlay->u.in.hSrcSurf = hSrc;
    pUpdateOverlay->u.in.hDstSurf = hDst;
    {
        pUpdateOverlay->u.in.offDstSurface = VBOXVHWA_OFFSET64_VOID;
        pUpdateOverlay->u.in.offSrcSurface = VBOXVHWA_OFFSET64_VOID;

        if(pUpdateOverlay->u.in.flags & VBOXVHWA_OVER_KEYDESTOVERRIDE)
        {
            rc = SSMR3GetU32(pSSM, &pUpdateOverlay->u.in.desc.DstCK.low); AssertRC(rc);
            rc = SSMR3GetU32(pSSM, &pUpdateOverlay->u.in.desc.DstCK.high); AssertRC(rc);
        }

        if(pUpdateOverlay->u.in.flags & VBOXVHWA_OVER_KEYSRCOVERRIDE)
        {
            rc = SSMR3GetU32(pSSM, &pUpdateOverlay->u.in.desc.SrcCK.low); AssertRC(rc);
            rc = SSMR3GetU32(pSSM, &pUpdateOverlay->u.in.desc.SrcCK.high); AssertRC(rc);
        }

        rc = SSMR3GetS32(pSSM, &pUpdateOverlay->u.in.dstRect.left); AssertRC(rc);
        rc = SSMR3GetS32(pSSM, &pUpdateOverlay->u.in.dstRect.right); AssertRC(rc);
        rc = SSMR3GetS32(pSSM, &pUpdateOverlay->u.in.dstRect.top); AssertRC(rc);
        rc = SSMR3GetS32(pSSM, &pUpdateOverlay->u.in.dstRect.bottom); AssertRC(rc);

        rc = SSMR3GetS32(pSSM, &pUpdateOverlay->u.in.srcRect.left); AssertRC(rc);
        rc = SSMR3GetS32(pSSM, &pUpdateOverlay->u.in.srcRect.right); AssertRC(rc);
        rc = SSMR3GetS32(pSSM, &pUpdateOverlay->u.in.srcRect.top); AssertRC(rc);
        rc = SSMR3GetS32(pSSM, &pUpdateOverlay->u.in.srcRect.bottom); AssertRC(rc);

        pCmdList->push_back(pCmd);
    }

    VBOXQGL_LOAD_OVERLAYSTOP(pSSM);

    return rc;
}

void VBoxVHWAImage::vhwaSaveExecVoid(struct SSMHANDLE * pSSM)
{
    VBOXQGL_SAVE_START(pSSM);
    int rc = SSMR3PutU32(pSSM, 0);         AssertRC(rc); /* 0 primaries */
    VBOXQGL_SAVE_STOP(pSSM);
}

void VBoxVHWAImage::vhwaSaveExec(struct SSMHANDLE * pSSM)
{
    VBOXQGL_SAVE_START(pSSM);

    /* the mechanism of restoring data is based on generating VHWA commands that restore the surfaces state
     * the following commands are generated:
     * I.    CreateSurface
     * II.   UpdateOverlay
     *
     * Data format is the following:
     * I.    u32 - Num primary surfaces - (the current frontbuffer is detected in the stored surf flags which are posted to the generated CreateSurface cmd)
     * II.   for each primary surf
     * II.1    generate & execute CreateSurface cmd (see below on the generation logic)
     * III.  u32 - Num overlays
     * IV.   for each overlay
     * IV.1    u32 - Num surfaces in overlay (the current frontbuffer is detected in the stored surf flags which are posted to the generated CreateSurface cmd)
     * IV.2    for each surface in overlay
     * IV.2.a    generate & execute CreateSurface cmd (see below on the generation logic)
     * IV.2.b    generate & execute UpdateOverlay cmd (see below on the generation logic)
     *
     */
    const SurfList & primaryList = mDisplay.primaries().surfaces();
    uint32_t cPrimary = (uint32_t)primaryList.size();
    if(cPrimary &&
            (mDisplay.getVGA() == NULL || mDisplay.getVGA()->handle() == VBOXVHWA_SURFHANDLE_INVALID))
    {
        cPrimary -= 1;
    }

    int rc = SSMR3PutU32(pSSM, cPrimary);         AssertRC(rc);
    if(cPrimary)
    {
        for (SurfList::const_iterator pr = primaryList.begin();
             pr != primaryList.end(); ++ pr)
        {
            VBoxVHWASurfaceBase *pSurf = *pr;
    //        bool bVga = (pSurf == mDisplay.getVGA());
            bool bVisible = (pSurf == mDisplay.getPrimary());
            uint32_t flags = VBOXVHWA_SCAPS_PRIMARYSURFACE;
            if(bVisible)
                flags |= VBOXVHWA_SCAPS_VISIBLE;

            if(pSurf->handle() != VBOXVHWA_SURFHANDLE_INVALID)
            {
                rc = vhwaSaveSurface(pSSM, *pr, flags);    AssertRC(rc);
#ifdef DEBUG
                --cPrimary;
                Assert(cPrimary < UINT32_MAX / 2);
#endif
            }
            else
            {
                Assert(pSurf == mDisplay.getVGA());
            }
        }

#ifdef DEBUG
        Assert(!cPrimary);
#endif

        const OverlayList & overlays = mDisplay.overlays();
        rc = SSMR3PutU32(pSSM, (uint32_t)overlays.size());         AssertRC(rc);

        for (OverlayList::const_iterator it = overlays.begin();
             it != overlays.end(); ++ it)
        {
            VBoxVHWASurfList * pSurfList = *it;
            const SurfList & surfaces = pSurfList->surfaces();
            uint32_t cSurfs = (uint32_t)surfaces.size();
            uint32_t flags = VBOXVHWA_SCAPS_OVERLAY;
            if(cSurfs > 1)
                flags |= VBOXVHWA_SCAPS_COMPLEX;
            rc = SSMR3PutU32(pSSM, cSurfs);         AssertRC(rc);
            for (SurfList::const_iterator sit = surfaces.begin();
                 sit != surfaces.end(); ++ sit)
            {
                rc = vhwaSaveSurface(pSSM, *sit, flags);    AssertRC(rc);
            }

            bool bVisible = true;
            VBoxVHWASurfaceBase * pOverlayData = pSurfList->current();
            if(!pOverlayData)
            {
                pOverlayData = surfaces.front();
                bVisible = false;
            }

            rc = vhwaSaveOverlayData(pSSM, pOverlayData, bVisible);    AssertRC(rc);
        }
    }

    VBOXQGL_SAVE_STOP(pSSM);
}

int VBoxVHWAImage::vhwaLoadVHWAEnable(VHWACommandList * pCmdList)
{
    char *buf = (char*)malloc(sizeof(VBOXVHWACMD));
    Assert(buf);
    if(buf)
    {
        memset(buf, 0, sizeof(VBOXVHWACMD));
        VBOXVHWACMD * pCmd = (VBOXVHWACMD*)buf;
        pCmd->enmCmd = VBOXVHWACMD_TYPE_ENABLE;
        pCmd->Flags = VBOXVHWACMD_FLAG_HH_CMD;
        pCmdList->push_back(pCmd);
        return VINF_SUCCESS;
    }

    return VERR_OUT_OF_RESOURCES;
}

int VBoxVHWAImage::vhwaLoadExec(VHWACommandList * pCmdList, struct SSMHANDLE * pSSM, uint32_t u32Version)
{
    VBOXQGL_LOAD_START(pSSM);

    if(u32Version > VBOXQGL_STATE_VERSION)
        return VERR_VERSION_MISMATCH;

    int rc;
    uint32_t u32;

    rc = vhwaLoadVHWAEnable(pCmdList); AssertRC(rc);
    if(RT_SUCCESS(rc))
    {
        rc = SSMR3GetU32(pSSM, &u32); AssertRC(rc);
        if(RT_SUCCESS(rc))
        {
            if(u32Version == 1U && u32 == (~0U)) /* work around the v1 bug */
                u32 = 0;
            if(u32)
            {
                for(uint32_t i = 0; i < u32; ++i)
                {
                    rc = vhwaLoadSurface(pCmdList, pSSM, 0, u32Version);  AssertRC(rc);
                    if(RT_FAILURE(rc))
                        break;
                }

                if(RT_SUCCESS(rc))
                {
                    rc = SSMR3GetU32(pSSM, &u32); AssertRC(rc);
                    if(RT_SUCCESS(rc))
                    {
                        for(uint32_t i = 0; i < u32; ++i)
                        {
                            uint32_t cSurfs;
                            rc = SSMR3GetU32(pSSM, &cSurfs); AssertRC(rc);
                            for(uint32_t j = 0; j < cSurfs; ++j)
                            {
                                rc = vhwaLoadSurface(pCmdList, pSSM, cSurfs - 1, u32Version);  AssertRC(rc);
                                if(RT_FAILURE(rc))
                                    break;
                            }

                            if(RT_SUCCESS(rc))
                            {
                                rc = vhwaLoadOverlayData(pCmdList, pSSM, u32Version);  AssertRC(rc);
                            }

                            if(RT_FAILURE(rc))
                            {
                                break;
                            }
                        }
                    }
                }
            }
#ifdef VBOXQGL_STATE_DEBUG
            else if(u32Version == 1) /* read the 0 overlay count to ensure the following VBOXQGL_LOAD_STOP succeedes */
            {
                rc = SSMR3GetU32(pSSM, &u32); AssertRC(rc);
                Assert(u32 == 0);
            }
#endif
        }
    }

    VBOXQGL_LOAD_STOP(pSSM);

    return rc;
}

int VBoxVHWAImage::vhwaConstruct(struct _VBOXVHWACMD_HH_CONSTRUCT *pCmd)
{
//    PVM pVM = (PVM)pCmd->pVM;
//    uint32_t intsId = 0; /* @todo: set the proper id */
//
//    char nameFuf[sizeof(VBOXQGL_STATE_NAMEBASE) + 8];
//
//    char * pszName = nameFuf;
//    sprintf(pszName, "%s%d", VBOXQGL_STATE_NAMEBASE, intsId);
//    int rc = SSMR3RegisterExternal(
//            pVM,                    /* The VM handle*/
//            pszName,                /* Data unit name. */
//            intsId,                 /* The instance identifier of the data unit.
//                                     * This must together with the name be unique. */
//            VBOXQGL_STATE_VERSION,   /* Data layout version number. */
//            128,             /* The approximate amount of data in the unit.
//                              * Only for progress indicators. */
//            NULL, NULL, NULL, /* pfnLiveXxx */
//            NULL,            /* Prepare save callback, optional. */
//            vboxQGLSaveExec, /* Execute save callback, optional. */
//            NULL,            /* Done save callback, optional. */
//            NULL,            /* Prepare load callback, optional. */
//            vboxQGLLoadExec, /* Execute load callback, optional. */
//            NULL,            /* Done load callback, optional. */
//            this             /* User argument. */
//            );
//    AssertRC(rc);
    mpvVRAM = pCmd->pvVRAM;
    mcbVRAM = pCmd->cbVRAM;
    return VINF_SUCCESS;
}

uchar * VBoxVHWAImage::vboxVRAMAddressFromOffset(uint64_t offset)
{
    /* @todo: check vramSize() */
    return (offset != VBOXVHWA_OFFSET64_VOID) ? ((uint8_t*)vramBase()) + offset : NULL;
}

uint64_t VBoxVHWAImage::vboxVRAMOffsetFromAddress(uchar* addr)
{
    return uint64_t(addr - ((uchar*)vramBase()));
}

uint64_t VBoxVHWAImage::vboxVRAMOffset(VBoxVHWASurfaceBase * pSurf)
{
    return pSurf->addressAlocated() ? VBOXVHWA_OFFSET64_VOID : vboxVRAMOffsetFromAddress(pSurf->address());
}

#endif

#ifdef VBOXQGL_DBG_SURF

int g_iCur = 0;
VBoxVHWASurfaceBase * g_apSurf[] = {NULL, NULL, NULL};

void VBoxVHWAImage::vboxDoTestSurfaces(void* context)
{
    if(g_iCur >= RT_ELEMENTS(g_apSurf))
        g_iCur = 0;
    VBoxVHWASurfaceBase * pSurf1 = g_apSurf[g_iCur];
    if(pSurf1)
    {
        pSurf1->getComplexList()->setCurrentVisible(pSurf1);
    }
}
#endif

void VBoxVHWAImage::vboxDoUpdateViewport(const QRect & aRect)
{
    adjustViewport(mDisplay.getPrimary()->size(), aRect);
    mViewport = aRect;

    const SurfList & primaryList = mDisplay.primaries().surfaces();

    for (SurfList::const_iterator pr = primaryList.begin();
         pr != primaryList.end(); ++ pr)
    {
        VBoxVHWASurfaceBase *pSurf = *pr;
        pSurf->updateVisibility(NULL, aRect, false, false);
    }

    const OverlayList & overlays = mDisplay.overlays();
    QRect overInter = overlaysRectIntersection();
    overInter = overInter.intersect(aRect);

    bool bDisplayPrimary = true;

    for (OverlayList::const_iterator it = overlays.begin();
         it != overlays.end(); ++ it)
    {
        VBoxVHWASurfList * pSurfList = *it;
        const SurfList & surfaces = pSurfList->surfaces();
        if(surfaces.size())
        {
            bool bNotIntersected = !overInter.isEmpty() && surfaces.front()->targRect().contains(overInter);
            Assert(bNotIntersected);

            bDisplayPrimary &= !bNotIntersected;
            for (SurfList::const_iterator sit = surfaces.begin();
                 sit != surfaces.end(); ++ sit)
            {
                VBoxVHWASurfaceBase *pSurf = *sit;
                pSurf->updateVisibility(mDisplay.getPrimary(), aRect, bNotIntersected, false);
            }
        }
    }

    Assert(!bDisplayPrimary);
    mDisplay.setDisplayPrimary(bDisplayPrimary);
}

bool VBoxVHWAImage::hasSurfaces() const
{
    if (mDisplay.overlays().size() != 0)
        return true;
    if (mDisplay.primaries().size() > 1)
        return true;
    /* in case gl was never turned on, we have no surfaces at all including VGA */
    if (!mDisplay.getVGA())
        return false;
    return mDisplay.getVGA()->handle() != VBOXVHWA_SURFHANDLE_INVALID;
}

bool VBoxVHWAImage::hasVisibleOverlays()
{
    const OverlayList & overlays = mDisplay.overlays();
    for (OverlayList::const_iterator it = overlays.begin();
         it != overlays.end(); ++ it)
    {
        VBoxVHWASurfList * pSurfList = *it;
        if(pSurfList->current() != NULL)
            return true;
    }
    return false;
}

QRect VBoxVHWAImage::overlaysRectUnion()
{
    const OverlayList & overlays = mDisplay.overlays();
    VBoxVHWADirtyRect un;
    for (OverlayList::const_iterator it = overlays.begin();
         it != overlays.end(); ++ it)
    {
        VBoxVHWASurfaceBase * pOverlay = (*it)->current();
        if(pOverlay != NULL)
        {
            un.add(pOverlay->targRect());
        }
    }
    return un.toRect();
}

QRect VBoxVHWAImage::overlaysRectIntersection()
{
    const OverlayList & overlays = mDisplay.overlays();
    QRect rect;
    VBoxVHWADirtyRect un;
    for (OverlayList::const_iterator it = overlays.begin();
         it != overlays.end(); ++ it)
    {
        VBoxVHWASurfaceBase * pOverlay = (*it)->current();
        if(pOverlay != NULL)
        {
            if(rect.isNull())
            {
                rect = pOverlay->targRect();
            }
            else
            {
                rect = rect.intersected(pOverlay->targRect());
                if(rect.isNull())
                    break;
            }
        }
    }
    return rect;
}

void VBoxVHWAImage::vboxDoUpdateRect(const QRect * pRect)
{
    mDisplay.getPrimary()->updatedMem(pRect);
}

void VBoxVHWAImage::resize(const VBoxFBSizeInfo & size)
{
//    VBOXQGLLOG(("format().blueBufferSize()(%d)\n", format().blueBufferSize()));
//    VBOXQGLLOG(("format().greenBufferSize()(%d)\n", format().greenBufferSize()));
//    VBOXQGLLOG(("format().redBufferSize()(%d)\n", format().redBufferSize()));
//#ifdef DEBUG_misha
//    Assert(format().blueBufferSize() == 8);
//    Assert(format().greenBufferSize() == 8);
//    Assert(format().redBufferSize() == 8);
//#endif
//
//    Assert(format().directRendering());
//    Assert(format().doubleBuffer());
//    Assert(format().hasOpenGL());
//    VBOXQGLLOG(("hasOpenGLOverlays(%d), hasOverlay(%d)\n", format().hasOpenGLOverlays(), format().hasOverlay()));
//    Assert(format().plane() == 0);
//    Assert(format().rgba());
//    Assert(!format().sampleBuffers());
//    Assert(!format().stereo());
//    VBOXQGLLOG(("swapInterval(%d)\n", format().swapInterval()));


    VBOXQGL_CHECKERR(
            vboxglActiveTexture(GL_TEXTURE0);
        );

    bool remind = false;
    bool fallback = false;

    VBOXQGLLOG(("resizing: fmt=%d, vram=%p, bpp=%d, bpl=%d, width=%d, height=%d\n",
                      size.pixelFormat(), size.VRAM(),
                      size.bitsPerPixel(), size.bytesPerLine(),
                      size.width(), size.height()));

    /* clean the old values first */

    ulong bytesPerLine;
    uint32_t bitsPerPixel;
    uint32_t b = 0xff, g = 0xff00, r = 0xff0000;
    ulong pixelFormat;
    bool bUsesGuestVram;

    /* check if we support the pixel format and can use the guest VRAM directly */
    if (size.pixelFormat() == FramebufferPixelFormat_FOURCC_RGB)
    {

        bitsPerPixel = size.bitsPerPixel();
        bytesPerLine = size.bytesPerLine();
        ulong bitsPerLine = bytesPerLine * 8;

        switch (bitsPerPixel)
        {
            case 32:
                break;
            case 24:
#ifdef DEBUG_misha
                Assert(0);
#endif
                break;
            case 8:
#ifdef DEBUG_misha
                Assert(0);
#endif
                g = b = 0;
                remind = true;
                break;
            case 1:
#ifdef DEBUG_misha
                Assert(0);
#endif
                r = 1;
                g = b = 0;
                remind = true;
                break;
            default:
#ifdef DEBUG_misha
                Assert(0);
#endif
                remind = true;
                fallback = true;
                break;
        }

        if (!fallback)
        {
            /* QImage only supports 32-bit aligned scan lines... */
            Assert ((size.bytesPerLine() & 3) == 0);
            fallback = ((size.bytesPerLine() & 3) != 0);
            Assert(!fallback);
        }
        if (!fallback)
        {
            /* ...and the scan lines ought to be a whole number of pixels. */
            Assert ((bitsPerLine & (size.bitsPerPixel() - 1)) == 0);
            fallback = ((bitsPerLine & (size.bitsPerPixel() - 1)) != 0);
            Assert(!fallback);
        }
        if (!fallback)
        {
            // ulong virtWdt = bitsPerLine / size.bitsPerPixel();
            pixelFormat = FramebufferPixelFormat_FOURCC_RGB;
            bUsesGuestVram = true;
        }
    }
    else
    {
        AssertBreakpoint();
        fallback = true;
    }

    if (fallback)
    {
        /* we should never come to fallback more now */
        AssertBreakpoint();
        /* we don't support either the pixel format or the color depth,
         * fallback to a self-provided 32bpp RGB buffer */
        bitsPerPixel = 32;
        b = 0xff;
        g = 0xff00;
        r = 0xff0000;
        bytesPerLine = size.width()*bitsPerPixel/8;
        pixelFormat = FramebufferPixelFormat_FOURCC_RGB;
        bUsesGuestVram = false;
    }

    ulong bytesPerPixel = bitsPerPixel/8;
    ulong displayWidth = bytesPerLine/bytesPerPixel;
    ulong displayHeight = size.height();

#ifdef VBOXQGL_DBG_SURF
    for(int i = 0; i < RT_ELEMENTS(g_apSurf); i++)
    {
        VBoxVHWASurfaceBase * pSurf1 = g_apSurf[i];
        if(pSurf1)
        {
            VBoxVHWASurfList *pConstructingList = pSurf1->getComplexList();
            delete pSurf1;
            if(pConstructingList)
                delete pConstructingList;
        }
    }
#endif

    VBoxVHWASurfaceBase * pDisplay = mDisplay.setVGA(NULL);
    if(pDisplay)
        delete pDisplay;

    VBoxVHWAColorFormat format(bitsPerPixel, r,g,b);
    QSize dispSize(displayWidth, displayHeight);
    QRect dispRect(0, 0, displayWidth, displayHeight);
    pDisplay = new VBoxVHWASurfaceBase(this,
            dispSize,
            dispRect,
            dispRect,
            dispRect, /* we do not know viewport at the stage of recise, set as a disp rect, it will be updated on repaint */
            format,
            (VBoxVHWAColorKey*)NULL, (VBoxVHWAColorKey*)NULL, (VBoxVHWAColorKey*)NULL, (VBoxVHWAColorKey*)NULL,
#ifdef VBOXVHWA_USE_TEXGROUP
            0,
#endif
            0);
    pDisplay->init(NULL, bUsesGuestVram ? size.VRAM() : NULL);
    mDisplay.setVGA(pDisplay);
//    VBOXQGLLOG(("\n\n*******\n\n     viewport size is: (%d):(%d)\n\n*******\n\n", size().width(), size().height()));
    mViewport = QRect(0,0,displayWidth, displayHeight);
    adjustViewport(dispSize, mViewport);
    setupMatricies(dispSize, true);

#ifdef VBOXQGL_DBG_SURF
    {
        uint32_t width = 100;
        uint32_t height = 60;

        for(int i = 0; i < RT_ELEMENTS(g_apSurf); i++)
        {
            VBoxVHWAColorFormat tmpFormat(FOURCC_YV12);
            QSize tmpSize(width, height) ;
            VBoxVHWASurfaceBase *pSurf1 = new VBoxVHWASurfaceBase(this, tmpSize,
                             mDisplay.getPrimary()->rect(),
                             QRect(0, 0, width, height),
                             mViewport,
                             tmpFormat,
                             NULL, NULL, NULL, &VBoxVHWAColorKey(0,0),
#ifdef VBOXVHWA_USE_TEXGROUP
                             0,
#endif
                             false);

            Assert(mDisplay.getVGA());
            pSurf1->init(mDisplay.getVGA(), NULL);
            uchar *addr = pSurf1->address();
            uchar cur = 0;
            for(uint32_t k = 0; k < width*height; k++)
            {
                addr[k] = cur;
                cur+=64;
            }
            pSurf1->updatedMem(&QRect(0,0,width, height));

            VBoxVHWASurfList *pConstructingList = new VBoxVHWASurfList();
            mDisplay.addOverlay(pConstructingList);
            pConstructingList->add(pSurf1);
            g_apSurf[i] = pSurf1;

        }

        VBOXVHWACMD_SURF_OVERLAY_UPDATE updateCmd;
        memset(&updateCmd, 0, sizeof(updateCmd));
        updateCmd.u.in.hSrcSurf = (VBOXVHWA_SURFHANDLE)g_apSurf[0];
        updateCmd.u.in.hDstSurf = (VBOXVHWA_SURFHANDLE)pDisplay;
        updateCmd.u.in.flags =
                VBOXVHWA_OVER_SHOW
                | VBOXVHWA_OVER_KEYDESTOVERRIDE;

        updateCmd.u.in.desc.DstCK.high = 1;
        updateCmd.u.in.desc.DstCK.low = 1;

        updateCmd.u.in.dstRect.left = 0;
        updateCmd.u.in.dstRect.right = pDisplay->width();
        updateCmd.u.in.dstRect.top = (pDisplay->height() - height)/2;
        updateCmd.u.in.dstRect.bottom = updateCmd.u.in.dstRect.top + height;

        updateCmd.u.in.srcRect.left = 0;
        updateCmd.u.in.srcRect.right = width;
        updateCmd.u.in.srcRect.top = 0;
        updateCmd.u.in.srcRect.bottom = height;

        updateCmd.u.in.offDstSurface = VBOXVHWA_OFFSET64_VOID; /* just a magic to avoid surf mem buffer change  */
        updateCmd.u.in.offSrcSurface = VBOXVHWA_OFFSET64_VOID; /* just a magic to avoid surf mem buffer change  */

        vhwaSurfaceOverlayUpdate(&updateCmd);
    }
#endif

//    if(!mOnResizeCmdList.empty())
//    {
//        for (VHWACommandList::const_iterator it = mOnResizeCmdList.begin();
//             it != mOnResizeCmdList.end(); ++ it)
//        {
//            VBOXVHWACMD * pCmd = (*it);
//            vboxDoVHWACmdExec(pCmd);
//            free(pCmd);
//        }
//        mOnResizeCmdList.clear();
//    }

    if (remind)
    {
        class RemindEvent : public VBoxAsyncEvent
        {
            ulong mRealBPP;
        public:
            RemindEvent (ulong aRealBPP)
                : VBoxAsyncEvent(0), mRealBPP (aRealBPP) {}
            void handle()
            {
                vboxProblem().remindAboutWrongColorDepth (mRealBPP, 32);
            }
        };
        (new RemindEvent (size.bitsPerPixel()))->post();
    }
}

VBoxVHWAColorFormat::VBoxVHWAColorFormat (uint32_t bitsPerPixel, uint32_t r, uint32_t g, uint32_t b) :
    mWidthCompression (1),
    mHeightCompression (1)
{
    init (bitsPerPixel, r, g, b);
}

VBoxVHWAColorFormat::VBoxVHWAColorFormat (uint32_t fourcc) :
    mWidthCompression (1),
    mHeightCompression (1)
{
    init (fourcc);
}

void VBoxVHWAColorFormat::init (uint32_t fourcc)
{
    mDataFormat = fourcc;
    mInternalFormat = GL_RGBA8;//GL_RGB;
    mFormat = GL_BGRA_EXT;//GL_RGBA;
    mType = GL_UNSIGNED_BYTE;
    mR = VBoxVHWAColorComponent (0xff);
    mG = VBoxVHWAColorComponent (0xff);
    mB = VBoxVHWAColorComponent (0xff);
    mA = VBoxVHWAColorComponent (0xff);
    mBitsPerPixelTex = 32;

    switch(fourcc)
    {
        case FOURCC_AYUV:
            mBitsPerPixel = 32;
#ifdef VBOX_WITH_WDDM
            mBitsPerPixelMem = 32;
#endif
            mWidthCompression = 1;
            break;
        case FOURCC_UYVY:
        case FOURCC_YUY2:
            mBitsPerPixel = 16;
#ifdef VBOX_WITH_WDDM
            mBitsPerPixelMem = 16;
#endif
            mWidthCompression = 2;
            break;
        case FOURCC_YV12:
            mBitsPerPixel = 8;
#ifdef VBOX_WITH_WDDM
            mBitsPerPixelMem = 12;
#endif
            mWidthCompression = 4;
            break;
        default:
            Assert(0);
            mBitsPerPixel = 0;
            mBitsPerPixelTex = 0;
#ifdef VBOX_WITH_WDDM
            mBitsPerPixelMem = 0;
#endif
            mWidthCompression = 0;
            break;
    }
}

void VBoxVHWAColorFormat::init (uint32_t bitsPerPixel, uint32_t r, uint32_t g, uint32_t b)
{
    mBitsPerPixel = bitsPerPixel;
    mBitsPerPixelTex = bitsPerPixel;
#ifdef VBOX_WITH_WDDM
    mBitsPerPixelMem = bitsPerPixel;
#endif
    mDataFormat = 0;
    switch (bitsPerPixel)
    {
        case 32:
            mInternalFormat = GL_RGB;//3;//GL_RGB;
            mFormat = GL_BGRA_EXT;//GL_RGBA;
            mType = GL_UNSIGNED_BYTE;
            mR = VBoxVHWAColorComponent (r);
            mG = VBoxVHWAColorComponent (g);
            mB = VBoxVHWAColorComponent (b);
            break;
        case 24:
#ifdef DEBUG_misha
            Assert(0);
#endif
            mInternalFormat = 3;//GL_RGB;
            mFormat = GL_BGR_EXT;
            mType = GL_UNSIGNED_BYTE;
            mR = VBoxVHWAColorComponent (r);
            mG = VBoxVHWAColorComponent (g);
            mB = VBoxVHWAColorComponent (b);
            break;
        case 16:
#ifdef DEBUG_misha
            Assert(0);
#endif
            mInternalFormat = GL_RGB5;
            mFormat = GL_BGR_EXT;
            mType = GL_UNSIGNED_BYTE; /* TODO" ??? */
            mR = VBoxVHWAColorComponent (r);
            mG = VBoxVHWAColorComponent (g);
            mB = VBoxVHWAColorComponent (b);
            break;
        case 8:
#ifdef DEBUG_misha
            Assert(0);
#endif
            mInternalFormat = 1;//GL_RGB;
            mFormat = GL_RED;//GL_RGB;
            mType = GL_UNSIGNED_BYTE;
            mR = VBoxVHWAColorComponent (0xff);
            break;
        case 1:
#ifdef DEBUG_misha
            Assert(0);
#endif
            mInternalFormat = 1;
            mFormat = GL_COLOR_INDEX;
            mType = GL_BITMAP;
            mR = VBoxVHWAColorComponent (0x1);
            break;
        default:
#ifdef DEBUG_misha
            Assert(0);
#endif
            mBitsPerPixel = 0;
            mBitsPerPixelTex = 0;
#ifdef VBOX_WITH_WDDM
            mBitsPerPixelMem = 0;
#endif
            break;
    }
}

bool VBoxVHWAColorFormat::equals (const VBoxVHWAColorFormat & other) const
{
    if(fourcc())
        return fourcc() == other.fourcc();
    if(other.fourcc())
        return false;

    return bitsPerPixel() == other.bitsPerPixel();
}

VBoxVHWAColorComponent::VBoxVHWAColorComponent (uint32_t aMask)
{
    unsigned f = ASMBitFirstSetU32 (aMask);
    if(f)
    {
        mOffset = f - 1;
        f = ASMBitFirstSetU32 (~(aMask >> mOffset));
        if(f)
        {
            mcBits = f - 1;
        }
        else
        {
            mcBits = 32 - mOffset;
        }

        Assert (mcBits);
        mMask = (((uint32_t)0xffffffff) >> (32 - mcBits)) << mOffset;
        Assert (mMask == aMask);

        mRange = (mMask >> mOffset) + 1;
    }
    else
    {
        mMask = 0;
        mRange = 0;
        mOffset = 32;
        mcBits = 0;
    }
}

void VBoxVHWAColorFormat::pixel2Normalized (uint32_t pix, float *r, float *g, float *b) const
{
    *r = mR.colorValNorm (pix);
    *g = mG.colorValNorm (pix);
    *b = mB.colorValNorm (pix);
}

VBoxQGLOverlay::VBoxQGLOverlay (QWidget *pViewport,QObject *pPostEventObject,  CSession * aSession, uint32_t id)
    : mpOverlayWgt (NULL),
      mpViewport (pViewport),
      mGlOn (false),
      mOverlayWidgetVisible (false),
      mOverlayVisible (false),
      mGlCurrent (false),
      mProcessingCommands (false),
      mNeedOverlayRepaint (false),
      mNeedSetVisible (false),
      mCmdPipe (pPostEventObject),
      mSettings (*aSession),
      mpSession(aSession),
      mpShareWgt (NULL),
      m_id(id)
{
    /* postpone the gl widget initialization to avoid conflict with 3D on Mac */
}

class VBoxGLShareWgt : public QGLWidget
{
public:
    VBoxGLShareWgt() :
        QGLWidget(new VBoxGLContext(VBoxVHWAImage::vboxGLFormat()))
    {
        /* work-around to disable done current needed to old ATI drivers on Linux */
        VBoxGLContext *pc = (VBoxGLContext*)context();
        pc->allowDoneCurrent (false);
    }

protected:
    void initializeGL()
    {
        vboxVHWAGetSupportInfo(context());
        VBoxVHWASurfaceBase::globalInit();
    }
};
void VBoxQGLOverlay::initGl()
{
    if(mpOverlayWgt)
    {
        Assert(mpShareWgt);
        return;
    }

    if (!mpShareWgt)
    {
        mpShareWgt = new VBoxGLShareWgt();
        /* force initializeGL */
        mpShareWgt->updateGL();
    }

    mOverlayImage.init(&mSettings);
    mpOverlayWgt = new VBoxGLWgt(&mOverlayImage, mpViewport, mpShareWgt);

    mOverlayWidgetVisible = true; /* to ensure it is set hidden with vboxShowOverlay */
    vboxShowOverlay (false);

    mpOverlayWgt->setMouseTracking (true);
}

void VBoxQGLOverlay::updateAttachment(QWidget *pViewport, QObject *pPostEventObject)
{
    if (mpViewport != pViewport)
    {
        mpViewport = pViewport;
        mpOverlayWgt = NULL;
        mOverlayWidgetVisible = false;
        if (mOverlayImage.hasSurfaces())
        {
//            Assert(!mOverlayVisible);
            if (pViewport)
            {
                initGl();
//            vboxDoCheckUpdateViewport();
            }
//            Assert(!mOverlayVisible);
        }
        mGlCurrent = false;
    }
    mCmdPipe.setNotifyObject(pPostEventObject);
}

int VBoxQGLOverlay::reset()
{
    VBoxVHWACommandElement * pHead, * pTail;
    mCmdPipe.reset(&pHead, &pTail);
    if(pHead)
    {
        CDisplay display = mpSession->GetConsole().GetDisplay();
        Assert (!display.isNull());

        /* complete aborted commands */
        for(VBoxVHWACommandElement * pCur = pHead; pCur; pCur = pCur->mpNext)
        {
            switch(pCur->type())
            {
#ifdef VBOX_WITH_VIDEOHWACCEL
            case VBOXVHWA_PIPECMD_VHWA:
                {
                    struct _VBOXVHWACMD * pCmd = pCur->vhwaCmd();
                    pCmd->rc = VERR_INVALID_STATE;
                    display.CompleteVHWACommand((BYTE*)pCmd);
                }
                break;
            case VBOXVHWA_PIPECMD_FUNC:
                /* should not happen, don't handle this for now */
                Assert(0);
                break;
#endif
            case VBOXVHWA_PIPECMD_PAINT:
                break;
            default:
                /* should not happen, don't handle this for now */
                Assert(0);
                break;
            }
        }

        VBoxVHWACommandElement *pTest = mCmdPipe.detachCmdList(NULL, pHead, pTail);
        Assert(!pTest);
        NOREF(pTest);
    }

    resetGl();

    return VINF_SUCCESS;
}

static DECLCALLBACK(void) vbvaVHWAHHCommandFreeCmd(void * pContext)
{
    free(pContext);
}

int VBoxQGLOverlay::resetGl()
{
    VHWACommandList list;
    int rc = mOverlayImage.reset(&list);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        for (VHWACommandList::const_iterator sIt = list.begin();
                    sIt != list.end(); ++ sIt)
        {
            VBOXVHWACMD *pCmd = (*sIt);
            VBOXVHWA_HH_CALLBACK_SET(pCmd, vbvaVHWAHHCommandFreeCmd, pCmd);
            mCmdPipe.postCmd(VBOXVHWA_PIPECMD_VHWA, pCmd, 0);
        }
    }
    return VINF_SUCCESS;
}

int VBoxQGLOverlay::onVHWACommand(struct _VBOXVHWACMD * pCmd)
{
    uint32_t flags = 0;
    switch(pCmd->enmCmd)
    {
        case VBOXVHWACMD_TYPE_SURF_FLIP:
        case VBOXVHWACMD_TYPE_SURF_OVERLAY_UPDATE:
        case VBOXVHWACMD_TYPE_SURF_OVERLAY_SETPOSITION:
            flags |= VBOXVHWACMDPIPEC_COMPLETEEVENT;
            break;
        case VBOXVHWACMD_TYPE_HH_RESET:
        {
            /* we do not post a reset command to the gui thread since this may lead to a deadlock
             * when reset is initiated by the gui thread*/
            pCmd->Flags &= ~VBOXVHWACMD_FLAG_HG_ASYNCH;
            pCmd->rc = reset();
            return VINF_SUCCESS;
        }
        default:
            break;
    }
    /* indicate that we process and complete the command asynchronously */
    pCmd->Flags |= VBOXVHWACMD_FLAG_HG_ASYNCH;

    mCmdPipe.postCmd(VBOXVHWA_PIPECMD_VHWA, pCmd, flags);
    return VINF_SUCCESS;

}

void VBoxQGLOverlay::onVHWACommandEvent(QEvent * pEvent)
{
    Q_UNUSED(pEvent);
    Assert(!mProcessingCommands);
    mProcessingCommands = true;
    Assert(!mGlCurrent);
    mGlCurrent = false; /* just a fall-back */
    bool bFirstCmd = true;
    VBoxVHWACommandElement *pLast;
    VBoxVHWACommandElement * pFirst = mCmdPipe.detachCmdList(&pLast, NULL, NULL);
    while(pFirst) /* pFirst can be zero right after reset when all pending commands are flushed,
                   * while events for those commands may still come along */
    {
        VBoxVHWACommandElement * pLastProcessed = processCmdList(pFirst, bFirstCmd);

        if (pLastProcessed == pLast)
        {
            pFirst = mCmdPipe.detachCmdList(&pLast, pFirst, pLastProcessed);
            bFirstCmd = false;
        }
        else
        {
            mCmdPipe.putBack(pLastProcessed->mpNext, pLast, pFirst, pLastProcessed);
            break;
        }
    }

    mProcessingCommands = false;
    repaint();
//    vboxOpExit();
    mGlCurrent = false;
}

bool VBoxQGLOverlay::onNotifyUpdate(ULONG aX, ULONG aY,
                         ULONG aW, ULONG aH)
{
#if 1
    QRect r(aX, aY, aW, aH);
    mCmdPipe.postCmd(VBOXVHWA_PIPECMD_PAINT, &r, 0);
    return true;
#else
    /* We're not on the GUI thread and update() isn't thread safe in
     * Qt 4.3.x on the Win, Qt 3.3.x on the Mac (4.2.x is),
     * on Linux (didn't check Qt 4.x there) and probably on other
     * non-DOS platforms, so post the event instead. */
    QApplication::postEvent (mView,
                             new VBoxRepaintEvent (aX, aY, aW, aH));

    return S_OK;
#endif
}

void VBoxQGLOverlay::onResizeEventPostprocess (const VBoxFBSizeInfo &re, const QPoint & topLeft)
{
    mSizeInfo = re;
    mContentsTopLeft = topLeft;

    if(mGlOn)
    {
        Assert(!mGlCurrent);
        Assert(!mNeedOverlayRepaint);
        mGlCurrent = false;
        makeCurrent();
        /* need to ensure we're in synch */
        mNeedOverlayRepaint = vboxSynchGl();
    }

    if(!mOnResizeCmdList.empty())
    {
        for (VHWACommandList::const_iterator it = mOnResizeCmdList.begin();
             it != mOnResizeCmdList.end(); ++ it)
        {
            VBOXVHWACMD * pCmd = (*it);
            vboxDoVHWACmdExec(pCmd);
            free(pCmd);
        }
        mOnResizeCmdList.clear();
    }

    repaintOverlay();
    mGlCurrent = false;
}

void VBoxQGLOverlay::repaintMain()
{
    if(mMainDirtyRect.isClear())
        return;

    const QRect &rect = mMainDirtyRect.rect();
    if(mOverlayWidgetVisible)
    {
        if(mOverlayViewport.contains(rect))
            return;
    }

    mpViewport->repaint (rect.x() - mContentsTopLeft.x(),
            rect.y() - mContentsTopLeft.y(),
            rect.width(), rect.height());

    mMainDirtyRect.clear();
}

void VBoxQGLOverlay::vboxDoVHWACmd(void *cmd)
{
    vboxDoVHWACmdExec(cmd);

    CDisplay display = mpSession->GetConsole().GetDisplay();
    Assert (!display.isNull());

    display.CompleteVHWACommand((BYTE*)cmd);
}

bool VBoxQGLOverlay::vboxSynchGl()
{
    VBoxVHWASurfaceBase * pVGA = mOverlayImage.vgaSurface();
    if(pVGA
            && mSizeInfo.pixelFormat() == pVGA->pixelFormat().toVBoxPixelFormat()
            && mSizeInfo.VRAM() == pVGA->address()
            && mSizeInfo.bitsPerPixel() == pVGA->bitsPerPixel()
            && mSizeInfo.bytesPerLine() == pVGA->bytesPerLine()
            && mSizeInfo.width() == pVGA->width()
            && mSizeInfo.height() == pVGA->height()
            )
    {
        return false;
    }
    /* create and issue a resize event to the gl widget to ensure we have all gl data initialized
     * and synchronized with the framebuffer */
    mOverlayImage.resize(mSizeInfo);
    return true;
}

void VBoxQGLOverlay::vboxSetGlOn(bool on)
{
    if(on == mGlOn)
        return;

    mGlOn = on;

    if(on)
    {
        /* need to ensure we have gl functions initialized */
        mpOverlayWgt->makeCurrent();
        vboxVHWAGetSupportInfo(mpOverlayWgt->context());

        VBOXQGLLOGREL(("Switching Gl mode on\n"));
        Assert(!mpOverlayWgt->isVisible());
        /* just to ensure */
        vboxShowOverlay(false);
        mOverlayVisible = false;
        vboxSynchGl();
    }
    else
    {
        VBOXQGLLOGREL(("Switching Gl mode off\n"));
        mOverlayVisible = false;
        vboxShowOverlay(false);
        /* for now just set the flag w/o destroying anything */
    }
}

void VBoxQGLOverlay::vboxDoCheckUpdateViewport()
{
    if(!mOverlayVisible)
    {
        vboxShowOverlay(false);
        return;
    }

    int cX = mContentsTopLeft.x();
    int cY = mContentsTopLeft.y();
    QRect fbVp(cX, cY, mpViewport->width(), mpViewport->height());
    QRect overVp = fbVp.intersected(mOverlayViewport);

    if(overVp.isEmpty())
    {
        vboxShowOverlay(false);
    }
    else
    {
        if(overVp != mOverlayImage.vboxViewport())
        {
            makeCurrent();
            mOverlayImage.vboxDoUpdateViewport(overVp);
            mNeedOverlayRepaint = true;
        }

        QRect rect(overVp.x() - cX, overVp.y() - cY, overVp.width(), overVp.height());

        vboxCheckUpdateOverlay(rect);

        vboxShowOverlay(true);

        /* workaround for linux ATI issue: need to update gl viewport after widget becomes visible */
        mOverlayImage.vboxDoUpdateViewport(overVp);
    }
}

void VBoxQGLOverlay::vboxShowOverlay(bool show)
{
    if(mOverlayWidgetVisible != show)
    {
        mpOverlayWgt->setVisible(show);
        mOverlayWidgetVisible = show;
        mGlCurrent = false;
        if(!show)
        {
            mMainDirtyRect.add(mOverlayImage.vboxViewport());
        }
    }
}

void VBoxQGLOverlay::vboxCheckUpdateOverlay(const QRect & rect)
{
    QRect overRect(mpOverlayWgt->pos(), mpOverlayWgt->size());
    if(overRect.x() != rect.x() || overRect.y() != rect.y())
    {
#if defined(RT_OS_WINDOWS)
        mpOverlayWgt->setVisible(false);
        mNeedSetVisible = true;
#endif
        VBOXQGLLOG_QRECT("moving wgt to " , &rect, "\n");
        mpOverlayWgt->move(rect.x(), rect.y());
        mGlCurrent = false;
    }

    if(overRect.width() != rect.width() || overRect.height() != rect.height())
    {
#if defined(RT_OS_WINDOWS)
        mpOverlayWgt->setVisible(false);
        mNeedSetVisible = true;
#endif
        VBOXQGLLOG(("resizing wgt to w(%d) ,h(%d)\n" , rect.width(), rect.height()));
        mpOverlayWgt->resize(rect.width(), rect.height());
        mGlCurrent = false;
    }
}

void VBoxQGLOverlay::addMainDirtyRect(const QRect & aRect)
{
    mMainDirtyRect.add(aRect);
    if(mGlOn)
    {
        mOverlayImage.vboxDoUpdateRect(&aRect);
        mNeedOverlayRepaint = true;
    }
}

int VBoxQGLOverlay::vhwaSurfaceUnlock(struct _VBOXVHWACMD_SURF_UNLOCK *pCmd)
{
    int rc = mOverlayImage.vhwaSurfaceUnlock(pCmd);
    VBoxVHWASurfaceBase * pVGA = mOverlayImage.vgaSurface();
    const VBoxVHWADirtyRect & rect = pVGA->getDirtyRect();
    mNeedOverlayRepaint = true;
    if(!rect.isClear())
    {
        mMainDirtyRect.add(rect);
    }
    return rc;
}

void VBoxQGLOverlay::vboxDoVHWACmdExec(void *cmd)
{
    struct _VBOXVHWACMD * pCmd = (struct _VBOXVHWACMD *)cmd;
    switch(pCmd->enmCmd)
    {
        case VBOXVHWACMD_TYPE_SURF_CANCREATE:
        {
            VBOXVHWACMD_SURF_CANCREATE * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_CANCREATE);
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaSurfaceCanCreate(pBody);
        } break;
        case VBOXVHWACMD_TYPE_SURF_CREATE:
        {
            VBOXVHWACMD_SURF_CREATE * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_CREATE);
            initGl();
            makeCurrent();
            vboxSetGlOn(true);
            pCmd->rc = mOverlayImage.vhwaSurfaceCreate(pBody);
            if(!mOverlayImage.hasSurfaces())
            {
                vboxSetGlOn(false);
            }
            else
            {
                mOverlayVisible = mOverlayImage.hasVisibleOverlays();
                if(mOverlayVisible)
                {
                    mOverlayViewport = mOverlayImage.overlaysRectUnion();
                }
                vboxDoCheckUpdateViewport();
                mNeedOverlayRepaint = true;
            }
        } break;
        case VBOXVHWACMD_TYPE_SURF_DESTROY:
        {
            VBOXVHWACMD_SURF_DESTROY * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_DESTROY);
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaSurfaceDestroy(pBody);
            if(!mOverlayImage.hasSurfaces())
            {
                vboxSetGlOn(false);
            }
            else
            {
                mOverlayVisible = mOverlayImage.hasVisibleOverlays();
                if(mOverlayVisible)
                {
                    mOverlayViewport = mOverlayImage.overlaysRectUnion();
                }
                vboxDoCheckUpdateViewport();
                mNeedOverlayRepaint = true;
            }
        } break;
        case VBOXVHWACMD_TYPE_SURF_LOCK:
        {
            VBOXVHWACMD_SURF_LOCK * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_LOCK);
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaSurfaceLock(pBody);
        } break;
        case VBOXVHWACMD_TYPE_SURF_UNLOCK:
        {
            VBOXVHWACMD_SURF_UNLOCK * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_UNLOCK);
            initGl();
            makeCurrent();
            pCmd->rc = vhwaSurfaceUnlock(pBody);
            /* mNeedOverlayRepaint is set inside the vhwaSurfaceUnlock */
        } break;
        case VBOXVHWACMD_TYPE_SURF_BLT:
        {
            VBOXVHWACMD_SURF_BLT * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_BLT);
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaSurfaceBlt(pBody);
            mNeedOverlayRepaint = true;
        } break;
        case VBOXVHWACMD_TYPE_SURF_FLIP:
        {
            VBOXVHWACMD_SURF_FLIP * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_FLIP);
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaSurfaceFlip(pBody);
            mNeedOverlayRepaint = true;
        } break;
        case VBOXVHWACMD_TYPE_SURF_OVERLAY_UPDATE:
        {
            VBOXVHWACMD_SURF_OVERLAY_UPDATE * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_OVERLAY_UPDATE);
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaSurfaceOverlayUpdate(pBody);
            mOverlayVisible = mOverlayImage.hasVisibleOverlays();
            if(mOverlayVisible)
            {
                mOverlayViewport = mOverlayImage.overlaysRectUnion();
            }
            vboxDoCheckUpdateViewport();
            mNeedOverlayRepaint = true;
        } break;
        case VBOXVHWACMD_TYPE_SURF_OVERLAY_SETPOSITION:
        {
            VBOXVHWACMD_SURF_OVERLAY_SETPOSITION * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_OVERLAY_SETPOSITION);
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaSurfaceOverlaySetPosition(pBody);
            mOverlayVisible = mOverlayImage.hasVisibleOverlays();
            if(mOverlayVisible)
            {
                mOverlayViewport = mOverlayImage.overlaysRectUnion();
            }
            vboxDoCheckUpdateViewport();
            mNeedOverlayRepaint = true;
        } break;
#ifdef VBOX_WITH_WDDM
        case VBOXVHWACMD_TYPE_SURF_COLORFILL:
        {
            VBOXVHWACMD_SURF_COLORFILL * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_COLORFILL);
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaSurfaceColorFill(pBody);
            mNeedOverlayRepaint = true;
        } break;
#endif
        case VBOXVHWACMD_TYPE_SURF_COLORKEY_SET:
        {
            VBOXVHWACMD_SURF_COLORKEY_SET * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_COLORKEY_SET);
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaSurfaceColorkeySet(pBody);
            /* this is here to ensure we have color key changes picked up */
            vboxDoCheckUpdateViewport();
            mNeedOverlayRepaint = true;
        } break;
        case VBOXVHWACMD_TYPE_QUERY_INFO1:
        {
            VBOXVHWACMD_QUERYINFO1 * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_QUERYINFO1);
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaQueryInfo1(pBody);
        } break;
        case VBOXVHWACMD_TYPE_QUERY_INFO2:
        {
            VBOXVHWACMD_QUERYINFO2 * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_QUERYINFO2);
            initGl();
            makeCurrent();
            pCmd->rc = mOverlayImage.vhwaQueryInfo2(pBody);
        } break;
        case VBOXVHWACMD_TYPE_ENABLE:
            initGl();
        case VBOXVHWACMD_TYPE_DISABLE:
            pCmd->rc = VINF_SUCCESS;
            break;
        case VBOXVHWACMD_TYPE_HH_CONSTRUCT:
        {
            VBOXVHWACMD_HH_CONSTRUCT * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_HH_CONSTRUCT);
            pCmd->rc = vhwaConstruct(pBody);
        } break;
#ifdef VBOX_WITH_WDDM
        case VBOXVHWACMD_TYPE_SURF_GETINFO:
        {
            VBOXVHWACMD_SURF_GETINFO * pBody = VBOXVHWACMD_BODY(pCmd, VBOXVHWACMD_SURF_GETINFO);
            pCmd->rc = mOverlayImage.vhwaSurfaceGetInfo(pBody);
        } break;
#endif
        default:
            Assert(0);
            pCmd->rc = VERR_NOT_IMPLEMENTED;
            break;
    }
}

static DECLCALLBACK(void) vboxQGLOverlaySaveExec(PSSMHANDLE pSSM, void *pvUser)
{
    VBoxQGLOverlay * fb = (VBoxQGLOverlay*)pvUser;
    fb->vhwaSaveExec(pSSM);
}

static DECLCALLBACK(int) vboxQGLOverlayLoadExec(PSSMHANDLE pSSM, void *pvUser, uint32_t u32Version, uint32_t uPass)
{
    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);
    VBoxQGLOverlay * fb = (VBoxQGLOverlay*)pvUser;
    return fb->vhwaLoadExec(pSSM, u32Version);
}

int VBoxQGLOverlay::vhwaLoadExec(struct SSMHANDLE * pSSM, uint32_t u32Version)
{
    return VBoxVHWAImage::vhwaLoadExec(&mOnResizeCmdList, pSSM, u32Version);
}

void VBoxQGLOverlay::vhwaSaveExec(struct SSMHANDLE * pSSM)
{
    mOverlayImage.vhwaSaveExec(pSSM);
}

int VBoxQGLOverlay::vhwaConstruct(struct _VBOXVHWACMD_HH_CONSTRUCT *pCmd)
{
    PVM pVM = (PVM)pCmd->pVM;
    uint32_t intsId = m_id;

    char nameFuf[sizeof(VBOXQGL_STATE_NAMEBASE) + 8];

    char * pszName = nameFuf;
    sprintf(pszName, "%s%d", VBOXQGL_STATE_NAMEBASE, intsId);
    int rc = SSMR3RegisterExternal(
            pVM,                    /* The VM handle*/
            pszName,                /* Data unit name. */
            intsId,                 /* The instance identifier of the data unit.
                                     * This must together with the name be unique. */
            VBOXQGL_STATE_VERSION,   /* Data layout version number. */
            128,             /* The approximate amount of data in the unit.
                              * Only for progress indicators. */
            NULL, NULL, NULL, /* pfnLiveXxx */
            NULL,            /* Prepare save callback, optional. */
            vboxQGLOverlaySaveExec, /* Execute save callback, optional. */
            NULL,            /* Done save callback, optional. */
            NULL,            /* Prepare load callback, optional. */
            vboxQGLOverlayLoadExec, /* Execute load callback, optional. */
            NULL,            /* Done load callback, optional. */
            this             /* User argument. */
            );
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        rc = mOverlayImage.vhwaConstruct(pCmd);
        AssertRC(rc);
    }
    return rc;
}

/* static */
bool VBoxQGLOverlay::isAcceleration2DVideoAvailable()
{
#ifndef DEBUG_misha
    if(!g_bVBoxVHWAChecked)
#endif
    {
        g_bVBoxVHWAChecked = true;
        g_bVBoxVHWASupported = VBoxVHWAInfo::checkVHWASupport();
    }
    return g_bVBoxVHWASupported;
}

/** additional video memory required for the best 2D support performance
 *  total amount of VRAM required is thus calculated as requiredVideoMemory + required2DOffscreenVideoMemory  */
/* static */
quint64 VBoxQGLOverlay::required2DOffscreenVideoMemory()
{
    /* HDTV == 1920x1080 ~ 2M
     * for the 4:2:2 formats each pixel is 2Bytes
     * so each frame may be 4MiB
     * so for triple-buffering we would need 12 MiB */
    return _1M * 12;
}

VBoxVHWACommandElement * VBoxQGLOverlay::processCmdList(VBoxVHWACommandElement * pCmd, bool bFirst)
{
    VBoxVHWACommandElement * pCur;
    do
    {
        pCur = pCmd;
        switch(pCmd->type())
        {
        case VBOXVHWA_PIPECMD_PAINT:
            addMainDirtyRect(pCmd->rect());
            break;
#ifdef VBOX_WITH_VIDEOHWACCEL
        case VBOXVHWA_PIPECMD_VHWA:
            vboxDoVHWACmd(pCmd->vhwaCmd());
            break;
        case VBOXVHWA_PIPECMD_FUNC:
        {
            const VBOXVHWAFUNCCALLBACKINFO & info = pCmd->func();
            info.pfnCallback(info.pContext1, info.pContext2);
            break;
        }
#endif
        default:
            Assert(0);
        }

        pCmd = pCmd->mpNext;
        if (!pCmd)
            break;

        if (!bFirst)
        {
            if (pCmd->isNewEvent())
                break;
        }
        else
        {
            Assert(pCur->isNewEvent());
            bFirst = false;
        }
    } while(1);

    return pCur;
}


VBoxVHWACommandElementProcessor::VBoxVHWACommandElementProcessor(QObject *pNotifyObject) :
    m_pNotifyObject(pNotifyObject),
    mbNewEvent (false),
    mbProcessingList (false)
{
    int rc = RTCritSectInit(&mCritSect);
    AssertRC(rc);

    for(int i = RT_ELEMENTS(mElementsBuffer) - 1; i >= 0; i--)
    {
        mFreeElements.push(&mElementsBuffer[i]);
    }
}

VBoxVHWACommandElementProcessor::~VBoxVHWACommandElementProcessor()
{
    Assert(!m_NotifyObjectRefs.refs());
    Assert(m_CmdPipe.isEmpty());
    RTCritSectDelete(&mCritSect);
}

bool VBoxVHWACommandElementProcessor::completeCurrentEvent()
{
    bool bActive = true;
    RTCritSectEnter(&mCritSect);
    mbNewEvent = true;
    if (!m_pNotifyObject)
        bActive = false;
    RTCritSectLeave(&mCritSect);
    return bActive;
}

void VBoxVHWACommandElementProcessor::postCmd(VBOXVHWA_PIPECMD_TYPE aType, void * pvData, uint32_t flags)
{
    QObject *pNotifyObject = NULL;
    /* 1. lock*/
    RTCritSectEnter(&mCritSect);
    VBoxVHWACommandElement * pCmd = mFreeElements.pop();
    if(!pCmd)
    {
        VBOXQGLLOG(("!!!no more free elements!!!\n"));
#ifdef VBOXQGL_PROF_BASE
        RTCritSectLeave(&mCritSect);
        return;
#else
    //TODO:
#endif
    }
    pCmd->setData(aType, pvData);

    if((flags & VBOXVHWACMDPIPEC_NEWEVENT) != 0)
        mbNewEvent = true;

    /* 2. if can add to current*/
    if(mbNewEvent || (!mbProcessingList && m_CmdPipe.isEmpty()))
    {
        pCmd->setNewEvent(true);
        mbNewEvent = false;
        if (m_pNotifyObject)
        {
            m_NotifyObjectRefs.inc(); /* ensure the parent does not get destroyed while we are using it */
            pNotifyObject = m_pNotifyObject;
#ifdef DEBUG_misha
            checkConsistence();
#endif
        }
    }
    else
    {
        pCmd->setNewEvent(false);
#ifdef DEBUG_misha
        if (m_pNotifyObject)
            checkConsistence();
#endif
    }

    m_CmdPipe.put(pCmd);
#ifdef DEBUG_misha
    if (m_pNotifyObject)
    {
        checkConsistence(1);
    }
#endif

    if((flags & VBOXVHWACMDPIPEC_COMPLETEEVENT) != 0)
        mbNewEvent = true;

    RTCritSectLeave(&mCritSect);

    if (pNotifyObject)
    {
        VBoxVHWACommandProcessEvent *pCurrentEvent = new VBoxVHWACommandProcessEvent();
        QApplication::postEvent (pNotifyObject, pCurrentEvent);
        m_NotifyObjectRefs.dec();
    }
}

#ifdef DEBUG_misha
void VBoxVHWACommandElementProcessor::checkConsistence(uint32_t cEvents2Submit, const VBoxVHWACommandElementPipe *pPipe)
{
    const VBoxVHWACommandElement *pLast;
    const VBoxVHWACommandElement *pFirst = pPipe ? pPipe->contentsRo(&pLast) : m_CmdPipe.contentsRo(&pLast);
    uint32_t cEvents = 0;

    for (const VBoxVHWACommandElement * pCur = pFirst; pCur; pCur = pCur->mpNext)
    {
        if (pCur->isNewEvent())
        {
            ++cEvents;
            Assert(cEvents <= VBoxVHWACommandProcessEvent::cPending() + cEvents2Submit);
        }
    }
//    Assert(cEvents == VBoxVHWACommandProcessEvent::cPending());
}
#endif

void VBoxVHWACommandElementProcessor::putBack(class VBoxVHWACommandElement * pFirst2Put, VBoxVHWACommandElement * pLast2Put,
        class VBoxVHWACommandElement * pFirst2Free, VBoxVHWACommandElement * pLast2Free)
{
    RTCritSectEnter(&mCritSect);
    if (pFirst2Free)
        mFreeElements.pusha(pFirst2Free, pLast2Free);
    m_CmdPipe.prepend(pFirst2Put, pLast2Put);
    mbProcessingList = false;
    Assert(pFirst2Put->isNewEvent());
#ifdef DEBUG_misha
    Assert(VBoxVHWACommandProcessEvent::cPending());
    const VBoxVHWACommandElement *pLast;
    const VBoxVHWACommandElement *pFirst = m_CmdPipe.contentsRo(&pLast);
    Assert(pFirst);
    Assert(pLast);
    Assert(pFirst == pFirst2Put);
    checkConsistence();
#endif
    RTCritSectLeave(&mCritSect);
}

void VBoxVHWACommandElementProcessor::setNotifyObject(QObject *pNotifyObject)
{
    int cEventsNeeded = 0;
    const VBoxVHWACommandElement * pFirst;
    RTCritSectEnter(&mCritSect);
    if (m_pNotifyObject == pNotifyObject)
    {
        RTCritSectLeave(&mCritSect);
        return;
    }

    if (m_pNotifyObject)
    {
        m_pNotifyObject = NULL;
        RTCritSectLeave(&mCritSect);

        m_NotifyObjectRefs.wait0();

        RTCritSectEnter(&mCritSect);
    }
    else
    {
        /* NULL can not be references */
        Assert (!m_NotifyObjectRefs.refs());
    }

    if (pNotifyObject)
    {
        m_pNotifyObject = pNotifyObject;

        pFirst = m_CmdPipe.contentsRo(NULL);
        for (; pFirst; pFirst = pFirst->mpNext)
        {
            if (pFirst->isNewEvent())
                ++cEventsNeeded;
        }

        if (cEventsNeeded)
            m_NotifyObjectRefs.inc();
    }
    else
    {
        /* should be zeroed already */
        Assert(!m_pNotifyObject);
    }

#ifdef DEBUG_misha
    checkConsistence(cEventsNeeded);
#endif

    RTCritSectLeave(&mCritSect);

    if (cEventsNeeded)
    {
        /* cEventsNeeded can only be != 0 if pNotifyObject is valid */
        Assert (pNotifyObject);
        for (int i = 0; i < cEventsNeeded; ++i)
        {
            VBoxVHWACommandProcessEvent *pCurrentEvent = new VBoxVHWACommandProcessEvent();
            QApplication::postEvent(pNotifyObject, pCurrentEvent);
        }
        m_NotifyObjectRefs.dec();
    }
}

VBoxVHWACommandElement * VBoxVHWACommandElementProcessor::detachCmdList(VBoxVHWACommandElement **ppLast,
        VBoxVHWACommandElement * pFirst2Free, VBoxVHWACommandElement * pLast2Free)
{
    VBoxVHWACommandElement * pList = NULL;
    RTCritSectEnter(&mCritSect);
    if (pFirst2Free)
    {
        mFreeElements.pusha(pFirst2Free, pLast2Free);
    }

#ifdef DEBUG_misha
    checkConsistence();
#endif

    pList = m_CmdPipe.detachList(ppLast);

    if (pList)
    {
        /* assume the caller atimically calls detachCmdList to free the elements obtained now those and reset the state */
        mbProcessingList = true;
        RTCritSectLeave(&mCritSect);
        return pList;
    }
    else
    {
        mbProcessingList = false;
    }

    RTCritSectLeave(&mCritSect);
    return NULL;
}

/* it is currently assumed no one sends any new commands while reset is in progress */
void VBoxVHWACommandElementProcessor::reset(VBoxVHWACommandElement ** ppHead, VBoxVHWACommandElement ** ppTail)
{
    VBoxVHWACommandElementPipe pipe;
    RTCritSectEnter(&mCritSect);

    pipe.setFrom(&m_CmdPipe);

    if(mbProcessingList)
    {
        for(;;)
        {
            RTCritSectLeave(&mCritSect);
            RTThreadSleep(2000); /* 2 ms */
            RTCritSectEnter(&mCritSect);
            /* it is assumed no one sends any new commands while reset is in progress */
            if(!mbProcessingList)
            {
                break;
            }
        }
    }

    Assert(!mbProcessingList);

    pipe.prependFrom(&m_CmdPipe);

    if(!pipe.isEmpty())
        mbProcessingList = true;

    RTCritSectLeave(&mCritSect);

    *ppHead = pipe.detachList(ppTail);
}

VBoxVHWATextureImage::VBoxVHWATextureImage(const QRect &size, const VBoxVHWAColorFormat &format, class VBoxVHWAGlProgramMngr * aMgr, VBOXVHWAIMG_TYPE flags) :
        mVisibleDisplay(0),
        mpProgram(0),
        mProgramMngr(aMgr),
        mpDst(NULL),
        mpDstCKey(NULL),
        mpSrcCKey(NULL),
        mbNotIntersected(false)
{
    mpTex[0] = vboxVHWATextureCreate(NULL, size, format, flags);
    mColorFormat = format;
    if(mColorFormat.fourcc() == FOURCC_YV12)
    {
        QRect rect(size.x()/2,size.y()/2,size.width()/2,size.height()/2);
        mpTex[1] = vboxVHWATextureCreate(NULL, rect, format, flags);
        mpTex[2] = vboxVHWATextureCreate(NULL, rect, format, flags);
        mcTex = 3;
    }
    else
    {
        mcTex = 1;
    }
}

void VBoxVHWATextureImage::deleteDisplayList()
{
    if(mVisibleDisplay)
    {
        glDeleteLists(mVisibleDisplay, 1);
        mVisibleDisplay = 0;
    }
}

void VBoxVHWATextureImage::deleteDisplay()
{
    deleteDisplayList();
    mpProgram = NULL;
}

void VBoxVHWATextureImage::draw(VBoxVHWATextureImage *pDst, const QRect * pDstRect, const QRect * pSrcRect)
{
    int tx1, ty1, tx2, ty2;
    pSrcRect->getCoords(&tx1, &ty1, &tx2, &ty2);
    int bx1, by1, bx2, by2;
    pDstRect->getCoords(&bx1, &by1, &bx2, &by2);
    tx2++; ty2++;bx2++; by2++;

    glBegin(GL_QUADS);
    uint32_t c = texCoord(GL_TEXTURE0, tx1, ty1);
    if(pDst)
    {
        pDst->texCoord(GL_TEXTURE0 + c, bx1, by1);
    }
    glVertex2i(bx1, by1);

    texCoord(GL_TEXTURE0, tx1, ty2);
    if(pDst)
    {
        pDst->texCoord(GL_TEXTURE0 + c, bx1, by2);
    }
    glVertex2i(bx1, by2);

    texCoord(GL_TEXTURE0, tx2, ty2);
    if(pDst)
    {
        pDst->texCoord(GL_TEXTURE0 + c, bx2, by2);
    }
    glVertex2i(bx2, by2);

    texCoord(GL_TEXTURE0, tx2, ty1);
    if(pDst)
    {
        pDst->texCoord(GL_TEXTURE0 + c, bx2, by1);
    }
    glVertex2i(bx2, by1);

    glEnd();
}

void VBoxVHWATextureImage::internalSetDstCKey(const VBoxVHWAColorKey * pDstCKey)
{
    if(pDstCKey)
    {
        mDstCKey = *pDstCKey;
        mpDstCKey = &mDstCKey;
    }
    else
    {
        mpDstCKey = NULL;
    }
}

void VBoxVHWATextureImage::internalSetSrcCKey(const VBoxVHWAColorKey * pSrcCKey)
{
    if(pSrcCKey)
    {
        mSrcCKey = *pSrcCKey;
        mpSrcCKey = &mSrcCKey;
    }
    else
    {
        mpSrcCKey = NULL;
    }
}

int VBoxVHWATextureImage::initDisplay(VBoxVHWATextureImage *pDst,
        const QRect * pDstRect, const QRect * pSrcRect,
        const VBoxVHWAColorKey * pDstCKey, const VBoxVHWAColorKey * pSrcCKey, bool bNotIntersected)
{
    if(!mVisibleDisplay
            || mpDst != pDst
            || *pDstRect != mDstRect
            || *pSrcRect != mSrcRect
            || !!(pDstRect) != !!(mpDstCKey)
            || !!(pSrcCKey) != !!(mpSrcCKey)
            || mbNotIntersected != bNotIntersected)
    {
        return createSetDisplay(pDst, pDstRect, pSrcRect,
                pDstCKey, pSrcCKey, bNotIntersected);

    }
    else if((pDstCKey && mpDstCKey && *pDstCKey == *mpDstCKey)
            || (pSrcCKey && mpSrcCKey && *pSrcCKey == *mpSrcCKey))
    {
        Assert(mpProgram);
        updateSetCKeys(pDstCKey, pSrcCKey);
        return VINF_SUCCESS;
    }
    mVisibleDisplay = 0;
    mpProgram = 0;
    return VINF_SUCCESS;
}

void VBoxVHWATextureImage::bind(VBoxVHWATextureImage * pPrimary)
{
    for(uint32_t i = 1; i < mcTex; i++)
    {
        vboxglActiveTexture(GL_TEXTURE0 + i);
        mpTex[i]->bind();
    }
    if(pPrimary)
    {
        for(uint32_t i = 0; i < pPrimary->mcTex; i++)
        {
            vboxglActiveTexture(GL_TEXTURE0 + i + mcTex);
            pPrimary->mpTex[i]->bind();
        }
    }

    vboxglActiveTexture(GL_TEXTURE0);
    mpTex[0]->bind();
}

uint32_t VBoxVHWATextureImage::calcProgramType(VBoxVHWATextureImage *pDst, const VBoxVHWAColorKey * pDstCKey, const VBoxVHWAColorKey * pSrcCKey, bool bNotIntersected)
{
    uint32_t type = 0;

    if(pDstCKey != NULL)
        type |= VBOXVHWA_PROGRAM_DSTCOLORKEY;
    if(pSrcCKey)
        type |= VBOXVHWA_PROGRAM_SRCCOLORKEY;
    if((pDstCKey || pSrcCKey) && bNotIntersected)
        type |= VBOXVHWA_PROGRAM_COLORKEYNODISCARD;

    NOREF(pDst);
    return type;
}

class VBoxVHWAGlProgramVHWA * VBoxVHWATextureImage::calcProgram(VBoxVHWATextureImage *pDst, const VBoxVHWAColorKey * pDstCKey, const VBoxVHWAColorKey * pSrcCKey, bool bNotIntersected)
{
    uint32_t type = calcProgramType(pDst, pDstCKey, pSrcCKey, bNotIntersected);

    return mProgramMngr->getProgram(type, &pixelFormat(), pDst ? &pDst->pixelFormat() : NULL);
}

int VBoxVHWATextureImage::createSetDisplay(VBoxVHWATextureImage *pDst, const QRect * pDstRect, const QRect * pSrcRect,
        const VBoxVHWAColorKey * pDstCKey, const VBoxVHWAColorKey * pSrcCKey, bool bNotIntersected)
{
    deleteDisplay();
    int rc = createDisplay(pDst, pDstRect, pSrcRect,
            pDstCKey, pSrcCKey, bNotIntersected,
            &mVisibleDisplay, &mpProgram);
    if(RT_FAILURE(rc))
    {
        mVisibleDisplay = 0;
        mpProgram = NULL;
    }

    mpDst = pDst;

    mDstRect = *pDstRect;
    mSrcRect = *pSrcRect;

    internalSetDstCKey(pDstCKey);
    internalSetSrcCKey(pSrcCKey);

    mbNotIntersected = bNotIntersected;

    return rc;
}


int VBoxVHWATextureImage::createDisplayList(VBoxVHWATextureImage *pDst, const QRect * pDstRect, const QRect * pSrcRect,
        const VBoxVHWAColorKey * pDstCKey, const VBoxVHWAColorKey * pSrcCKey, bool bNotIntersected,
        GLuint *pDisplay)
{
    Q_UNUSED(pDstCKey);
    Q_UNUSED(pSrcCKey);
    Q_UNUSED(bNotIntersected);

    glGetError(); /* clear the err flag */
    GLuint display = glGenLists(1);
    GLenum err = glGetError();
    if(err == GL_NO_ERROR)
    {
        Assert(display);
        if(!display)
        {
            /* well, it seems it should not return 0 on success according to the spec,
             * but just in case, pick another one */
            display = glGenLists(1);
            err = glGetError();
            if(err == GL_NO_ERROR)
            {
                Assert(display);
            }
            else
            {
                /* we are failed */
                Assert(!display);
                display = 0;
            }
        }

        if(display)
        {
            glNewList(display, GL_COMPILE);

            runDisplay(pDst, pDstRect, pSrcRect);

            glEndList();
            VBOXQGL_ASSERTNOERR();
            *pDisplay = display;
            return VINF_SUCCESS;
        }
    }
    else
    {
        VBOXQGLLOG(("gl error ocured (0x%x)\n", err));
        Assert(err == GL_NO_ERROR);
    }
    return VERR_GENERAL_FAILURE;
}

void VBoxVHWATextureImage::updateCKeys(VBoxVHWATextureImage * pDst, class VBoxVHWAGlProgramVHWA * pProgram, const VBoxVHWAColorKey * pDstCKey, const VBoxVHWAColorKey * pSrcCKey)
{
    if(pProgram)
    {
        pProgram->start();
        if(pSrcCKey)
        {
            VBoxVHWATextureImage::setCKey(pProgram, &pixelFormat(), pSrcCKey, false);
        }
        if(pDstCKey)
        {
            VBoxVHWATextureImage::setCKey(pProgram, &pDst->pixelFormat(), pDstCKey, true);
        }
        pProgram->stop();
    }
}

void VBoxVHWATextureImage::updateSetCKeys(const VBoxVHWAColorKey * pDstCKey, const VBoxVHWAColorKey * pSrcCKey)
{
    updateCKeys(mpDst, mpProgram, pDstCKey, pSrcCKey);
    internalSetDstCKey(pDstCKey);
    internalSetSrcCKey(pSrcCKey);
}

int VBoxVHWATextureImage::createDisplay(VBoxVHWATextureImage *pDst, const QRect * pDstRect, const QRect * pSrcRect,
        const VBoxVHWAColorKey * pDstCKey, const VBoxVHWAColorKey * pSrcCKey, bool bNotIntersected,
        GLuint *pDisplay, class VBoxVHWAGlProgramVHWA ** ppProgram)
{
    VBoxVHWAGlProgramVHWA * pProgram = NULL;
    if (!pDst)
    {
        /* sanity */
        Assert(pDstCKey == NULL);
        pDstCKey = NULL;
    }

    Assert(!pSrcCKey);
    if (pSrcCKey)
        pSrcCKey = NULL; /* fallback */

    pProgram = calcProgram(pDst, pDstCKey, pSrcCKey, bNotIntersected);

    updateCKeys(pDst, pProgram, pDstCKey, pSrcCKey);

    GLuint displ;
    int rc = createDisplayList(pDst, pDstRect, pSrcRect,
            pDstCKey, pSrcCKey, bNotIntersected,
            &displ);
    if(RT_SUCCESS(rc))
    {
        *pDisplay = displ;
        *ppProgram = pProgram;
    }

    return rc;
}

void VBoxVHWATextureImage::display(VBoxVHWATextureImage *pDst, const QRect * pDstRect, const QRect * pSrcRect,
        const VBoxVHWAColorKey * pDstCKey, const VBoxVHWAColorKey * pSrcCKey, bool bNotIntersected)
{
    VBoxVHWAGlProgramVHWA * pProgram = calcProgram(pDst, pDstCKey, pSrcCKey, bNotIntersected);
    if(pProgram)
        pProgram->start();

    runDisplay(pDst, pDstRect, pSrcRect);

    if(pProgram)
        pProgram->stop();
}

void VBoxVHWATextureImage::display()
{
    Assert(mVisibleDisplay);
    if(mVisibleDisplay)
    {
        if(mpProgram)
            mpProgram->start();

        VBOXQGL_CHECKERR(
                glCallList(mVisibleDisplay);
                );

        if(mpProgram)
            mpProgram->stop();
    }
    else
    {
        display(mpDst, &mDstRect, &mSrcRect,
                mpDstCKey, mpSrcCKey, mbNotIntersected);
    }
}

int VBoxVHWATextureImage::setCKey (VBoxVHWAGlProgramVHWA * pProgram, const VBoxVHWAColorFormat * pFormat, const VBoxVHWAColorKey * pCKey, bool bDst)
{
    float r,g,b;
    pFormat->pixel2Normalized (pCKey->lower(), &r, &g, &b);
    int rcL = bDst ? pProgram->setDstCKeyLowerRange (r, g, b) : pProgram->setSrcCKeyLowerRange (r, g, b);
    Assert(RT_SUCCESS(rcL));

    return RT_SUCCESS(rcL) /*&& RT_SUCCESS(rcU)*/ ? VINF_SUCCESS: VERR_GENERAL_FAILURE;
}

VBoxVHWASettings::VBoxVHWASettings (CSession &session)
{
    CMachine machine = session.GetMachine();

    QString str = machine.GetExtraData (VBoxDefs::GUI_Accelerate2D_StretchLinear);
    mStretchLinearEnabled = str != "off";

    uint32_t aFourccs[VBOXVHWA_NUMFOURCC];
    int num = 0;
    str = machine.GetExtraData (VBoxDefs::GUI_Accelerate2D_PixformatAYUV);
    if (str != "off")
        aFourccs[num++] = FOURCC_AYUV;
    str = machine.GetExtraData (VBoxDefs::GUI_Accelerate2D_PixformatUYVY);
    if (str != "off")
        aFourccs[num++] = FOURCC_UYVY;
    str = machine.GetExtraData (VBoxDefs::GUI_Accelerate2D_PixformatYUY2);
    if (str != "off")
        aFourccs[num++] = FOURCC_YUY2;
    str = machine.GetExtraData (VBoxDefs::GUI_Accelerate2D_PixformatYV12);
    if (str != "off")
        aFourccs[num++] = FOURCC_YV12;

    mFourccEnabledCount = num;
    memcpy(mFourccEnabledList, aFourccs, num* sizeof (aFourccs[0]));
}

int VBoxVHWASettings::calcIntersection (int c1, const uint32_t *a1, int c2, const uint32_t *a2, int cOut, uint32_t *aOut)
{
    /* fourcc arrays are not big, so linear search is enough,
     * also no need to check for duplicates */
    int cMatch = 0;
    for (int i = 0; i < c1; ++i)
    {
        uint32_t cur1 = a1[i];
        for (int j = 0; j < c2; ++j)
        {
            uint32_t cur2 = a2[j];
            if(cur1 == cur2)
            {
                if(cOut > cMatch && aOut)
                    aOut[cMatch] = cur1;
                ++cMatch;
                break;
            }
        }
    }

    return cMatch;
}

#endif

