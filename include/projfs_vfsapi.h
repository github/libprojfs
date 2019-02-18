/* VFSForGit ProjFS API
   Copyright (C) 2018-2019 GitHub, Inc.

   See the NOTICE file distributed with this library for additional
   information regarding copyright ownership.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library, in the file COPYING; if not,
   see <http://www.gnu.org/licenses/>.
 */

/*
   This -- and only this -- header file may also be distributed under
   the terms of the MIT License as follows:

   Copyright (C) Microsoft Corporation. All rights reserved.

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE

   You should have received a copy of the MIT License along with this
   library, in the file COPYING; if not, see
   <https://opensource.org/licenses/MIT>
 */

#ifndef PROJFS_VFSAPI_H
#define PROJFS_VFSAPI_H

/** @file
 *
 * This file defines the VFSforGit interface of ProjFS
 */

#ifdef HAVE_STDINT_H
#include <stdint.h>			/* for uint16_t */
#endif

#ifdef HAVE_STDBOOL_H
#include <stdbool.h>			/* for bool */
#else
#ifndef __cplusplus
#ifdef HAVE__BOOL
#define bool _Bool
#else
#define bool unsigned char
#endif /* HAVE__BOOL */
#endif /* __cplusplus */
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define _In_
#define _Out_

#define PrjFS_PlaceholderIdLength 128

typedef struct _PrjFS_MountHandle PrjFS_MountHandle;

typedef struct _PrjFS_FileHandle PrjFS_FileHandle;

typedef struct _PrjFS_Callbacks PrjFS_Callbacks;

typedef enum
{
    PrjFS_Result_Invalid                            = 0x00000000,

    PrjFS_Result_Success                            = 0x00000001,
    PrjFS_Result_Pending                            = 0x00000002,

    // Bugs in the caller
    PrjFS_Result_EInvalidArgs                       = 0x10000001,
    PrjFS_Result_EInvalidOperation                  = 0x10000002,
    PrjFS_Result_ENotSupported                      = 0x10000004,

    // Runtime errors
    PrjFS_Result_EDriverNotLoaded                   = 0x20000001,
    PrjFS_Result_EOutOfMemory                       = 0x20000002,
    PrjFS_Result_EFileNotFound                      = 0x20000004,
    PrjFS_Result_EPathNotFound                      = 0x20000008,
    PrjFS_Result_EAccessDenied                      = 0x20000010,
    PrjFS_Result_EInvalidHandle                     = 0x20000020,
    PrjFS_Result_EIOError                           = 0x20000040,
    PrjFS_Result_EDirectoryNotEmpty                 = 0x20000200,
    PrjFS_Result_EVirtualizationInvalidOperation    = 0x20000400,

    PrjFS_Result_ENotYetImplemented                 = 0xFFFFFFFF
} PrjFS_Result;

typedef enum
{
    PrjFS_NotificationType_Invalid                  = 0x00000000,

    PrjFS_NotificationType_None                     = 0x00000001,
    PrjFS_NotificationType_NewFileCreated           = 0x00000004,
    PrjFS_NotificationType_PreDelete                = 0x00000010,
    PrjFS_NotificationType_FileRenamed              = 0x00000080,
    PrjFS_NotificationType_HardLinkCreated          = 0x00000100,
    PrjFS_NotificationType_PreConvertToFull         = 0x00001000,

    PrjFS_NotificationType_PreModify                = 0x10000001,
    PrjFS_NotificationType_FileModified             = 0x10000002,
    PrjFS_NotificationType_FileDeleted              = 0x10000004
} PrjFS_NotificationType;

#if 0
typedef struct
{
    _In_    PrjFS_NotificationType                  NotificationBitMask;
    _In_    const char*                             NotificationRelativeRoot;
} PrjFS_NotificationMapping;
#endif

PrjFS_Result PrjFS_StartVirtualizationInstance(
    _In_    const char*                             storageRootFullPath,
    _In_    const char*                             virtualizationRootFullPath,
    _In_    PrjFS_Callbacks                         callbacks,
    _In_    unsigned int                            poolThreadCount,
    _Out_   PrjFS_MountHandle**                     mountHandle
);

void PrjFS_StopVirtualizationInstance(
    _In_    const PrjFS_MountHandle*                mountHandle
);

PrjFS_Result PrjFS_WritePlaceholderDirectory(
    _In_    const PrjFS_MountHandle*                mountHandle,
    _In_    const char*                             relativePath
);

PrjFS_Result PrjFS_WritePlaceholderFile(
    _In_    const PrjFS_MountHandle*                mountHandle,
    _In_    const char*                             relativePath,
    _In_    unsigned char                           providerId[PrjFS_PlaceholderIdLength],
    _In_    unsigned char                           contentId[PrjFS_PlaceholderIdLength],
    _In_    unsigned long                           fileSize,
    _In_    uint16_t                                fileMode
);

#if 0
PrjFS_Result PrjFS_ConvertDirectoryToPlaceholder(
    _In_    const char*                             relativePath
);
#endif

PrjFS_Result PrjFS_WriteSymLink(
    _In_    const PrjFS_MountHandle*                mountHandle,
    _In_    const char*                             relativePath,
    _In_    const char*                             symLinkTarget
);

#if 0
typedef enum
{
    PrjFS_UpdateType_Invalid                        = 0x00000000,

    PrjFS_UpdateType_AllowReadOnly                  = 0x00000020
} PrjFS_UpdateType;

typedef enum
{
    PrjFS_UpdateFailureCause_Invalid                = 0x00000000,

    PrjFS_UpdateFailureCause_FullFile               = 0x00000002,
    PrjFS_UpdateFailureCause_ReadOnly               = 0x00000008
} PrjFS_UpdateFailureCause;

PrjFS_Result PrjFS_UpdatePlaceholderFileIfNeeded(
    _In_    const char*                             relativePath,
    _In_    unsigned char                           providerId[PrjFS_PlaceholderIdLength],
    _In_    unsigned char                           contentId[PrjFS_PlaceholderIdLength],
    _In_    unsigned long                           fileSize,
    _In_    uint16_t                                fileMode,
    _In_    PrjFS_UpdateType                        updateFlags,
    _Out_   PrjFS_UpdateFailureCause*               failureCause
);

PrjFS_Result PrjFS_ReplacePlaceholderFileWithSymLink(
    _In_    const char*                             relativePath,
    _In_    const char*                             symLinkTarget,
    _In_    PrjFS_UpdateType                        updateFlags,
    _Out_   PrjFS_UpdateFailureCause*               failureCause
);

PrjFS_Result PrjFS_DeleteFile(
    _In_    const char*                             relativePath,
    _In_    PrjFS_UpdateType                        updateFlags,
    _Out_   PrjFS_UpdateFailureCause*               failureCause
);
#endif

PrjFS_Result PrjFS_WriteFileContents(
    _In_    const PrjFS_FileHandle*                 fileHandle,
    _In_    const void*                             bytes,
    _In_    unsigned int                            byteCount
);

#if 0
typedef enum
{
    PrjFS_FileState_Invalid                         = 0x00000000,

    PrjFS_FileState_Placeholder                     = 0x00000001,
    PrjFS_FileState_HydratedPlaceholder             = 0x00000002,
    PrjFS_FileState_Full                            = 0x00000008
} PrjFS_FileState;

PrjFS_Result PrjFS_GetOnDiskFileState(
    _In_    const char*                             fullPath,
    _Out_   unsigned int*                           fileState
);
#endif

typedef PrjFS_Result (PrjFS_EnumerateDirectoryCallback)(
    _In_    unsigned long                           commandId,
    _In_    const char*                             relativePath,
    _In_    int                                     triggeringProcessId,
    _In_    const char*                             triggeringProcessName
);

typedef PrjFS_Result (PrjFS_GetFileStreamCallback)(
    _In_    unsigned long                           commandId,
    _In_    const char*                             relativePath,
    _In_    unsigned char                           providerId[PrjFS_PlaceholderIdLength],
    _In_    unsigned char                           contentId[PrjFS_PlaceholderIdLength],
    _In_    int                                     triggeringProcessId,
    _In_    const char*                             triggeringProcessName,
    _In_    const PrjFS_FileHandle*                 fileHandle
);

typedef PrjFS_Result (PrjFS_NotifyOperationCallback)(
    _In_    unsigned long                           commandId,
    _In_    const char*                             relativePath,
    _In_    unsigned char                           providerId[PrjFS_PlaceholderIdLength],
    _In_    unsigned char                           contentId[PrjFS_PlaceholderIdLength],
    _In_    int                                     triggeringProcessId,
    _In_    const char*                             triggeringProcessName,
    _In_    bool                                    isDirectory,
    _In_    PrjFS_NotificationType                  notificationType,
    _In_    const char*                             destinationRelativePath
);

typedef struct _PrjFS_Callbacks
{
    _In_    PrjFS_EnumerateDirectoryCallback*       EnumerateDirectory;
    _In_    PrjFS_GetFileStreamCallback*            GetFileStream;
    _In_    PrjFS_NotifyOperationCallback*          NotifyOperation;
} PrjFS_Callbacks;

#if 0
PrjFS_Result PrjFS_CompleteCommand(
    _In_    unsigned long                           commandId,
    _In_    PrjFS_Result                            result
);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PROJFS_VFSAPI_H */
