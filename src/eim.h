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

#ifndef EIM_H
#define EIM_H

#include <fcitx/ime.h>
#include <fcitx/instance.h>
#include <fcitx/candidate.h>

#ifdef __cplusplus
#define __EXPORT_API extern "C"
#else
#define __EXPORT_API
#endif

#define _(x) dgettext("fcitx-en", x)

__EXPORT_API void *FcitxEnCreate(FcitxInstance * instance);
__EXPORT_API void FcitxEnDestroy(void *arg);
__EXPORT_API INPUT_RETURN_VALUE FcitxEnDoInput(void *arg, FcitxKeySym sym, unsigned int state);
__EXPORT_API INPUT_RETURN_VALUE FcitxEnGetCandWords(void *arg);
__EXPORT_API boolean FcitxEnInit(void *);
__EXPORT_API void FcitxEnReset(void *arg);

typedef struct _FcitxEnConfig
{
  FcitxGenericConfig config;
} FcitxEnConfig;

typedef struct _node
{
  char *word;
  struct _node *next;
} node;

typedef struct _FcitxEn
{
  FcitxEnConfig config;
  FcitxInstance *owner;
  node *dic;
  char *buf;
  int cur;
} FcitxEn;

CONFIG_BINDING_DECLARE(FcitxEnConfig);

#endif
