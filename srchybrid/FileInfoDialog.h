//this file is part of eMule
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#pragma once
#include "ResizableLib/ResizablePage.h"
#include "RichEditCtrlX.h"
#include "Preferences.h"
#include "OtherFunctions.h"

class CKnownFile;
struct SMediaInfo;

// MediaInfoDLL
/** @brief Kinds of Stream */
typedef enum MediaInfo_stream_t
{
	MediaInfo_Stream_General,
	MediaInfo_Stream_Video,
	MediaInfo_Stream_Audio,
	MediaInfo_Stream_Text,
	MediaInfo_Stream_Other,
	MediaInfo_Stream_Image,
	MediaInfo_Stream_Menu,
	MediaInfo_Stream_Max
} MediaInfo_stream_C;

/** @brief Kinds of Info */
typedef enum MediaInfo_info_t
{
	MediaInfo_Info_Name,
	MediaInfo_Info_Text,
	MediaInfo_Info_Measure,
	MediaInfo_Info_Options,
	MediaInfo_Info_Name_Text,
	MediaInfo_Info_Measure_Text,
	MediaInfo_Info_Info,
	MediaInfo_Info_HowTo,
	MediaInfo_Info_Max
} MediaInfo_info_C;

/////////////////////////////////////////////////////////////////////////////
// CMediaInfoDLL

class CMediaInfoDLL
{
public:
	CMediaInfoDLL()
		: m_ullVersion()
		, m_hLib()
		, m_bInitialized()
		, m_pfnMediaInfo_New()
		, m_pfnMediaInfo_Open()
		, m_pfnMediaInfo_Close()
		, m_pfnMediaInfo_Delete()
		, m_pfnMediaInfo_Get()
		, m_pfnMediaInfo_GetI()
	{
	}

	~CMediaInfoDLL()
	{
		if (m_hLib)
			FreeLibrary(m_hLib);
	}

	bool Initialize()
	{
		if (!m_bInitialized) {
			m_bInitialized = true;

			CString strPath(theApp.GetProfileString(_T("eMule"), _T("MediaInfo_MediaInfoDllPath"), _T("MEDIAINFO.DLL")));
			if (strPath == _T("<noload>"))
				return false;
			m_hLib = LoadLibrary(strPath);
			if (m_hLib == NULL) {
				CRegKey key;
				if (key.Open(HKEY_CURRENT_USER, _T("Software\\MediaInfo"), KEY_READ) == ERROR_SUCCESS) {
					TCHAR szPath[MAX_PATH];
					ULONG ulChars = _countof(szPath);
					if (key.QueryStringValue(_T("Path"), szPath, &ulChars) == ERROR_SUCCESS) {
						LPTSTR pszResult = ::PathCombine(strPath.GetBuffer(MAX_PATH), szPath, _T("MEDIAINFO.DLL"));
						strPath.ReleaseBuffer();
						if (pszResult)
							m_hLib = LoadLibrary(strPath);
					}
				}
			}
			if (m_hLib == NULL) {
				CString strProgramFiles = ShellGetFolderPath(CSIDL_PROGRAM_FILES);
				if (!strProgramFiles.IsEmpty()) {
					LPTSTR pszResult = ::PathCombine(strPath.GetBuffer(MAX_PATH), strProgramFiles, _T("MediaInfo\\MEDIAINFO.DLL"));
					strPath.ReleaseBuffer();
					if (pszResult)
						m_hLib = LoadLibrary(strPath);
				}
			}

			// Support of very old versions at some point becomes difficult, and even unreasonable.
			// For example, in 2020 it was hard to find MediaInfo v0.4.* and v0.5.* in the net.
			// Currently the oldest allowed version would be v0.7.13 (released in April, 2009).
			if (m_hLib != NULL) {
				// Note from MediaInfo developer
				// -----------------------------
				// Note : versioning method, for people who develop with LoadLibrary method
				// - if one of 2 first numbers change, there is no guaranty that the DLL is compatible with old one
				// - if one of 2 last numbers change, there is a guaranty that the DLL is compatible with old one.
				// So you should test the version of the DLL, and if one of the 2 first numbers change, not load it.
				// ---
				ULONGLONG ullVersion = GetModuleVersion(m_hLib);
				if (ullVersion >= MAKEDLLVERULL(0, 7, 13, 0)
					&& ((thePrefs.GetWindowsVersion() == _WINVER_XP_ && ullVersion < MAKEDLLVERULL(21, 4, 0, 0))
						|| ullVersion < MAKEDLLVERULL(23, 11, 0, 0)))
				{
					(FARPROC&)m_pfnMediaInfo_New = GetProcAddress(m_hLib, "MediaInfo_New");
					(FARPROC&)m_pfnMediaInfo_Delete = GetProcAddress(m_hLib, "MediaInfo_Delete");
					(FARPROC&)m_pfnMediaInfo_Open = GetProcAddress(m_hLib, "MediaInfo_Open");
					(FARPROC&)m_pfnMediaInfo_Close = GetProcAddress(m_hLib, "MediaInfo_Close");
					(FARPROC&)m_pfnMediaInfo_Get = GetProcAddress(m_hLib, "MediaInfo_Get");
					(FARPROC&)m_pfnMediaInfo_GetI = GetProcAddress(m_hLib, "MediaInfo_GetI");
					if (m_pfnMediaInfo_New && m_pfnMediaInfo_Delete && m_pfnMediaInfo_Open && m_pfnMediaInfo_Close && m_pfnMediaInfo_Get)
						m_ullVersion = ullVersion;
				}
				if (!m_ullVersion) {
					m_pfnMediaInfo_New = NULL;
					m_pfnMediaInfo_Delete = NULL;
					m_pfnMediaInfo_Open = NULL;
					m_pfnMediaInfo_Close = NULL;
					m_pfnMediaInfo_Get = NULL;
					m_pfnMediaInfo_GetI = NULL;
					FreeLibrary(m_hLib);
					m_hLib = NULL;
				}
			}
		}
		return m_hLib != NULL;
	}

	ULONGLONG GetVersion() const
	{
		return m_ullVersion;
	}

	void* Open(LPCTSTR File)
	{
		if (!m_pfnMediaInfo_New)
			return NULL;
		void* Handle = (*m_pfnMediaInfo_New)();
		if (Handle)
			(*m_pfnMediaInfo_Open)(Handle, File);
		return Handle;
	}

	void Close(void* Handle)
	{
		if (m_pfnMediaInfo_Delete)
			(*m_pfnMediaInfo_Delete)(Handle);	// File is closed automatically
		else if (m_pfnMediaInfo_Close)
			(*m_pfnMediaInfo_Close)(Handle);
	}

	CString Get(void* Handle, MediaInfo_stream_C StreamKind, int StreamNumber, LPCTSTR Parameter, MediaInfo_info_C KindOfInfo, MediaInfo_info_C KindOfSearch)
	{
		if (!m_pfnMediaInfo_Get)
			return CString();
		return (*m_pfnMediaInfo_Get)(Handle, StreamKind, StreamNumber, Parameter, KindOfInfo, KindOfSearch);
	}

	CString GetI(void* Handle, MediaInfo_stream_C StreamKind, size_t StreamNumber, size_t iParameter, MediaInfo_info_C KindOfInfo)
	{
		return CString((*m_pfnMediaInfo_GetI)(Handle, StreamKind, StreamNumber, iParameter, KindOfInfo));
	}

protected:
	ULONGLONG m_ullVersion;
	HINSTANCE m_hLib;
	bool m_bInitialized;

	// MediaInfoLib: 0.7.13-0.7.99, 17.10-20.08
	void* (__stdcall* m_pfnMediaInfo_New)();
	int(__stdcall* m_pfnMediaInfo_Open)(void* Handle, const wchar_t* File);
	void(__stdcall* m_pfnMediaInfo_Close)(void* Handle);
	void(__stdcall* m_pfnMediaInfo_Delete)(void* Handle);
	const wchar_t* (__stdcall* m_pfnMediaInfo_Get)(void* Handle, MediaInfo_stream_C StreamKind, size_t StreamNumber, const wchar_t* Parameter, MediaInfo_info_C KindOfInfo, MediaInfo_info_C KindOfSearch);
	const wchar_t* (__stdcall* m_pfnMediaInfo_GetI)(void* Handle, MediaInfo_stream_C StreamKind, size_t StreamNumber, size_t Parameter, MediaInfo_info_C KindOfInfo);
};

/////////////////////////////////////////////////////////////////////////////
// CFileInfoDialog dialog

class CFileInfoDialog : public CResizablePage
{
	DECLARE_DYNAMIC(CFileInfoDialog)

	enum
	{
		IDD = IDD_FILEINFO
	};
	void InitDisplay(LPCTSTR pStr);
	CSimpleArray<CObject*> m_pFiles;
public:
	CFileInfoDialog();   // standard constructor
	virtual BOOL OnInitDialog();

	void SetFiles(const CSimpleArray<CObject*> *paFiles)	{ m_paFiles = paFiles; m_bDataChanged = true; }
	void SetReducedDialog()									{ m_bReducedDlg = true; }
	void Localize();

protected:
	const CSimpleArray<CObject*> *m_paFiles;
	bool m_bDataChanged;
	CRichEditCtrlX m_fi;
	bool m_bReducedDlg;

	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support
	virtual BOOL OnSetActive();

	DECLARE_MESSAGE_MAP()
	afx_msg LRESULT OnMediaInfoResult(WPARAM, LPARAM);
	afx_msg LRESULT OnDataChanged(WPARAM, LPARAM);
	afx_msg void OnDestroy();
};