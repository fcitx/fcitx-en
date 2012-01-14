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
static int en_prefix_suggest(const char * prefix, char *** cp);
static void en_free_list(char *** cp, const int n);
const FcitxHotkey FCITX_TAB[2] = {{NULL, FcitxKey_Tab, FcitxKeyState_None}, {NULL, FcitxKey_None, FcitxKeyState_None}};

int en_prefix_suggest(const char * prefix, char *** cp)
{
	FILE * file = fopen("/usr/local/share/fcitx/en_dic.txt", "r");
	if (file == NULL)
		return 0;
	char line [32];
	char ** candlist = malloc(0);
	int list_size = 0;
	int prefix_len = strlen(prefix);
	while (fgets (line, 32, file) != NULL)
	{
		if (strncmp (prefix, line, prefix_len) == 0) {
			line[strlen(line)-1] = '\0'; // remove newline
			char * word = strdup(line);
			list_size ++;
			candlist = realloc(candlist, list_size * sizeof (char *));
			candlist[list_size-1] = word;
			if(list_size == 10)
				break;
		}
	}
	*cp = candlist;
	return list_size;
}

static void en_free_list(char *** cp, const int n)
{
	char ** candlist = * cp;
	int i;
	for(i=0; i<n ; i++) {
		free(candlist[i]);
	}
	free(candlist);
}

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

    en->owner = instance;
    en->len = 0;
    en->cur = 0;
    en->chooseMode = 0;
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

    if (FcitxHotkeyIsHotKeyLAZ(sym, state) || (FcitxHotkeyIsHotKeyDigit(sym, state) && en->chooseMode == 0)) {
		char in = (char) sym & 0xff;
		char * half1 = strndup(en->buf, en->cur);
		char * half2 = strdup(en->buf+en->cur);
		en->buf = realloc(en->buf, en->len+2);
		sprintf(en->buf, "%s%c%s", half1, in, half2);
		en->len++; en->cur++;
		free(half1); free(half2);
		en->chooseMode = 0;
	} else if (FcitxHotkeyIsHotKeyDigit(sym, state) && en->chooseMode == 1) {
		// in choose mode
			return IRV_TO_PROCESS;
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_BACKSPACE)) {
		if (en->cur == 0) {
			return IRV_TO_PROCESS; //hit end
		}
		if (en->cur>0) {
			char * half1 = strndup(en->buf, en->cur-1);
			char * half2 = strdup(en->buf+en->cur);
			en->buf = realloc(en->buf, en->len);
			sprintf(en->buf, "%s%s", half1, half2);
			en->len--; en->cur--;
			free(half1); free(half2);
		}
		en->chooseMode = 0;
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_DELETE)) {
		if (en->cur == en->len) {
			return IRV_TO_PROCESS; //hit end
		}
		if (en->cur < en->len) {
			char * half1 = strndup(en->buf, en->cur);
			char * half2 = strdup(en->buf+en->cur+1);
			en->buf = realloc(en->buf, en->len);
			sprintf(en->buf, "%s%s", half1, half2);
			en->len--;
			free(half1); free(half2);
		}
		en->chooseMode = 0;
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_RIGHT)) {
		if (en->cur < en->len && en->chooseMode == 0)
			en->cur++; // not in chooseMode
		else
			return IRV_TO_PROCESS;
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_LEFT)) {
		if (en->cur > 0 && en->chooseMode == 0)
			en->cur--; // not in chooseMode
		else
			return IRV_TO_PROCESS;
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_ESCAPE)) {
		return IRV_CLEAN; // not in chooseMode, reset status
	} else if (FcitxHotkeyIsHotKey(sym, state, FCITX_TAB)) {
		if (en->len == 0) {
			return IRV_TO_PROCESS;
		}
		if (en->chooseMode == 0 && strlen(en->buf) >= 3)
			en->chooseMode = 1; // in chooseMode, cancel chooseMode
		else
			en->chooseMode = 0;
	} else if (FcitxHotkeyIsHotKeySimple(sym, state) || FcitxHotkeyIsHotKey(sym, state, FCITX_ENTER)) {
		if (en->len == 0) {
			return IRV_TO_PROCESS;
		}
		// sym is symbol, or enter, so it is the end of word
		char in = (char) sym & 0xff;
		en->buf = realloc(en->buf, en->len+2);
		sprintf(en->buf, "%s%c", en->buf, in);
		strcpy(FcitxInputStateGetOutputString(input), en->buf);
		return IRV_COMMIT_STRING;
    } else {
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
    en->len= en->cur = 0;
    free(en->buf);
    en->buf = strdup("");
    en->chooseMode = 0;
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

	if(en->chooseMode) {
		int index = 0;
		char ** candList;
		int candNum = en_prefix_suggest(en->buf, &candList);
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
		en_free_list(&candList, candNum);
    }
    // setup cursor
    FcitxInputStateSetShowCursor(input, true);
    FcitxLog(DEBUG, "buf len: %d, cur: %d", en->len, en->cur);
    FcitxInputStateSetCursorPos(input, en->cur);
    FcitxInputStateSetClientCursorPos(input, en->cur);

    FcitxMessagesAddMessageAtLast(msgPreedit, MSG_INPUT, "%s", en->buf);
    FcitxMessagesAddMessageAtLast(clientPreedit, MSG_INPUT, "%s", en->buf);

    return IRV_DISPLAY_CANDWORDS;
}

INPUT_RETURN_VALUE FcitxEnGetCandWord(void* arg, FcitxCandidateWord* candWord)
{
    FcitxEn* en = (FcitxEn*) candWord->owner;
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
__EXPORT_API
void FcitxEnDestroy(void* arg)
{
    FcitxEn* en = (FcitxEn*) arg;
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
    // none at the moment
}
