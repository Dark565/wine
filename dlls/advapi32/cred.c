/*
 * Credential Management APIs
 *
 * Copyright 2007 Robert Shearman for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "wincred.h"
#include "winternl.h"

#include "crypt.h"

#include "wine/unicode.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(cred);

/* the size of the ARC4 key used to encrypt the password data */
#define KEY_SIZE 8

static const WCHAR wszCredentialManagerKey[] = {'S','o','f','t','w','a','r','e','\\','W','i','n','e','\\',
    'C','r','e','d','e','n','t','i','a','l',' ','M','a','n','a','g','e','r',0};
static const WCHAR wszEncryptionKeyValue[] = {'E','n','c','r','y','p','t','i','o','n','K','e','y',0};

static const WCHAR wszFlagsValue[] = {'F','l','a','g','s',0};
static const WCHAR wszTypeValue[] = {'T','y','p','e',0};
static const WCHAR wszTargetNameValue[] = {'T','a','r','g','e','t','N','a','m','e',0};
static const WCHAR wszCommentValue[] = {'C','o','m','m','e','n','t',0};
static const WCHAR wszLastWrittenValue[] = {'L','a','s','t','W','r','i','t','t','e','n',0};
static const WCHAR wszPersistValue[] = {'P','e','r','s','i','s','t',0};
static const WCHAR wszTargetAliasValue[] = {'T','a','r','g','e','t','A','l','i','a','s',0};
static const WCHAR wszUserNameValue[] = {'U','s','e','r','N','a','m','e',0};
static const WCHAR wszPasswordValue[] = {'P','a','s','s','w','o','r','d',0};

static DWORD write_credential_blob(HKEY hkey, LPCWSTR target_name, DWORD type,
                                   const BYTE key_data[KEY_SIZE],
                                   const BYTE *credential_blob, DWORD credential_blob_size)
{
    LPBYTE encrypted_credential_blob;
    struct ustring data;
    struct ustring key;
    DWORD ret;

    key.Length = key.MaximumLength = sizeof(key_data);
    key.Buffer = (unsigned char *)key_data;

    encrypted_credential_blob = HeapAlloc(GetProcessHeap(), 0, credential_blob_size);
    if (!encrypted_credential_blob) return ERROR_OUTOFMEMORY;

    memcpy(encrypted_credential_blob, credential_blob, credential_blob_size);
    data.Length = data.MaximumLength = credential_blob_size;
    data.Buffer = encrypted_credential_blob;
    SystemFunction032(&data, &key);

    ret = RegSetValueExW(hkey, wszPasswordValue, 0, REG_BINARY, (LPVOID)encrypted_credential_blob, credential_blob_size);
    HeapFree(GetProcessHeap(), 0, encrypted_credential_blob);

    return ret;
}

static DWORD write_credential(HKEY hkey, const CREDENTIALW *credential,
                              const BYTE key_data[KEY_SIZE], BOOL preserve_blob)
{
    DWORD ret;
    FILETIME LastWritten;

    GetSystemTimeAsFileTime(&LastWritten);

    ret = RegSetValueExW(hkey, wszFlagsValue, 0, REG_DWORD, (LPVOID)&credential->Flags,
                         sizeof(credential->Flags));
    if (ret != ERROR_SUCCESS) return ret;
    ret = RegSetValueExW(hkey, wszTypeValue, 0, REG_DWORD, (LPVOID)&credential->Type,
                         sizeof(credential->Type));
    if (ret != ERROR_SUCCESS) return ret;
    ret = RegSetValueExW(hkey, NULL, 0, REG_SZ, (LPVOID)credential->TargetName,
                         sizeof(WCHAR)*(strlenW(credential->TargetName)+1));
    if (ret != ERROR_SUCCESS) return ret;
    if (credential->Comment)
    {
        ret = RegSetValueExW(hkey, wszCommentValue, 0, REG_SZ, (LPVOID)credential->Comment,
                             sizeof(WCHAR)*(strlenW(credential->Comment)+1));
        if (ret != ERROR_SUCCESS) return ret;
    }
    ret = RegSetValueExW(hkey, wszLastWrittenValue, 0, REG_BINARY, (LPVOID)&LastWritten,
                         sizeof(LastWritten));
    if (ret != ERROR_SUCCESS) return ret;
    ret = RegSetValueExW(hkey, wszPersistValue, 0, REG_DWORD, (LPVOID)&credential->Persist,
                         sizeof(credential->Persist));
    if (ret != ERROR_SUCCESS) return ret;
    /* FIXME: Attributes */
    if (credential->TargetAlias)
    {
        ret = RegSetValueExW(hkey, wszTargetAliasValue, 0, REG_SZ, (LPVOID)credential->TargetAlias,
                             sizeof(WCHAR)*(strlenW(credential->TargetAlias)+1));
        if (ret != ERROR_SUCCESS) return ret;
    }
    if (credential->UserName)
    {
        ret = RegSetValueExW(hkey, wszUserNameValue, 0, REG_SZ, (LPVOID)credential->UserName,
                             sizeof(WCHAR)*(strlenW(credential->UserName)+1));
        if (ret != ERROR_SUCCESS) return ret;
    }
    if (!preserve_blob)
    {
        ret = write_credential_blob(hkey, credential->TargetName, credential->Type,
                                    key_data, credential->CredentialBlob,
                                    credential->CredentialBlobSize);
    }
    return ret;
}

static DWORD open_cred_mgr_key(HKEY *hkey, BOOL open_for_write)
{
    return RegCreateKeyExW(HKEY_CURRENT_USER, wszCredentialManagerKey, 0,
                           NULL, REG_OPTION_NON_VOLATILE,
                           KEY_READ | KEY_WRITE, NULL, hkey, NULL);
}

static DWORD get_cred_mgr_encryption_key(HKEY hkeyMgr, BYTE key_data[KEY_SIZE])
{
    static const BYTE my_key_data[KEY_SIZE] = { 0 };
    DWORD type;
    DWORD count;
    FILETIME ft;
    ULONG seed;
    ULONG value;
    DWORD ret;

    memcpy(key_data, my_key_data, KEY_SIZE);

    count = KEY_SIZE;
    ret = RegQueryValueExW(hkeyMgr, wszEncryptionKeyValue, NULL, &type, (LPVOID)key_data,
                           &count);
    if (ret == ERROR_SUCCESS)
    {
        if (type != REG_BINARY)
            return ERROR_REGISTRY_CORRUPT;
        else
            return ERROR_SUCCESS;
    }
    if (ret != ERROR_FILE_NOT_FOUND)
        return ret;

    GetSystemTimeAsFileTime(&ft);
    seed = ft.dwLowDateTime;
    value = RtlUniform(&seed);
    *(DWORD *)key_data = value;
    seed = ft.dwHighDateTime;
    value = RtlUniform(&seed);
    *(DWORD *)(key_data + 4) = value;

    return RegSetValueExW(hkeyMgr, wszEncryptionKeyValue, 0, REG_BINARY,
                          (LPVOID)key_data, KEY_SIZE);
}

static LPWSTR get_key_name_for_target(LPCWSTR target_name, DWORD type)
{
    static const WCHAR wszGenericPrefix[] = {'G','e','n','e','r','i','c',':',' ',0};
    static const WCHAR wszDomPasswdPrefix[] = {'D','o','m','P','a','s','s','w','d',':',' ',0};
    INT len;
    LPCWSTR prefix = NULL;
    LPWSTR key_name, p;

    len = strlenW(target_name);
    if (type == CRED_TYPE_GENERIC)
    {
        prefix = wszGenericPrefix;
        len += sizeof(wszGenericPrefix)/sizeof(wszGenericPrefix[0]);
    }
    else
    {
        prefix = wszDomPasswdPrefix;
        len += sizeof(wszDomPasswdPrefix)/sizeof(wszDomPasswdPrefix[0]);
    }

    key_name = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    if (!key_name) return NULL;

    strcpyW(key_name, prefix);
    strcatW(key_name, target_name);

    for (p = key_name; *p; p++)
        if (*p == '\\') *p = '_';

    return key_name;
}

/******************************************************************************
 * CredWriteW [ADVAPI32.@]
 */
BOOL WINAPI CredWriteW(PCREDENTIALW Credential, DWORD Flags)
{
    HKEY hkeyMgr;
    HKEY hkeyCred;
    DWORD ret;
    LPWSTR key_name;
    BYTE key_data[KEY_SIZE];

    TRACE("(%p, 0x%x)\n", Credential, Flags);

    if (!Credential || !Credential->TargetName)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (Flags & ~CRED_PRESERVE_CREDENTIAL_BLOB)
    {
        FIXME("unhandled flags 0x%x\n", Flags);
        SetLastError(ERROR_INVALID_FLAGS);
        return FALSE;
    }

    if (Credential->Type != CRED_TYPE_GENERIC && Credential->Type != CRED_TYPE_DOMAIN_PASSWORD)
    {
        FIXME("unhandled type %d\n", Credential->Type);
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    TRACE("Credential->TargetName = %s\n", debugstr_w(Credential->TargetName));
    TRACE("Credential->UserName = %s\n", debugstr_w(Credential->UserName));

    if (Credential->Type == CRED_TYPE_DOMAIN_PASSWORD)
    {
        if (!Credential->UserName ||
            (!strchrW(Credential->UserName, '\\') && !strchrW(Credential->UserName, '@')))
        {
            ERR("bad username %s\n", debugstr_w(Credential->UserName));
            SetLastError(ERROR_BAD_USERNAME);
            return FALSE;
        }
    }

    ret = open_cred_mgr_key(&hkeyMgr, FALSE);
    if (ret != ERROR_SUCCESS)
    {
        WARN("couldn't open/create manager key, error %d\n", ret);
        SetLastError(ERROR_NO_SUCH_LOGON_SESSION);
        return FALSE;
    }

    ret = get_cred_mgr_encryption_key(hkeyMgr, key_data);
    if (ret != ERROR_SUCCESS)
    {
        RegCloseKey(hkeyMgr);
        SetLastError(ret);
        return FALSE;
    }

    key_name = get_key_name_for_target(Credential->TargetName, Credential->Type);
    ret = RegCreateKeyExW(hkeyMgr, key_name, 0, NULL, REG_OPTION_VOLATILE,
                          KEY_READ|KEY_WRITE, NULL, &hkeyCred, NULL);
    HeapFree(GetProcessHeap(), 0, key_name);
    if (ret != ERROR_SUCCESS)
    {
        TRACE("credentials for target name %s not found\n",
              debugstr_w(Credential->TargetName));
        SetLastError(ERROR_NOT_FOUND);
        return FALSE;
    }

    ret = write_credential(hkeyCred, Credential, key_data,
                           Flags & CRED_PRESERVE_CREDENTIAL_BLOB);

    RegCloseKey(hkeyCred);
    RegCloseKey(hkeyMgr);

    if (ret != ERROR_SUCCESS)
    {
        SetLastError(ret);
        return FALSE;
    }
    return TRUE;
}
