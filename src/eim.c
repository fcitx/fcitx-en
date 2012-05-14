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

#define MAX_WORD_LEN 64

FCITX_EXPORT_API FcitxIMClass ime = {
  FcitxEnCreate, FcitxEnDestroy
};

FCITX_EXPORT_API int ABI_VERSION = FCITX_ABI_VERSION;

CONFIG_DESC_DEFINE(GetFcitxEnConfigDesc, "fcitx-en.desc")
static INPUT_RETURN_VALUE
FcitxEnGetCandWord(void *arg, FcitxCandidateWord * candWord);
static void
FcitxEnReloadConfig(void *arg);
static boolean
LoadEnConfig(FcitxEnConfig * fs);
static void
SaveEnConfig(FcitxEnConfig * fs);
static void
ConfigEn(FcitxEn * en);
static boolean
GoodMatch(const char *current, const char *dictWord);
static float
Distance(const char *s1, const char *s2, const int maxOffset);
const FcitxHotkey FCITX_TAB[2] =
  { {NULL, FcitxKey_Tab, FcitxKeyState_None}, {NULL, FcitxKey_None, FcitxKeyState_None} };
const FcitxHotkey FCITX_HYPHEN[2] =
  { {NULL, FcitxKey_minus, FcitxKeyState_None}, {NULL, FcitxKey_None, FcitxKeyState_None} };
const FcitxHotkey FCITX_APOS[2] =
  { {NULL, FcitxKey_apostrophe, FcitxKeyState_None}, {NULL, FcitxKey_None, FcitxKeyState_None} };

/**
 * @brief initialize the extra input method
 *
 * @param arg
 * @return successful or not
 **/
__EXPORT_API void *
FcitxEnCreate(FcitxInstance * instance)
{
  if (GetFcitxEnConfigDesc() == NULL)
    return NULL;

  FcitxEn *en = (FcitxEn *) fcitx_utils_malloc0(sizeof(FcitxEn));
  FcitxGlobalConfig *config = FcitxInstanceGetGlobalConfig(instance);
  FcitxInputState *input = FcitxInstanceGetInputState(instance);
  FcitxCandidateWordSetChoose(FcitxInputStateGetCandidateList(input), DIGIT_STR_CHOOSE);

  bindtextdomain("fcitx-en", LOCALEDIR);

  //load dic
  FILE *file = fopen(EN_DIC_FILE, "r");
  if (file == NULL)
    return NULL;
  char line[MAX_WORD_LEN];
  node **tmp = &(en->dic);
  while (fgets(line, sizeof(line), file) != NULL) {
    line[strlen(line) - 1] = '\0';      // remove newline
    *tmp = (node *) malloc(sizeof(node));
    (*tmp)->word = strdup(line);
    (*tmp)->next = NULL;
    tmp = &((*tmp)->next);
  }
  fclose(file);


  en->owner = instance;
  en->cur = 0;
  en->buf = strdup("");
  FcitxCandidateWordSetPageSize(FcitxInputStateGetCandidateList(input), config->iMaxCandWord);
  LoadEnConfig(&en->config);
  ConfigEn(en);

  FcitxInstanceRegisterIM(instance,
                          en,
                          "en",
                          _("En"),
                          "en",
                          FcitxEnInit,
                          FcitxEnReset,
                          FcitxEnDoInput, FcitxEnGetCandWords, NULL, NULL, FcitxEnReloadConfig, NULL, 1, "en_US");
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

  if (FcitxHotkeyIsHotKeyLAZ(sym, state) || FcitxHotkeyIsHotKeyUAZ(sym, state) ||
      FcitxHotkeyIsHotKey(sym, state, FCITX_HYPHEN) || FcitxHotkeyIsHotKey(sym, state, FCITX_APOS)) {
    char in = (char) sym & 0xff;
    char *half1 = strndup(en->buf, en->cur);
    char *half2 = strdup(en->buf + en->cur);
    en->buf = realloc(en->buf, buf_len + 2);
    sprintf(en->buf, "%s%c%s", half1, in, half2);
    en->cur++;
    free(half1);
    free(half2);
  } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_BACKSPACE)) {
    if (buf_len == 0)
      return IRV_TO_PROCESS;    // end
    if (en->cur > 0) {
      char *half1 = strndup(en->buf, en->cur - 1);
      char *half2 = strdup(en->buf + en->cur);
      en->buf = realloc(en->buf, buf_len);
      sprintf(en->buf, "%s%s", half1, half2);
      en->cur--;
      free(half1);
      free(half2);
    }
  } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_DELETE)) {
    if (buf_len == 0)
      return IRV_TO_PROCESS;
    if (en->cur < buf_len) {
      char *half1 = strndup(en->buf, en->cur);
      char *half2 = strdup(en->buf + en->cur + 1);
      en->buf = realloc(en->buf, buf_len);
      sprintf(en->buf, "%s%s", half1, half2);
      free(half1);
      free(half2);
    }
  } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_ESCAPE)) {
    return IRV_CLEAN;           // not in chooseMode, reset status
  } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_TAB)) {
    if (buf_len == 0)
      return IRV_TO_PROCESS;
    else if (buf_len > 2) {
		node *tmp;
		for (tmp = en->dic; tmp != NULL; tmp = tmp->next) {
		  if (GoodMatch(en->buf, tmp->word)) {
			int tmp_len = strlen(tmp->word);
			en->buf = realloc(en->buf, tmp_len + 1);
			strcpy(en->buf, tmp->word);
			en->cur = tmp_len;
			break;
		  }
		}
    }
  } else if (FcitxHotkeyIsHotKeySimple(sym, state) || FcitxHotkeyIsHotKey(sym, state, FCITX_ENTER)) {
    if (buf_len == 0 || FcitxHotkeyIsHotKeyDigit(sym, state))
      return IRV_TO_PROCESS;
    // sym is symbol, or enter, so it is the end of word
    if (FcitxHotkeyIsHotKeySimple(sym, state)) {        // for enter key
      char in = (char) sym & 0xff;
      char *old = strdup(en->buf);
      en->buf = realloc(en->buf, buf_len + 2);
      sprintf(en->buf, "%s%c", old, in);
      free(old);
    }
    strcpy(FcitxInputStateGetOutputString(input), en->buf);
    return IRV_COMMIT_STRING;
  } else {
    return IRV_TO_PROCESS;
  }
  if (strlen(en->buf) > 0)
    return IRV_DISPLAY_CANDWORDS;
  else
    return IRV_CLEAN;
}

__EXPORT_API boolean
FcitxEnInit(void *arg)
{
  FcitxEn *en = (FcitxEn *) arg;
  FcitxInstanceSetContext(en->owner, CONTEXT_IM_KEYBOARD_LAYOUT, "us");
  FcitxInstanceSetContext(en->owner, CONTEXT_ALTERNATIVE_PREVPAGE_KEY, FCITX_LEFT);
  FcitxInstanceSetContext(en->owner, CONTEXT_ALTERNATIVE_NEXTPAGE_KEY, FCITX_RIGHT);
  return true;
}

__EXPORT_API void
FcitxEnReset(void *arg)
{
  FcitxEn *en = (FcitxEn *) arg;
  en->cur = 0;
  free(en->buf);
  en->buf = strdup("");
}


/**
 * @brief function DoInput has done everything for us.
 *
 * @param searchMode
 * @return INPUT_RETURN_VALUE
 **/
__EXPORT_API INPUT_RETURN_VALUE
FcitxEnGetCandWords(void *arg)
{
  FcitxEn *en = (FcitxEn *) arg;
  FcitxInputState *input = FcitxInstanceGetInputState(en->owner);
  FcitxMessages *msgPreedit = FcitxInputStateGetPreedit(input);
  FcitxMessages *clientPreedit = FcitxInputStateGetClientPreedit(input);
  FcitxGlobalConfig *config = FcitxInstanceGetGlobalConfig(en->owner);

  FcitxCandidateWordSetPageSize(FcitxInputStateGetCandidateList(input), config->iMaxCandWord);

  //clean up window asap
  FcitxInstanceCleanInputWindow(en->owner);

  ConfigEn(en);

  FcitxLog(DEBUG, "buf: %s", en->buf);
  int buf_len = strlen(en->buf);

    node *tmp;
    int num = 0;
    for (tmp = en->dic; tmp != NULL; tmp = tmp->next) {
      if (GoodMatch(en->buf, tmp->word)) {
        FcitxCandidateWord cw;
        cw.callback = FcitxEnGetCandWord;
        cw.owner = en;
        cw.priv = NULL;
        cw.strExtra = NULL;
        cw.strWord = strdup(tmp->word);
        cw.wordType = MSG_OTHER;
        FcitxCandidateWordAppend(FcitxInputStateGetCandidateList(input), &cw);
        num++;
        if (num == 10)
          break;
      }
    }
  // setup cursor
  FcitxInputStateSetShowCursor(input, true);
  FcitxInputStateSetCursorPos(input, en->cur);
  FcitxInputStateSetClientCursorPos(input, en->cur);

  FcitxMessagesAddMessageAtLast(msgPreedit, MSG_INPUT, "%s", en->buf);
  FcitxMessagesAddMessageAtLast(clientPreedit, MSG_INPUT, "%s", en->buf);

  return IRV_DISPLAY_CANDWORDS;
}

INPUT_RETURN_VALUE
FcitxEnGetCandWord(void *arg, FcitxCandidateWord * candWord)
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
__EXPORT_API void
FcitxEnDestroy(void *arg)
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


boolean
GoodMatch(const char *current, const char *dictWord)
{
  int buf_len = strlen(current);
  if (strlen(dictWord) < buf_len -2)
	return false;
  char *tmp = strndup(current, buf_len);
  float dist = Distance(current, dictWord, 2); // search around 2 chars
  free(tmp);
  return dist < 3;
}

void
FcitxEnReloadConfig(void *arg)
{
  FcitxEn *en = (FcitxEn *) arg;
  LoadEnConfig(&en->config);
  ConfigEn(en);
}

boolean
LoadEnConfig(FcitxEnConfig * fs)
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

void
SaveEnConfig(FcitxEnConfig * fc)
{
  FcitxConfigFileDesc *configDesc = GetFcitxEnConfigDesc();
  FILE *fp = FcitxXDGGetFileUserWithPrefix("conf", "fcitx-en.config", "wt", NULL);
  FcitxConfigSaveConfigFileFp(fp, &fc->config, configDesc);
  if (fp)
    fclose(fp);
}

void
ConfigEn(FcitxEn * en)
{
  // none at the moment
}


float
Distance(const char *s1, const char *s2, const int maxOffset)
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
  return (len1 + len2) / 2 - lcs;
}
