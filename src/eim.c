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

#include <hunspell/hunspell.h>

#include "config.h"
#include "eim.h"

FCITX_EXPORT_API
FcitxIMClass ime = {
    FcitxEnCreate,
    FcitxEnDestroy
};
FCITX_EXPORT_API
int ABI_VERSION = FCITX_ABI_VERSION;

CONFIG_DESC_DEFINE(GetFcitxEnConfigDesc, "fcitx-en.desc")
static INPUT_RETURN_VALUE FcitxEnGetCandWord(void* arg, FcitxCandidateWord* candWord);
static void FcitxEnReloadConfig(void* arg);
static boolean LoadEnConfig(FcitxEnConfig* fs);
static void SaveEnConfig(FcitxEnConfig* fs);
static void ConfigEn(FcitxEn* en);

/**
 * @brief initialize the extra input method
 *
 * @param arg
 * @return successful or not
 **/
__EXPORT_API
void* FcitxEnCreate(FcitxInstance* instance)
{
    if (GetFcitxEnConfigDesc() == NULL)
        return NULL;
    
    FcitxEn* en = (FcitxEn*) fcitx_utils_malloc0(sizeof(FcitxEn));
    FcitxGlobalConfig* config = FcitxInstanceGetGlobalConfig(instance);
    FcitxInputState *input = FcitxInstanceGetInputState(instance);
    FcitxCandidateWordSetChoose(FcitxInputStateGetCandidateList(input), DIGIT_STR_CHOOSE);
    
    bindtextdomain("fcitx-en", LOCALEDIR);

    en->context = Hunspell_create("/usr/share/hunspell/en_US.aff", "/usr/share/hunspell/en_US.dic");
    en->owner = instance;
    en->len = 0;
    en->cur = 0;
    en->buf = strdup("");
    FcitxCandidateWordSetPageSize(FcitxInputStateGetCandidateList(input), config->iMaxCandWord);
    LoadEnConfig(&en->config);
    ConfigEn(en);

    FcitxInstanceRegisterIM(
        instance,
        en,
        "en",
        _("En"),
        "en",
        FcitxEnInit,
        FcitxEnReset,
        FcitxEnDoInput,
        FcitxEnGetCandWords,
        NULL,
        NULL,
        FcitxEnReloadConfig,
        NULL,
        1,
        "zh_TW"
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
__EXPORT_API
INPUT_RETURN_VALUE FcitxEnDoInput(void* arg, FcitxKeySym sym, unsigned int state)
{
    FcitxEn* en = (FcitxEn*) arg;
    FcitxInputState *input = FcitxInstanceGetInputState(en->owner);
    // not alpha and buf len is zero
    if (! FcitxHotkeyIsHotKeyLAZ(sym, state) && strlen(en->buf) <= 0 )
		return IRV_TO_PROCESS;

    if (FcitxHotkeyIsHotKeyLAZ(sym, state)) {
		char in = (char) sym & 0xff;
		char * half1 = strndup(en->buf, en->cur);
		char * half2 = strdup(en->buf+en->cur);
		sprintf(en->buf, "%s%c%s", half1, in, half2);
		en->len++;
		en->cur++;
	} else if (FcitxHotkeyIsHotKeyDigit(sym, state)) {
		return FcitxCandidateWordChooseByIndex(FcitxInputStateGetCandidateList(input), 0);
    
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_BACKSPACE)) {
		if (en->cur>0) {
			char * half1 = strndup(en->buf, en->cur-1);
			char * half2 = strdup(en->buf+en->cur);
			sprintf(en->buf, "%s%s", half1, half2);
			en->len--;
			en->cur--;
		}
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_DELETE)) {
		if (en->cur < en->len) {
			char * half1 = strndup(en->buf, en->cur);
			char * half2 = strdup(en->buf+en->cur+1);
			sprintf(en->buf, "%s%s", half1, half2);
			en->len--;
		}
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_SPACE)) {
        strcpy(FcitxInputStateGetOutputString(input), en->buf);
        return IRV_COMMIT_STRING;
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_RIGHT)) {
        if (en->cur < en->len)
           en->cur++;
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_LEFT)) {
        if (en->cur > 0)
           en->cur--;
    } else {
        // to do: more en_handle
        return IRV_TO_PROCESS;
    }
    return IRV_DISPLAY_CANDWORDS;
}

__EXPORT_API
boolean FcitxEnInit(void* arg)
{
    FcitxEn* en = (FcitxEn*) arg;
    FcitxInstanceSetContext(en->owner, CONTEXT_IM_KEYBOARD_LAYOUT, "us");
    FcitxInstanceSetContext(en->owner, CONTEXT_ALTERNATIVE_PREVPAGE_KEY, FCITX_LEFT);
    FcitxInstanceSetContext(en->owner, CONTEXT_ALTERNATIVE_NEXTPAGE_KEY, FCITX_RIGHT);
    return true;
}

__EXPORT_API
void FcitxEnReset(void* arg)
{
    FcitxEn* en = (FcitxEn*) arg;
    en->buf = strdup("");
    en->len=en->cur=0;
    // todo
}


/**
 * @brief function DoInput has done everything for us.
 *
 * @param searchMode
 * @return INPUT_RETURN_VALUE
 **/
__EXPORT_API
INPUT_RETURN_VALUE FcitxEnGetCandWords(void* arg)
{
    FcitxEn* en = (FcitxEn*) arg;
    FcitxInputState *input = FcitxInstanceGetInputState(en->owner);
    FcitxMessages *msgPreedit = FcitxInputStateGetPreedit(input);
    FcitxMessages *clientPreedit = FcitxInputStateGetClientPreedit(input);
    FcitxGlobalConfig* config = FcitxInstanceGetGlobalConfig(en->owner);
    
    FcitxCandidateWordSetPageSize(FcitxInputStateGetCandidateList(input), config->iMaxCandWord);

    //clean up window asap
    FcitxInstanceCleanInputWindowUp(en->owner);

    ConfigEn(en);

    FcitxLog(DEBUG, "buf: %s", en->buf);

	int index = 0;
	char ** candList;
	int candNum = Hunspell_suggest(en->context, &candList, en->buf);
	while (index < candNum) {
		FcitxCandidateWord cw;
		cw.callback = FcitxEnGetCandWord;
		cw.owner = en;
		cw.priv = NULL;
		cw.strExtra = NULL;
		cw.strWord = strdup(candList[index]);
		cw.wordType = MSG_OTHER;
		FcitxCandidateWordAppend(FcitxInputStateGetCandidateList(input), &cw);
		index ++;
	}
	Hunspell_free_list(en->context, &candList, candNum);

    // setup cursor
    FcitxInputStateSetShowCursor(input, true);
    FcitxLog(DEBUG, "buf len: %d, cur: %d", en->len, en->cur);
    FcitxInputStateSetCursorPos(input, en->cur);
    FcitxInputStateSetClientCursorPos(input, en->cur);

    // insert zuin in the middle
    FcitxMessagesAddMessageAtLast(msgPreedit, MSG_INPUT, "%s", en->buf);
    FcitxMessagesAddMessageAtLast(clientPreedit, MSG_INPUT, "%s", en->buf);

    return IRV_DISPLAY_CANDWORDS;
}

INPUT_RETURN_VALUE FcitxEnGetCandWord(void* arg, FcitxCandidateWord* candWord)
{
    FcitxEn* en = (FcitxEn*) candWord->owner;
    FcitxInputState *input = FcitxInstanceGetInputState(en->owner);
	FcitxLog(DEBUG, "selected candword: %s", candWord->strWord);
	en->buf = strdup(candWord->strWord);
	en->len=en->cur=strlen(candWord->strWord);
    return IRV_DISPLAY_CANDWORDS;
}


/**
 * @brief Destroy the input method while unload it.
 *
 * @return int
 **/
__EXPORT_API
void FcitxEnDestroy(void* arg)
{
    FcitxEn* en = (FcitxEn*) arg;
    Hunspell_destroy(en->context);
    free(en->buf);
    free(arg);
}

void FcitxEnReloadConfig(void* arg) {
    FcitxEn* en = (FcitxEn*) arg;
    LoadEnConfig(&en->config);
    ConfigEn(en);
}

boolean LoadEnConfig(FcitxEnConfig* fs)
{
    FcitxConfigFileDesc *configDesc = GetFcitxEnConfigDesc();
    if (!configDesc)
        return false;

    FILE *fp = FcitxXDGGetFileUserWithPrefix("conf", "fcitx-en.config", "rt", NULL);

    if (!fp) {
        if (errno == ENOENT)
            SaveEnConfig(fs);
    }
    FcitxConfigFile *cfile = FcitxConfigParseConfigFileFp(fp, configDesc);

    FcitxEnConfigConfigBind(fs, cfile, configDesc);
    FcitxConfigBindSync(&fs->config);

    if (fp)
        fclose(fp);
    return true;
}

void SaveEnConfig(FcitxEnConfig* fc)
{
    FcitxConfigFileDesc *configDesc = GetFcitxEnConfigDesc();
    FILE *fp = FcitxXDGGetFileUserWithPrefix("conf", "fcitx-en.config", "wt", NULL);
    FcitxConfigSaveConfigFileFp(fp, &fc->config, configDesc);
    if (fp)
        fclose(fp);
}

void ConfigEn(FcitxEn* en)
{
    Hunhandle* ctx = en->context;
    // none at the moment
}
