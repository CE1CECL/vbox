/** @file
 *
 * VBox frontends: Basic Frontend (BFE):
 * Implementation of SDLConsole class
 */

/*
 * Copyright (C) 2006 InnoTek Systemberatung GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
 * distribution. VirtualBox OSE is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * If you received this file as part of a commercial VirtualBox
 * distribution, then only the terms of your commercial VirtualBox
 * license agreement apply instead of the previous paragraph.
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_GUI

#ifdef VBOXBFE_WITHOUT_COM
# include "COMDefs.h"
#else
# include <VBox/com/defs.h>
#endif
#include <VBox/types.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/pdm.h>
#include <VBox/log.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/runtime.h>
#include <iprt/assert.h>
#include <iprt/semaphore.h>
#include <iprt/stream.h>
#include <iprt/uuid.h>
#include <iprt/alloca.h>

#ifdef VBOXBFE_WITH_X11
# include <X11/Xlib.h>
# include <X11/Xcursor/Xcursor.h>
#endif

#include "VBoxBFE.h"

#include <vector>

#include "DisplayImpl.h"
#include "MouseImpl.h"
#include "KeyboardImpl.h"
#include "VMMDevInterface.h"
#include "Framebuffer.h"
#include "MachineDebuggerImpl.h"

#include "ConsoleImpl.h"
#include "SDLConsole.h"

/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/

SDLConsole::SDLConsole() : Console()
{
    int rc;

    fInputGrab       = false;
    gpDefaultCursor  = NULL;
    gpCustomCursor   = NULL;
    /** Custom window manager cursor? */
    gpCustomWMcursor = NULL;
    mfInitialized    = false;

    memset(gaModifiersState, 0, sizeof(gaModifiersState));

    rc = SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE);
    if (rc != 0)
    {
        RTPrintf("SDL Error: '%s'\n", SDL_GetError());
        return;
    }

    /* memorize the default cursor */
    gpDefaultCursor = SDL_GetCursor();
    /* create a fake empty cursor */
    {
        uint8_t cursorData[1] = {0};
        gpCustomCursor = SDL_CreateCursor(cursorData, cursorData, 8, 1, 0, 0);
        gpCustomWMcursor = gpCustomCursor->wm_cursor;
        gpCustomCursor->wm_cursor = NULL;
    }
#ifdef VBOXBFE_WITH_X11
    /* get Window Manager info */
    SDL_VERSION(&gSdlInfo.version);
    if (!SDL_GetWMInfo(&gSdlInfo))
    {
        /** @todo: Is this fatal? */
        AssertMsgFailed(("Error: could not get SDL Window Manager info!\n"));
    }
#endif

    /*
     * Enable keyboard repeats
     */
    SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
    mfInitialized = true;
}

SDLConsole::~SDLConsole()
{
    if (fInputGrab)
        inputGrabEnd();
}

CONEVENT SDLConsole::eventWait()
{
    SDL_Event *ev = &ev1;

    if (SDL_WaitEvent(ev) != 1)
      {
        return CONEVENT_QUIT;
      }

    switch (ev->type)
    {

        /*
         * The screen needs to be repainted.
         */
        case SDL_VIDEOEXPOSE:
        {
            return CONEVENT_SCREENUPDATE;
        }

        /*
         * Keyboard events.
         */
        case SDL_KEYDOWN:
        case SDL_KEYUP:
        {
            switch (enmHKeyState)
            {
                case HKEYSTATE_NORMAL:
                {
                    if (    ev->type == SDL_KEYDOWN
                        &&  ev->key.keysym.sym == gHostKeySym
                        &&  (SDL_GetModState() & ~(KMOD_MODE | KMOD_NUM | KMOD_RESERVED)) == gHostKey)
                    {
		        EvHKeyDown = *ev;
                        enmHKeyState = HKEYSTATE_DOWN;
                        break;
                    }
                    processKey(&ev->key);
                    break;
                }

                case HKEYSTATE_DOWN:
                {

                    if (ev->type == SDL_KEYDOWN)
                    {
                        /* potential host key combination, try execute it */
                        int rc = handleHostKey(&ev->key);
                        if (rc == VINF_SUCCESS)
                        {
                            enmHKeyState = HKEYSTATE_USED;
                            break;
                        }
                        if (VBOX_SUCCESS(rc))
                        {
                            return CONEVENT_QUIT;
                        }
                    }
                    else /* SDL_KEYUP */
                    {
                        if (ev->key.keysym.sym == gHostKeySym)
                        {
                            /* toggle grabbing state */
                            if (!fInputGrab)
                            {
                                inputGrabStart();
                            }
                            else
                            {
                                inputGrabEnd();
                            }

                            /* SDL doesn't always reset the keystates, correct it */
                            resetKeys();
                            enmHKeyState = HKEYSTATE_NORMAL;
                            break;
                        }
                    }

                    /* not host key */
                    enmHKeyState = HKEYSTATE_NOT_IT;
                    ev1 = *ev;
                    processKey(&EvHKeyDown.key);
                    processKey(&ev->key);
                    break;
                }

                case HKEYSTATE_USED:
                {
                    if ((SDL_GetModState() & ~(KMOD_MODE | KMOD_NUM | KMOD_RESERVED)) == 0)
                    {
                        enmHKeyState = HKEYSTATE_NORMAL;
                    }
                    if (ev->type == SDL_KEYDOWN)
                    {
                        int rc = handleHostKey(&ev->key);
                        if (VBOX_SUCCESS(rc) && rc != VINF_SUCCESS)
                        {
                            return CONEVENT_QUIT;
                        }
                    }
                    break;
                }

                default:
                    AssertMsgFailed(("enmHKeyState=%d\n", enmHKeyState));
                    /* fall thru */
                case HKEYSTATE_NOT_IT:
                {
                    if ((SDL_GetModState() & ~(KMOD_MODE | KMOD_NUM | KMOD_RESERVED)) == 0)
                    {
                        enmHKeyState = HKEYSTATE_NORMAL;
                    }
                    processKey(&ev->key);
                    break;
                }
            } /* state switch */
            break;
        }

        /*
         * The window was closed.
         */
        case SDL_QUIT:
        {
            return CONEVENT_QUIT;
        }

        /*
         * The mouse has moved
         */
        case SDL_MOUSEMOTION:
        {
            if (fInputGrab || gMouse->getAbsoluteCoordinates())
            {
                mouseSendEvent(0);
            }
            break;
        }

        /*
         * A mouse button has been clicked or released.
         */
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        {
            SDL_MouseButtonEvent *bev = &ev->button;
            if (!fInputGrab && !gMouse->getAbsoluteCoordinates())
            {
                if (ev->type == SDL_MOUSEBUTTONDOWN && (bev->state & SDL_BUTTON_LMASK))
                {
                    /* start grabbing all events */
                    inputGrabStart();
                }
            }
            else
            {
                int dz = 0;
                if (bev->button == SDL_BUTTON_WHEELUP)
                {
                    dz = -1;
                }
                else if (bev->button == SDL_BUTTON_WHEELDOWN)
                {
                    dz = 1;
                }
                mouseSendEvent(dz);
            }
            break;
        }

        /*
         * The window has gained or lost focus.
         */
        case SDL_ACTIVEEVENT:
        {
            if (fInputGrab && (SDL_GetAppState() & SDL_ACTIVEEVENTMASK) == 0)
            {
                inputGrabEnd();
            }
            break;
        }


        /*
         * User specific update event.
         */
        /** @todo use a common user event handler so that SDL_PeepEvents() won't
         * possibly remove other events in the queue!
         */
        case SDL_USER_EVENT_UPDATERECT:
        {

            /*
             * Decode event parameters.
             */
            #define DECODEX(ev) ((intptr_t)(ev)->user.data1 >> 16)
            #define DECODEY(ev) ((intptr_t)(ev)->user.data1 & 0xFFFF)
            #define DECODEW(ev) ((intptr_t)(ev)->user.data2 >> 16)
            #define DECODEH(ev) ((intptr_t)(ev)->user.data2 & 0xFFFF)
            int x = DECODEX(ev);
            int y = DECODEY(ev);
            int w = DECODEW(ev);
            int h = DECODEH(ev);
            LogFlow(("SDL_USER_EVENT_UPDATERECT: x = %d, y = %d, w = %d, h = %d\n",
                    x, y, w, h));

            Assert(gFramebuffer);
            /*
             * Lock the framebuffer, perform the update and lock again
             */
            gFramebuffer->Lock();
            gFramebuffer->update(x, y, w, h);
            gFramebuffer->Unlock();

            #undef DECODEX
            #undef DECODEY
            #undef DECODEW
            #undef DECODEH
            break;
        }

        /*
         * User specific resize event.
         */
        case SDL_USER_EVENT_RESIZE:
            return CONEVENT_USR_SCREENRESIZE;

        /*
         * User specific update title bar notification event
         */
        case SDL_USER_EVENT_UPDATE_TITLEBAR:
            return CONEVENT_USR_TITLEBARUPDATE;

        /*
         * User specific termination event
         */
        case SDL_USER_EVENT_TERMINATE:
        {
            if (ev->user.code != VBOXSDL_TERM_NORMAL)
                RTPrintf("Error: VM terminated abnormally!\n");
            return CONEVENT_USR_QUIT;
        }

#ifdef VBOX_SECURELABEL
        /*
         * User specific secure label update event
         */
        case SDL_USER_EVENT_SECURELABEL_UPDATE:
            return CONEVENT_USR_SECURELABELUPDATE;

#endif /* VBOX_SECURELABEL */

        /*
         * User specific pointer shape change event
         */
        case SDL_USER_EVENT_POINTER_CHANGE:
        {
            PointerShapeChangeData *data =
                (PointerShapeChangeData *) ev->user.data1;
            setPointerShape (data);
            delete data;
            break;
        }

        default:
        {
	  printf("%s:%d unknown SDL event %d\n",__FILE__,__LINE__,ev->type);
            LogBird(("unknown SDL event %d\n", ev->type));
            break;
        }
    }
    return CONEVENT_NONE;
}

/**
 * Push the exit event forcing the main event loop to terminate.
 */
void SDLConsole::eventQuit()
{
    SDL_Event event;

    event.type = SDL_USEREVENT;
    event.user.type  = SDL_USER_EVENT_TERMINATE;
    SDL_PushEvent(&event);
}

/**
 * Converts an SDL keyboard eventcode to a XT scancode.
 *
 * @returns XT scancode
 * @param   ev SDL scancode
 */
uint8_t SDLConsole::keyEventToKeyCode(const SDL_KeyboardEvent *ev)
{
    int keycode;

    // start with the scancode determined by SDL
    keycode = ev->keysym.scancode;

#ifdef VBOXBFE_WITH_X11 /// @todo verify that these are X11 issues and not unique linux issues.
    // workaround for SDL keyboard translation issues on X11
    static const uint8_t x_keycode_to_pc_keycode[61] =
    {
       0xc7,      /*  97  Home   */
       0xc8,      /*  98  Up     */
       0xc9,      /*  99  PgUp   */
       0xcb,      /* 100  Left   */
       0x4c,      /* 101  KP-5   */
       0xcd,      /* 102  Right  */
       0xcf,      /* 103  End    */
       0xd0,      /* 104  Down   */
       0xd1,      /* 105  PgDn   */
       0xd2,      /* 106  Ins    */
       0xd3,      /* 107  Del    */
       0x9c,      /* 108  Enter  */
       0x9d,      /* 109  Ctrl-R */
       0x0,       /* 110  Pause  */
       0xb7,      /* 111  Print  */
       0xb5,      /* 112  Divide */
       0xb8,      /* 113  Alt-R  */
       0xc6,      /* 114  Break  */
       0x0,       /* 115 */
       0x0,       /* 116 */
       0x0,       /* 117 */
       0x0,       /* 118 */
       0x0,       /* 119 */
       0x70,      /* 120 Hiragana_Katakana */
       0x0,       /* 121 */
       0x0,       /* 122 */
       0x73,      /* 123 backslash */
       0x0,       /* 124 */
       0x0,       /* 125 */
       0x0,       /* 126 */
       0x0,       /* 127 */
       0x0,       /* 128 */
       0x79,      /* 129 Henkan */
       0x0,       /* 130 */
       0x7b,      /* 131 Muhenkan */
       0x0,       /* 132 */
       0x7d,      /* 133 Yen */
       0x0,       /* 134 */
       0x0,       /* 135 */
       0x47,      /* 136 KP_7 */
       0x48,      /* 137 KP_8 */
       0x49,      /* 138 KP_9 */
       0x4b,      /* 139 KP_4 */
       0x4c,      /* 140 KP_5 */
       0x4d,      /* 141 KP_6 */
       0x4f,      /* 142 KP_1 */
       0x50,      /* 143 KP_2 */
       0x51,      /* 144 KP_3 */
       0x52,      /* 145 KP_0 */
       0x53,      /* 146 KP_. */
       0x47,      /* 147 KP_HOME */
       0x48,      /* 148 KP_UP */
       0x49,      /* 149 KP_PgUp */
       0x4b,      /* 150 KP_Left */
       0x4c,      /* 151 KP_ */
       0x4d,      /* 152 KP_Right */
       0x4f,      /* 153 KP_End */
       0x50,      /* 154 KP_Down */
       0x51,      /* 155 KP_PgDn */
       0x52,      /* 156 KP_Ins */
       0x53,      /* 157 KP_Del */
    };

    if (keycode < 9)
    {
        keycode = 0;
    }
    else if (keycode < 97)
    {
        // just an offset
        keycode -= 8;
    }
    else if (keycode < 158)
    {
        // apply conversion table
        keycode = x_keycode_to_pc_keycode[keycode - 97];
    }
    else
    {
        keycode = 0;
    }

#elif defined(__DARWIN__)
    /*
     * The keycode on darwin is more or less the same as the SDL key symbol. 
     * This means we'll have to assume a keyboard layout and translate
     * the SDL / Quartz keycodes via it. 
     *
     * At first I'll just do a hardcoded us internaltional keyboard mapping 
     * here to try this out.
     */
/* from SDL:  
    key.scancode = [ event keyCode ];
    key.sym      = keymap [ key.scancode ];
    key.unicode  = [ chars characterAtIndex:0 ];
    key.mod      = KMOD_NONE; */
    const SDLKey sym = ev->keysym.sym;
    if (sym != SDLK_UNKNOWN)
    {
        Log(("SDL key event: sym=%d scancode=%#x unicode=%#x\n",
             sym, ev->keysym.scancode, ev->keysym.unicode));
        switch (sym)
        {                               /* set 1 scan code */
            case SDLK_ESCAPE:           return 0x01;
            case SDLK_EXCLAIM:          
            case SDLK_1:                return 0x02;
            case SDLK_AT:
            case SDLK_2:                return 0x03;
            case SDLK_HASH:
            case SDLK_3:                return 0x04;
            case SDLK_DOLLAR:
            case SDLK_4:                return 0x05;
            /* % */
            case SDLK_5:                return 0x06;
            case SDLK_CARET:
            case SDLK_6:                return 0x07;
            case SDLK_AMPERSAND:
            case SDLK_7:                return 0x08;
            case SDLK_ASTERISK:
            case SDLK_8:                return 0x09;
            case SDLK_LEFTPAREN:
            case SDLK_9:                return 0x0a;
            case SDLK_RIGHTPAREN:
            case SDLK_0:                return 0x0b;
            case SDLK_UNDERSCORE:
            case SDLK_MINUS:            return 0x0c;
            case SDLK_EQUALS:
            case SDLK_PLUS:             return 0x0d;
            case SDLK_BACKSPACE:        return 0x0e;
            case SDLK_TAB:              return 0x0f;
            case SDLK_q:                return 0x10;
            case SDLK_w:                return 0x11;
            case SDLK_e:                return 0x12;
            case SDLK_r:                return 0x13;
            case SDLK_t:                return 0x14;
            case SDLK_y:                return 0x15;
            case SDLK_u:                return 0x16;
            case SDLK_i:                return 0x17;
            case SDLK_o:                return 0x18;
            case SDLK_p:                return 0x19;
            case SDLK_LEFTBRACKET:      return 0x1a;
            case SDLK_RIGHTBRACKET:     return 0x1b;
            case SDLK_RETURN:           return 0x1c;
            case SDLK_KP_ENTER:         return 0x1c | 0x80;
            case SDLK_LCTRL:            return 0x1d;
            case SDLK_RCTRL:            return 0x1d | 0x80;
            case SDLK_a:                return 0x1e;
            case SDLK_s:                return 0x1f;
            case SDLK_d:                return 0x20;
            case SDLK_f:                return 0x21;
            case SDLK_g:                return 0x22;
            case SDLK_h:                return 0x23;
            case SDLK_j:                return 0x24;
            case SDLK_k:                return 0x25;
            case SDLK_l:                return 0x26;
            case SDLK_COLON:            
            case SDLK_SEMICOLON:        return 0x27;
            case SDLK_QUOTEDBL:
            case SDLK_QUOTE:            return 0x28;
            case SDLK_BACKQUOTE:        return 0x29;
            case SDLK_LSHIFT:           return 0x2a;
            case SDLK_BACKSLASH:        return 0x2b;
            case SDLK_z:                return 0x2c;
            case SDLK_x:                return 0x2d;
            case SDLK_c:                return 0x2e;
            case SDLK_v:                return 0x2f;
            case SDLK_b:                return 0x30;
            case SDLK_n:                return 0x31;
            case SDLK_m:                return 0x32;
            case SDLK_LESS:
            case SDLK_COMMA:            return 0x33;
            case SDLK_GREATER:
            case SDLK_PERIOD:           return 0x34;
            case SDLK_KP_DIVIDE:        /*??*/
            case SDLK_QUESTION:
            case SDLK_SLASH:            return 0x35;
            case SDLK_RSHIFT:           return 0x36;
            case SDLK_KP_MULTIPLY:
            case SDLK_PRINT:            return 0x37; /* fixme */
            case SDLK_LALT:             return 0x38;
            case SDLK_MODE: /* alt gr*/
            case SDLK_RALT:             return 0x38 | 0x80;
            case SDLK_SPACE:            return 0x39;
            case SDLK_CAPSLOCK:         return 0x3a;
            case SDLK_F1:               return 0x3b;
            case SDLK_F2:               return 0x3c;
            case SDLK_F3:               return 0x3d;
            case SDLK_F4:               return 0x3e;
            case SDLK_F5:               return 0x3f;
            case SDLK_F6:               return 0x40;
            case SDLK_F7:               return 0x41;
            case SDLK_F8:               return 0x42;
            case SDLK_F9:               return 0x43;
            case SDLK_F10:              return 0x44;
            case SDLK_PAUSE:            return 0x45; /* fixme */
            case SDLK_NUMLOCK:          return 0x45;
            case SDLK_SCROLLOCK:        return 0x46;
            case SDLK_KP7:              return 0x47;
            case SDLK_HOME:             return 0x47 | 0x80;
            case SDLK_KP8:              return 0x48;
            case SDLK_UP:               return 0x48 | 0x80;
            case SDLK_KP9:              return 0x49;
            case SDLK_PAGEUP:           return 0x49 | 0x80;
            case SDLK_KP_MINUS:         return 0x4a;
            case SDLK_KP4:              return 0x4b;
            case SDLK_LEFT:             return 0x4b | 0x80;
            case SDLK_KP5:              return 0x4c;
            case SDLK_KP6:              return 0x4d;
            case SDLK_RIGHT:            return 0x4d | 0x80;
            case SDLK_KP_PLUS:          return 0x4e;
            case SDLK_KP1:              return 0x4f; 
            case SDLK_END:              return 0x4f | 0x80; 
            case SDLK_KP2:              return 0x50; 
            case SDLK_DOWN:             return 0x50 | 0x80; 
            case SDLK_KP3:              return 0x51; 
            case SDLK_PAGEDOWN:         return 0x51 | 0x80; 
            case SDLK_KP0:              return 0x52; 
            case SDLK_INSERT:           return 0x52 | 0x80; 
            case SDLK_KP_PERIOD:        return 0x53;
            case SDLK_DELETE:           return 0x53 | 0x80; 
            case SDLK_SYSREQ:           return 0x54;
            case SDLK_F11:              return 0x56;
            case SDLK_F12:              return 0x57;
            case SDLK_F13:              return 0x5b;
            case SDLK_LSUPER:           return 0x5b | 0x80;
            case SDLK_F14:              return 0x5c;
            case SDLK_RSUPER:           return 0x5c | 0x80;
            case SDLK_F15:              return 0x5d;
            case SDLK_MENU:             return 0x5d | 0x80;
#if 0 /* @todo */
            case SDLK_CLEAR:            return 0x;
            case SDLK_KP_EQUALS:        return 0x;
            case SDLK_RMETA:            return 0x;
            case SDLK_LMETA:            return 0x;
            case SDLK_COMPOSE:	        return 0x;
            case SDLK_HELP:             return 0x;
            case SDLK_BREAK:            return 0x;
            case SDLK_POWER:	        return 0x;
            case SDLK_EURO:		        return 0x;
            case SDLK_UNDO:		        return 0x;
#endif 
            default:
                Log(("Unhandled sdl key event: sym=%d scancode=%#x unicode=%#x\n", 
                     ev->keysym.sym, ev->keysym.scancode, ev->keysym.unicode));
                keycode = 0;
                break;
        }
    }
    else 
    {
        /* deal with this as needed. mac can emit pure unicode events */
        Log(("Unhandled key event: scancode=%#x unicode=%#x\n", 
             ev->keysym.scancode, ev->keysym.unicode));
    }
#endif
    return keycode;
}

/**
 * Releases any modifier keys that are currently in pressed state.
 */
void SDLConsole::resetKeys(void)
{
    int i;
    for(i = 0; i < 256; i++)
    {
        if (gaModifiersState[i])
        {
            if (i & 0x80)
                gKeyboard->PutScancode(0xe0);
            gKeyboard->PutScancode(i | 0x80);
            gaModifiersState[i] = 0;
        }
    }
}

/**
 * Keyboard event handler.
 *
 * @param ev SDL keyboard event.
 */
void SDLConsole::processKey(SDL_KeyboardEvent *ev)
{
    int keycode, v;

#if defined(VBOXSDL_ADVANCED_OPTIONS) && defined(DEBUG) && 0
#error
    // first handle the debugger hotkeys
    uint8_t *keystate = SDL_GetKeyState(NULL);
    if ((keystate[SDLK_LALT]) && (ev->type == SDL_KEYDOWN))
    {
        switch (ev->keysym.sym)
        {
            // pressing Alt-F12 toggles the supervisor recompiler
            case SDLK_F12:
            {
                if (gMachineDebugger)
                {
                    BOOL recompileSupervisor;
                    gMachineDebugger->COMGETTER(RecompileSupervisor)(&recompileSupervisor);
                    gMachineDebugger->COMSETTER(RecompileSupervisor)(!recompileSupervisor);
                }
                break;
            }
            // pressing Alt-F11 toggles the user recompiler
            case SDLK_F11:
            {
                if (gMachineDebugger)
                {
                    BOOL recompileUser;
                    gMachineDebugger->COMGETTER(RecompileUser)(&recompileUser);
                    gMachineDebugger->COMSETTER(RecompileUser)(!recompileUser);
                }
                break;
            }
            // pressing Alt-F10 toggles the patch manager
            case SDLK_F10:
            {
                if (gMachineDebugger)
                {
                    BOOL patmEnabled;
                    gMachineDebugger->COMGETTER(PATMEnabled)(&patmEnabled);
                    gMachineDebugger->COMSETTER(PATMEnabled)(!patmEnabled);
                }
                break;
            }
            // pressing Alt-F9 toggles CSAM
            case SDLK_F9:
            {
                if (gMachineDebugger)
                {
                    BOOL csamEnabled;
                    gMachineDebugger->COMGETTER(CSAMEnabled)(&csamEnabled);
                    gMachineDebugger->COMSETTER(CSAMEnabled)(!csamEnabled);
                }
                break;
            }
            // pressing Alt-F8 toggles singlestepping mode
            case SDLK_F8:
            {
                if (gMachineDebugger)
                {
                    BOOL singlestepEnabled;
                    gMachineDebugger->COMGETTER(Singlestep)(&singlestepEnabled);
                    gMachineDebugger->COMSETTER(Singlestep)(!singlestepEnabled);
                }
                break;
            }

            default:
                break;
        }
    }
    // pressing Ctrl-F12 toggles the logger
    else if ((keystate[SDLK_RCTRL] || keystate[SDLK_LCTRL]) &&
             (ev->keysym.sym == SDLK_F12) && (ev->type == SDL_KEYDOWN))
    {
        PRTLOGGER pLogger = RTLogDefaultInstance();
        bool fEnabled = (pLogger && !(pLogger->fFlags & RTLOGFLAGS_DISABLED));
        if (fEnabled)
        {
            RTLogFlags(pLogger, "disabled");
        }
        else
        {
            RTLogFlags(pLogger, "nodisabled");
        }
    }
    // pressing F12 sets a logmark
    else if ((ev->keysym.sym == SDLK_F12) && (ev->type == SDL_KEYDOWN))
    {
        RTLogPrintf("****** LOGGING MARK ******\n");
        RTLogFlush(NULL);
    }
    // now update the titlebar flags
    updateTitlebar();
#endif

    // the pause key is the weirdest, needs special handling
    if (ev->keysym.sym == SDLK_PAUSE)
    {
        v = 0;
        if (ev->type == SDL_KEYUP)
            v |= 0x80;
        gKeyboard->PutScancode(0xe1);
        gKeyboard->PutScancode(0x1d | v);
        gKeyboard->PutScancode(0x45 | v);
        return;
    }

    /*
     * Perform SDL key event to scancode conversion
     */
    keycode = keyEventToKeyCode(ev);

    switch(keycode)
    {
        case 0x00:
        {
            /* sent when leaving window: reset the modifiers state */
            resetKeys();
            return;
        }

        case 0x2a:  /* Left Shift */
        case 0x36:  /* Right Shift */
        case 0x1d:  /* Left CTRL */
        case 0x9d:  /* Right CTRL */
        case 0x38:  /* Left ALT */
        case 0xb8:  /* Right ALT */
        {
            if (ev->type == SDL_KEYUP)
                gaModifiersState[keycode] = 0;
            else
                gaModifiersState[keycode] = 1;
            break;
        }

        case 0x45: /* num lock */
        case 0x3a: /* caps lock */
        {
            /* SDL does not send the key up event, so we generate it */
            gKeyboard->PutScancode(keycode);
            gKeyboard->PutScancode(keycode | 0x80);
            return;
        }
    }

    /*
     * Now we send the event. Apply extended and release prefixes.
     */
    if (keycode & 0x80)
        gKeyboard->PutScancode(0xe0);
    if (ev->type == SDL_KEYUP)
        gKeyboard->PutScancode(keycode | 0x80);
    else
        gKeyboard->PutScancode(keycode & 0x7f);
}

/**
 * Start grabbing the mouse.
 */
void SDLConsole::inputGrabStart()
{
    if (!gMouse->getNeedsHostCursor())
        SDL_ShowCursor(SDL_DISABLE);
    SDL_WM_GrabInput(SDL_GRAB_ON);
    // dummy read to avoid moving the mouse
    SDL_GetRelativeMouseState(NULL, NULL);
    fInputGrab = 1;
    updateTitlebar();
}

/**
 * End mouse grabbing.
 */
void SDLConsole::inputGrabEnd()
{
    SDL_WM_GrabInput(SDL_GRAB_OFF);
    if (!gMouse->getNeedsHostCursor())
        SDL_ShowCursor(SDL_ENABLE);
    fInputGrab = 0;
    updateTitlebar();
}

/**
 * Query mouse position and button state from SDL and send to the VM
 *
 * @param dz  Relative mouse wheel movement
 */

extern int GetRelativeMouseState(int *, int*);
extern int GetMouseState(int *, int*);

void SDLConsole::mouseSendEvent(int dz)
{
    int x, y, state, buttons;
    bool abs;

    abs = (gMouse->getAbsoluteCoordinates() && !fInputGrab) || gMouse->getNeedsHostCursor();

    state = abs ? SDL_GetMouseState(&x, &y) : SDL_GetRelativeMouseState(&x, &y);

    // process buttons
    buttons = 0;
    if (state & SDL_BUTTON(SDL_BUTTON_LEFT))
        buttons |= PDMIMOUSEPORT_BUTTON_LEFT;
    if (state & SDL_BUTTON(SDL_BUTTON_RIGHT))
        buttons |= PDMIMOUSEPORT_BUTTON_RIGHT;
    if (state & SDL_BUTTON(SDL_BUTTON_MIDDLE))
        buttons |= PDMIMOUSEPORT_BUTTON_MIDDLE;

    // now send the mouse event
    if (abs)
    {
        /**
         * @todo
         * PutMouseEventAbsolute() expects x and y starting from 1,1.
         * should we do the increment internally in PutMouseEventAbsolute()
         * or state it in PutMouseEventAbsolute() docs?
         */
        /* only send if outside the extra offset area */
        if (y >= gFramebuffer->getYOffset())
            gMouse->PutMouseEventAbsolute(x + 1, y + 1 - gFramebuffer->getYOffset(), dz, buttons);
    }
    else
    {
        gMouse->PutMouseEvent(x, y, dz, buttons);
    }
}

/**
 * Update the pointer shape or visibility.
 *
 * This is called when the mouse pointer shape changes or pointer is
 * hidden/displaying.  The new shape is passed as a caller allocated
 * buffer that will be freed after returning.
 *
 * @param   fVisible            Whether the pointer is visible or not.
 * @param   fAlpha              Alpha channel information is present.
 * @param   xHot                Horizontal coordinate of the pointer hot spot.
 * @param   yHot                Vertical coordinate of the pointer hot spot.
 * @param   width               Pointer width in pixels.
 * @param   height              Pointer height in pixels.
 * @param   pShape              The shape buffer. If NULL, then only
 *                              pointer visibility is being changed
 */
void SDLConsole::onMousePointerShapeChange(bool fVisible,
                                           bool fAlpha, uint32_t xHot,
                                           uint32_t yHot, uint32_t width,
                                           uint32_t height, void *pShape)
{
    PointerShapeChangeData *data;
    data = new PointerShapeChangeData (fVisible, fAlpha, xHot, yHot,
                                       width, height, (const uint8_t *) pShape);
    Assert (data);
    if (!data)
        return;

    SDL_Event event = {0};
    event.type = SDL_USEREVENT;
    event.user.type = SDL_USER_EVENT_POINTER_CHANGE;
    event.user.data1 = data;

    int rc = SDL_PushEvent (&event);
    AssertMsg (!rc, ("Error: SDL_PushEvent was not successful!\n"));
    if (rc)
        delete data;
}

/**
 * Build the titlebar string
 */
void SDLConsole::updateTitlebar()
{
    char title[1024];

    strcpy(title, "InnoTek VirtualBox");

    if (machineState == VMSTATE_SUSPENDED)
        strcat(title, " - [Paused]");

    if (fInputGrab)
        strcat(title, " - [Input captured]");

#if defined(VBOXSDL_ADVANCED_OPTIONS) && defined(DEBUG)
    // do we have a debugger interface
    if (gMachineDebugger)
    {
        // query the machine state
        BOOL recompileSupervisor = FALSE;
        BOOL recompileUser = FALSE;
        BOOL patmEnabled = FALSE;
        BOOL csamEnabled = FALSE;
        BOOL singlestepEnabled = FALSE;
        gMachineDebugger->COMGETTER(RecompileSupervisor)(&recompileSupervisor);
        gMachineDebugger->COMGETTER(RecompileUser)(&recompileUser);
        gMachineDebugger->COMGETTER(PATMEnabled)(&patmEnabled);
        gMachineDebugger->COMGETTER(CSAMEnabled)(&csamEnabled);
        gMachineDebugger->COMGETTER(Singlestep)(&singlestepEnabled);
        PRTLOGGER pLogger = RTLogDefaultInstance();
        bool fEnabled = (pLogger && !(pLogger->fFlags & RTLOGFLAGS_DISABLED));
        RTStrPrintf(title + strlen(title), sizeof(title) - strlen(title),
                    " [STEP=%d CS=%d PAT=%d RR0=%d RR3=%d LOG=%d]",
                    singlestepEnabled == TRUE, csamEnabled == TRUE, patmEnabled == TRUE,
                    recompileSupervisor == FALSE, recompileUser == FALSE, fEnabled == TRUE);
    }
#endif /* DEBUG */

    SDL_WM_SetCaption(title, "InnoTek VirtualBox");
}

/**
 * Updates the title bar while saving the state.
 * @param   iPercent    Percentage.
 */
void SDLConsole::updateTitlebarSave(int iPercent)
{
    char szTitle[256];
    AssertMsg(iPercent >= 0 && iPercent <= 100, ("%d\n", iPercent));
    RTStrPrintf(szTitle, sizeof(szTitle), "InnoTek VirtualBox - Saving %d%%...", iPercent);
    SDL_WM_SetCaption(szTitle, "InnoTek VirtualBox");
}

/**
 *  Sets the pointer shape according to parameters.
 *  Must be called only from the main SDL thread.
 */
void SDLConsole::setPointerShape (const PointerShapeChangeData *data)
{
    /*
     * don't do anything if there are no guest additions loaded (anymore)
     */
    if (!gMouse->getAbsoluteCoordinates())
        return;

    if (data->shape)
    {
        bool ok = false;

        uint32_t andMaskSize = (data->width + 7) / 8 * data->height;
        uint32_t srcShapePtrScan = data->width * 4;

        const uint8_t *srcAndMaskPtr = data->shape;
        const uint8_t *srcShapePtr = data->shape + ((andMaskSize + 3) & ~3);

#if defined (__WIN__)

        BITMAPV5HEADER bi;
        HBITMAP hBitmap;
        void *lpBits;
        HCURSOR hAlphaCursor = NULL;

        ::ZeroMemory (&bi, sizeof (BITMAPV5HEADER));
        bi.bV5Size = sizeof (BITMAPV5HEADER);
        bi.bV5Width = data->width;
        bi.bV5Height = - (LONG) data->height;
        bi.bV5Planes = 1;
        bi.bV5BitCount = 32;
        bi.bV5Compression = BI_BITFIELDS;
        // specifiy a supported 32 BPP alpha format for Windows XP
        bi.bV5RedMask   = 0x00FF0000;
        bi.bV5GreenMask = 0x0000FF00;
        bi.bV5BlueMask  = 0x000000FF;
        if (data->alpha)
            bi.bV5AlphaMask = 0xFF000000;
        else
            bi.bV5AlphaMask = 0;

        HDC hdc = ::GetDC (NULL);

        // create the DIB section with an alpha channel
        hBitmap = ::CreateDIBSection (hdc, (BITMAPINFO *) &bi, DIB_RGB_COLORS,
                                      (void **) &lpBits, NULL, (DWORD) 0);

        ::ReleaseDC (NULL, hdc);

        HBITMAP hMonoBitmap = NULL;
        if (data->alpha)
        {
            // create an empty mask bitmap
            hMonoBitmap = ::CreateBitmap (data->width, data->height, 1, 1, NULL);
        }
        else
        {
            // for now, we assert if width is not multiple of 16. the
            // alternative is to manually align the AND mask to 16 bits.
            AssertMsg (!(data->width % 16), ("AND mask must be word-aligned!\n"));

            // create the AND mask bitmap
            hMonoBitmap = ::CreateBitmap (data->width, data->height, 1, 1,
                                          srcAndMaskPtr);
        }

        Assert (hBitmap);
        Assert (hMonoBitmap);
        if (hBitmap && hMonoBitmap)
        {
            DWORD *dstShapePtr = (DWORD *) lpBits;

            for (uint32_t y = 0; y < data->height; y ++)
            {
                memcpy (dstShapePtr, srcShapePtr, srcShapePtrScan);
                srcShapePtr += srcShapePtrScan;
                dstShapePtr += data->width;
            }

            ICONINFO ii;
            ii.fIcon = FALSE;
            ii.xHotspot = data->xHot;
            ii.yHotspot = data->yHot;
            ii.hbmMask = hMonoBitmap;
            ii.hbmColor = hBitmap;

            hAlphaCursor = ::CreateIconIndirect (&ii);
            Assert (hAlphaCursor);
            if (hAlphaCursor)
            {
                // here we do a dirty trick by substituting a Window Manager's
                // cursor handle with the handle we created

                WMcursor *old_wm_cursor = gpCustomCursor->wm_cursor;

                // see SDL12/src/video/wincommon/SDL_sysmouse.c
                void *wm_cursor = malloc (sizeof (HCURSOR) + sizeof (uint8_t *) * 2);
                *(HCURSOR *) wm_cursor = hAlphaCursor;

                gpCustomCursor->wm_cursor = (WMcursor *) wm_cursor;
                SDL_SetCursor (gpCustomCursor);
                SDL_ShowCursor (SDL_ENABLE);

                if (old_wm_cursor)
                {
                    ::DestroyCursor (* (HCURSOR *) old_wm_cursor);
                    free (old_wm_cursor);
                }

                ok = true;
            }
        }

        if (hMonoBitmap)
            ::DeleteObject (hMonoBitmap);
        if (hBitmap)
            ::DeleteObject (hBitmap);

#elif defined(VBOXBFE_WITH_X11)

        XcursorImage *img = XcursorImageCreate (data->width, data->height);
        Assert (img);
        if (img)
        {
            img->xhot = data->xHot;
            img->yhot = data->yHot;

            XcursorPixel *dstShapePtr = img->pixels;

            for (uint32_t y = 0; y < data->height; y ++)
            {
                memcpy (dstShapePtr, srcShapePtr, srcShapePtrScan);

                if (!data->alpha)
                {
                    // convert AND mask to the alpha channel
                    uint8_t byte = 0;
                    for (uint32_t x = 0; x < data->width; x ++)
                    {
                        if (!(x % 8))
                            byte = *(srcAndMaskPtr ++);
                        else
                            byte <<= 1;

                        if (byte & 0x80)
                        {
                            // X11 doesn't support inverted pixels (XOR ops,
                            // to be exact) in cursor shapes, so we detect such
                            // pixels and always replace them with black ones to
                            // make them visible at least over light colors
                            if (dstShapePtr [x] & 0x00FFFFFF)
                                dstShapePtr [x] = 0xFF000000;
                            else
                                dstShapePtr [x] = 0x00000000;
                        }
                        else
                            dstShapePtr [x] |= 0xFF000000;
                    }
                }

                srcShapePtr += srcShapePtrScan;
                dstShapePtr += data->width;
            }

            Cursor cur = XcursorImageLoadCursor (gSdlInfo.info.x11.display, img);
            Assert (cur);
            if (cur)
            {
                // here we do a dirty trick by substituting a Window Manager's
                // cursor handle with the handle we created

                WMcursor *old_wm_cursor = gpCustomCursor->wm_cursor;

                // see SDL12/src/video/x11/SDL_x11mouse.c
                void *wm_cursor = malloc (sizeof (Cursor));
                *(Cursor *) wm_cursor = cur;

                gpCustomCursor->wm_cursor = (WMcursor *) wm_cursor;
                SDL_SetCursor (gpCustomCursor);
                SDL_ShowCursor (SDL_ENABLE);

                if (old_wm_cursor)
                {
                    XFreeCursor (gSdlInfo.info.x11.display, *(Cursor *) old_wm_cursor);
                    free (old_wm_cursor);
                }

                ok = true;
            }

            XcursorImageDestroy (img);
        }

#endif /* VBOXBFE_WITH_X11 */

        if (!ok)
        {
            SDL_SetCursor (gpDefaultCursor);
            SDL_ShowCursor (SDL_ENABLE);
        }
    }
    else
    {
        if (data->visible)
        {
            SDL_ShowCursor (SDL_ENABLE);
        }
        else
        {
            SDL_ShowCursor (SDL_DISABLE);
        }
    }
}

void SDLConsole::resetCursor(void)
{
    SDL_SetCursor (gpDefaultCursor);
    SDL_ShowCursor (SDL_ENABLE);
}

/**
 * Handles a host key down event
 */
int SDLConsole::handleHostKey(const SDL_KeyboardEvent *pEv)
{
    /*
     * Revalidate the host key modifier
     */
    if ((SDL_GetModState() & ~(KMOD_MODE | KMOD_NUM | KMOD_RESERVED)) != gHostKey)
        return VERR_NOT_SUPPORTED;

    /*
     * What was pressed?
     */
    switch (pEv->keysym.sym)
    {
        /* Control-Alt-Delete */
        case SDLK_DELETE:
        {
            gKeyboard->PutCAD();
            break;
        }

        /*
         * Fullscreen / Windowed toggle.
         */
        case SDLK_f:
        {
            if (gfAllowFullscreenToggle)
            {
                gFramebuffer->setFullscreen(!gFramebuffer->getFullscreen());

                /*
                 * We have switched from/to fullscreen, so request a full
                 * screen repaint, just to be sure.
                 */
                gDisplay->InvalidateAndUpdate();
            }
            break;
        }

        /*
         * Pause / Resume toggle.
         */
        case SDLK_p:
        {
            if (machineState == VMSTATE_RUNNING)
            {
                if (fInputGrab)
                    inputGrabEnd();

                PVMREQ pReq;
                int rcVBox = VMR3ReqCall(pVM, &pReq, RT_INDEFINITE_WAIT,
                                         (PFNRT)VMR3Suspend, 1, pVM);
                AssertRC(rcVBox);
                if (VBOX_SUCCESS(rcVBox))
                {
                    rcVBox = pReq->iStatus;
                    VMR3ReqFree(pReq);
                }
            }
            else
            if (machineState == VMSTATE_SUSPENDED)
            {
                PVMREQ pReq;
                int rcVBox = VMR3ReqCall(pVM, &pReq, RT_INDEFINITE_WAIT,
                                         (PFNRT)VMR3Resume, 1, pVM);
                AssertRC(rcVBox);
                if (VBOX_SUCCESS(rcVBox))
                {
                    rcVBox = pReq->iStatus;
                    VMR3ReqFree(pReq);
                }
            }
            updateTitlebar();
            break;
        }

        /*
         * Reset the VM
         */
        case SDLK_r:
        {
            PVMREQ pReq;
            int rcVBox = VMR3ReqCall(pVM, &pReq, RT_INDEFINITE_WAIT,
                                     (PFNRT)VMR3Reset, 1, pVM);
            AssertRC(rcVBox);
            if (VBOX_SUCCESS(rcVBox))
            {
                rcVBox = pReq->iStatus;
                VMR3ReqFree(pReq);
            }
            break;
        }

        /*
         * Terminate the VM
         */
        case SDLK_q:
        {
            return VINF_EM_TERMINATE;
            break;
        }

#if 0
        /*
         * Save the machine's state and exit
         */
        case SDLK_s:
        {
            resetKeys();
            RTThreadYield();
            if (fInputGrab)
                inputGrabEnd();
            RTThreadYield();
            updateTitlebarSave(0);
            gProgress = NULL;
            int rc = gConsole->SaveState(gProgress.asOutParam());
            if (rc != S_OK)
            {
                RTPrintf("Error saving state! rc = 0x%x\n", rc);
                return VINF_EM_TERMINATE;
            }
            Assert(gProgress);

            /*
             * Wait for the operation to be completed and work
             * the title bar in the mean while.
             */
            LONG    cPercent = 0;
            for (;;)
            {
                BOOL    fCompleted;
                rc = gProgress->COMGETTER(Completed)(&fCompleted);
                if (FAILED(rc) || fCompleted)
                    break;
                LONG cPercentNow;
                rc = gProgress->COMGETTER(Percent)(&cPercentNow);
                if (FAILED(rc))
                    break;
                if (cPercentNow != cPercent)
                {
                    UpdateTitlebarSave(cPercent);
                    cPercent = cPercentNow;
                }

                /* wait */
                rc = gProgress->WaitForCompletion(100);
                if (FAILED(rc))
                    break;
                /// @todo process gui events.
            }

            /*
             * What's the result of the operation?
             */
            HRESULT lrc;
            rc = gProgress->COMGETTER(ResultCode)(&lrc);
            if (FAILED(rc))
                lrc = ~0;
            if (!lrc)
            {
                UpdateTitlebarSave(100);
                RTThreadYield();
                RTPrintf("Saved the state successfully.\n");
            }
            else
                RTPrintf("Error saving state, lrc=%d (%#x)\n", lrc, lrc);
            return VINF_EM_TERMINATE;
        }
#endif
        /*
         * Not a host key combination.
         * Indicate this by returning false.
         */
        default:
            return VERR_NOT_SUPPORTED;
    }

    return VINF_SUCCESS;
}


