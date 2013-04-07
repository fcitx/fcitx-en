/***************************************************************************
 *   Copyright (C) 2012~2012 by Tai-Lin Chu                                *
 *   tailinchu@gmail.com                                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.              *
 ***************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <fcitx/ime.h>
#include <fcitx-config/fcitx-config.h>
#include <fcitx-config/xdg.h>
#include <fcitx-config/hotkey.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/utils.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/instance.h>
#include <fcitx/context.h>
#include <fcitx/keys.h>
#include <fcitx/ui.h>
#include <libintl.h>

#include "config.h"
#include "eim.h"

#define MAX_WORD_LEN 64
#define CAND_WORD_NUM 10
#define SHORT_WORD_LEN 4
#define THRESH 0.4

FCITX_DEFINE_PLUGIN(fcitx_en, ime2, FcitxIMClass2) = {
    FcitxEnCreate,
    FcitxEnDestroy,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

CONFIG_DESC_DEFINE(GetFcitxEnConfigDesc, "fcitx-en.desc")
static INPUT_RETURN_VALUE FcitxEnGetCandWord(void *arg, FcitxCandidateWord * candWord);
static void FcitxEnReloadConfig(void *arg);
static boolean LoadEnConfig(FcitxEnConfig * fs);
static void SaveEnConfig(FcitxEnConfig * fs);
static void ConfigEn(FcitxEn * en);
static int Sift3(const char *s1, const char *s2, const int maxOffset);
static double Distance(const char *s1, const char *s2);

const FcitxHotkey FCITX_HYPHEN[2] =
{ {NULL, FcitxKey_minus, FcitxKeyState_None}, {NULL, FcitxKey_None,
    FcitxKeyState_None}
};

const FcitxHotkey FCITX_APOS[2] =
{ {NULL, FcitxKey_apostrophe, FcitxKeyState_None}, {NULL, FcitxKey_None,
    FcitxKeyState_None}
};

/**
 * @brief initialize the extra input method
 *
 * @param arg
 * @return successful or not
 **/
__EXPORT_API void *FcitxEnCreate(FcitxInstance * instance)
{
    if (GetFcitxEnConfigDesc() == NULL)
        return NULL;

    FcitxEn *en = (FcitxEn *) fcitx_utils_malloc0(sizeof(FcitxEn));
    FcitxGlobalConfig *config = FcitxInstanceGetGlobalConfig(instance);
    FcitxInputState *input = FcitxInstanceGetInputState(instance);
    FcitxCandidateWordSetChoose(FcitxInputStateGetCandidateList(input),
            DIGIT_STR_CHOOSE);

    bindtextdomain("fcitx-en", LOCALEDIR);

    //load dic
    FILE *file = fopen(EN_DIC_FILE, "r");
    if (file == NULL)
        return NULL;
    char line[MAX_WORD_LEN];
    node **tmp = &(en->dic);
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strlen(line) - 1] = '\0';	// remove newline
        *tmp = (node *) malloc(sizeof(node));
        (*tmp)->word = strdup(line);
        (*tmp)->next = NULL;
        tmp = &((*tmp)->next);
    }
    fclose(file);

    en->owner = instance;
    en->cur = 0;
    en->buf = strdup("");
    en->selectMode = false;
    FcitxCandidateWordSetPageSize(FcitxInputStateGetCandidateList(input), 10);
    LoadEnConfig(&en->config);
    ConfigEn(en);
    
    
    FcitxIMIFace iface;
    memset(&iface, 0, sizeof(FcitxIMIFace));
    iface.Init = FcitxEnInit;
    iface.ResetIM = FcitxEnReset;
    iface.DoInput = FcitxEnDoInput;
    iface.GetCandWords = FcitxEnGetCandWords;
    iface.Save = NULL;
    iface.ReloadConfig = FcitxEnReloadConfig;
    iface.OnClose = NULL;
    iface.KeyBlocker = NULL;

    FcitxInstanceRegisterIMv2(
        instance,
        en,
        "AutoEnglish",
        _("AutoEnglish"),
        "fcitx-en",
        iface,
        1,
        "en_US"
    );
    
    return en;
}

/**
 * @brief Process Key Input and return the status
 *
 * @param keycode keycode from XKeyEvent
 * @param state state from XKeyEvent
 * @param count count from XKeyEvent
 * @return INPUT_RETURN_VALUE
 **/
    __EXPORT_API INPUT_RETURN_VALUE
FcitxEnDoInput(void *arg, FcitxKeySym sym, unsigned int state)
{
    FcitxEn *en = (FcitxEn *) arg;
    FcitxInputState *input = FcitxInstanceGetInputState(en->owner);
    int buf_len = strlen(en->buf);
    FcitxCandidateWordList *candList = FcitxInputStateGetCandidateList(input);
    if (en->selectMode && 
        buf_len > 0 && FcitxCandidateWordGetListSize(candList) > 0 &&
        FcitxHotkeyIsHotKeyDigit(sym, state)
    ) {
        return IRV_TO_PROCESS;
    }
    
    if (FcitxHotkeyIsHotKeyLAZ(sym, state)
            || FcitxHotkeyIsHotKeyUAZ(sym, state)
            || FcitxHotkeyIsHotKeyDigit(sym, state)
            || FcitxHotkeyIsHotKey(sym, state, FCITX_HYPHEN)
            || FcitxHotkeyIsHotKey(sym, state, FCITX_APOS)) {
        char in = (char)sym & 0xff;
        char *half1 = strndup(en->buf, en->cur);
        char *half2 = strdup(en->buf + en->cur);
        en->buf = realloc(en->buf, buf_len + 2);
        sprintf(en->buf, "%s%c%s", half1, in, half2);
        en->cur++;
        free(half1);
        free(half2);
        
        en->selectMode = false;
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_TAB)) {
        en->selectMode = !en->selectMode;
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_LEFT)) {
        if (en->cur > 0)
            en->cur--;
        
        en->selectMode = false;
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_RIGHT)) {
        if (en->cur < buf_len)
            en->cur++;
        
        en->selectMode = false;
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_BACKSPACE)) {
        if (en->cur > 0) {
            char *half1 = strndup(en->buf, en->cur - 1);
            char *half2 = strdup(en->buf + en->cur);
            en->buf = realloc(en->buf, buf_len + 1);
            sprintf(en->buf, "%s%s", half1, half2);
            en->cur--;
            free(half1);
            free(half2);
        }
        en->selectMode = false;
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_DELETE)) {
        if (en->cur < buf_len) {
            char *half1 = strndup(en->buf, en->cur);
            char *half2 = strdup(en->buf + en->cur + 1);
            en->buf = realloc(en->buf, buf_len + 1);
            sprintf(en->buf, "%s%s", half1, half2);
            free(half1);
            free(half2);
        }
        en->selectMode = false;
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_ESCAPE)) {
        en->selectMode = false;
        return IRV_CLEAN;
    } else {
        if (buf_len == 0)
            return IRV_TO_PROCESS;
        // sym is symbol, or enter, so it is the end of word
        if (FcitxHotkeyIsHotKeySimple(sym, state)) {	// for enter key
            char in = (char)sym & 0xff;
            char *old = strdup(en->buf);
            en->buf = realloc(en->buf, buf_len + 2);
            sprintf(en->buf, "%s%c", old, in);
            free(old);
        }
        strcpy(FcitxInputStateGetOutputString(input), en->buf);
        en->selectMode = false;
        return IRV_COMMIT_STRING;
    }
    if (strlen(en->buf) == 0)
        return IRV_DONOT_PROCESS_CLEAN;
    else
        return IRV_DISPLAY_CANDWORDS;
}

__EXPORT_API boolean FcitxEnInit(void *arg)
{
    FcitxEn *en = (FcitxEn *) arg;
    FcitxInstanceSetContext(en->owner, CONTEXT_IM_KEYBOARD_LAYOUT, "us");
    FcitxInstanceSetContext(en->owner, CONTEXT_ALTERNATIVE_PREVPAGE_KEY,
            FCITX_LEFT);
    FcitxInstanceSetContext(en->owner, CONTEXT_ALTERNATIVE_NEXTPAGE_KEY,
            FCITX_RIGHT);
    return true;
}

__EXPORT_API void FcitxEnReset(void *arg)
{
    FcitxEn *en = (FcitxEn *) arg;
    en->cur = 0;
    free(en->buf);
    en->buf = strdup("");
}

static int compare(const void *a, const void *b)
{
    double diff = ((cword *) a)->dist - ((cword *) b)->dist;
    if (diff > 0)
        return 1;
    else if (diff < 0)
        return -1;
    else
        return 0;
}

/**
 * @brief function DoInput has done everything for us.
 *
 * @param searchMode
 * @return INPUT_RETURN_VALUE
 **/
__EXPORT_API INPUT_RETURN_VALUE FcitxEnGetCandWords(void *arg)
{
    FcitxEn *en = (FcitxEn *) arg;
    FcitxInputState *input = FcitxInstanceGetInputState(en->owner);
    FcitxMessages *msgPreedit = FcitxInputStateGetPreedit(input);
    FcitxMessages *clientPreedit = FcitxInputStateGetClientPreedit(input);
    FcitxGlobalConfig *config = FcitxInstanceGetGlobalConfig(en->owner);

    FcitxCandidateWordSetPageSize(FcitxInputStateGetCandidateList(input), 10);

    //clean up window asap
    FcitxInstanceCleanInputWindow(en->owner);

    ConfigEn(en);

    FcitxLog(DEBUG, "buf: %s", en->buf);
    if (en->selectMode) {

        node *tmp;
        cword *clist = (cword *) malloc(sizeof(cword) * CAND_WORD_NUM);
        int num = 0;
        for (tmp = en->dic; tmp != NULL; tmp = tmp->next) {
            double dist = Distance(en->buf, tmp->word);
            if (dist < THRESH) {
                char str[MAX_WORD_LEN];
                snprintf(str, MAX_WORD_LEN, "%s ", tmp->word);
                clist[num].word = strdup(str);
                if (isupper(en->buf[0]))
                    clist[num].word[0] = toupper(clist[num].word[0]);
                clist[num].dist = dist;
                num++;
                if (num == CAND_WORD_NUM)
                    break;
            }
        }
        
        qsort((void *)clist, num, sizeof(cword), compare);
        
        int i;
        for (i = 0; i < num; i++) {
            FcitxCandidateWord cw;
            cw.callback = FcitxEnGetCandWord;
            cw.owner = en;
            cw.priv = NULL;
            cw.strExtra = NULL;
            cw.strWord = clist[i].word; // free by fcitx
            cw.wordType = MSG_OTHER;
            FcitxCandidateWordAppend(FcitxInputStateGetCandidateList(input),
                    &cw);
        }
        free(clist);
    }

    // setup cursor
    FcitxInputStateSetShowCursor(input, true);
    FcitxInputStateSetCursorPos(input, en->cur);
    FcitxInputStateSetClientCursorPos(input, en->cur);

    FcitxMessagesAddMessageAtLast(msgPreedit, MSG_INPUT, "%s", en->buf);
    FcitxMessagesAddMessageAtLast(clientPreedit, MSG_OTHER, "%s", en->buf);

    return IRV_DISPLAY_CANDWORDS;
}

INPUT_RETURN_VALUE FcitxEnGetCandWord(void *arg, FcitxCandidateWord * candWord)
{
    FcitxEn *en = (FcitxEn *) candWord->owner;
    FcitxInputState *input = FcitxInstanceGetInputState(en->owner);
    FcitxLog(DEBUG, "selected candword: %s", candWord->strWord);
    strcpy(FcitxInputStateGetOutputString(input), candWord->strWord);
    return IRV_COMMIT_STRING;
}

/**
 * @brief Destroy the input method while unload it.
 *
 * @return int
 **/
__EXPORT_API void FcitxEnDestroy(void *arg)
{
    FcitxEn *en = (FcitxEn *) arg;
    free(en->buf);
    node *tmp = en->dic;
    while (tmp != NULL) {
        free(tmp->word);
        node *next = tmp->next;
        free(tmp);
        tmp = next;
    }
    free(arg);
}

static double Distance(const char * s1, const char * s2) {
    double ret = 1;
    char * short_str;
    char * long_str;
    if (strlen(s1) < strlen(s2)) {
        short_str = strdup(s1);
        long_str = strdup(s2);
    } else {
        short_str = strdup(s2);
        long_str = strdup(s1);
    }
    int short_len = strlen(short_str);
    int long_len = strlen(long_str);
    if (long_len - short_len >= SHORT_WORD_LEN) {
        ret = 1;
    } else if (short_len <= SHORT_WORD_LEN) {
        ret = strncasecmp(short_str, long_str, short_len) == 0? 0 : 1;
    } else {
        int i;
        for (i = long_len; i > short_len ; i--) {
            double dist = ((double) Sift3(short_str, long_str, 2) ) / short_len; // search around 2 chars
            if (dist < ret)
                ret = dist;
            long_str[i - 1] = '\0';
        }
    }
    free(long_str);
    free(short_str);
    return ret;
}

//
// sift 3 
//

static int Sift3(const char *s1, const char *s2, const int maxOffset)
{
    int len1 = strlen(s1);
    int len2 = strlen(s2);
    if (len1 == 0)
        return len2 == 0 ? 0 : len2;
    if (len2 == 0)
        return len1;
    int c = 0;
    int offset1 = 0;
    int offset2 = 0;
    int lcs = 0;
    while ((c + offset1 < len1)
            && (c + offset2 < len2)) {
        if (s1[c + offset1] == s2[c + offset2])
            lcs++;
        else {
            offset1 = 0;
            offset2 = 0;
            int i;
            for (i = 0; i < maxOffset; i++) {
                if ((c + i < len1)
                        && (s1[c + i] == s2[c])) {
                    offset1 = i;
                    break;
                }
                if ((c + i < len2)
                        && (s1[c] == s2[c + i])) {
                    offset2 = i;
                    break;
                }
            }
        }
        c++;
    }
    return (len1 + len2) / 2  - lcs;
}


static void FcitxEnReloadConfig(void *arg)
{
    FcitxEn *en = (FcitxEn *) arg;
    LoadEnConfig(&en->config);
    ConfigEn(en);
}

boolean LoadEnConfig(FcitxEnConfig * fs)
{
    // none at the moment
    return true;
}

void SaveEnConfig(FcitxEnConfig * fc)
{
    // none at the moment
}

void ConfigEn(FcitxEn * en)
{
    // none at the moment
}
