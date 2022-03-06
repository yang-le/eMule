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
#include "OtherFunctions.h"

class CKnownFile;
struct SMediaInfo;

// MediaInfoDLL
/** @brief Kinds of Stream */
typedef enum _stream_t
{
	Stream_General,
	Stream_Video,
	Stream_Audio,
	Stream_Text,
	Stream_Chapters,
	Stream_Image,
	Stream_Max
} stream_t_C;

/** @brief Kinds of Info */
typedef enum _info_t
{
	Info_Name,
	Info_Text,
	Info_Measure,
	Info_Options,
	Info_Name_Text,
	Info_Measure_Text,
	Info_Info,
	Info_HowTo,
	Info_Max
} info_t_C;

/////////////////////////////////////////////////////////////////////////////
// CMediaInfoDLL

class CMediaInfoDLL
{
public:
	CMediaInfoDLL()
		: m_ullVersion()
		, m_hLib()
		, m_bInitialized()
		, m_pfnMediaInfo4_Open()	// MediaInfoLib - v0.4.0.1
		, m_pfnMediaInfo4_Close()
		, m_pfnMediaInfo4_Get()
		, m_pfnMediaInfo4_Count_Get()
		, m_pfnMediaInfo5_Open()	// MediaInfoLib - v0.5 - v0.6.1
		, m_pfnMediaInfo_Close()	// MediaInfoLib - v0.7+
		, m_pfnMediaInfo_Get()
		, m_pfnMediaInfo_Count_Get()
		, m_pfnMediaInfo_Open()
		, m_pfnMediaInfo_New()
		, m_pfnMediaInfo_Delete()
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
						LPTSTR pszResult = PathCombine(strPath.GetBuffer(MAX_PATH), szPath, _T("MEDIAINFO.DLL"));
						strPath.ReleaseBuffer();
						if (pszResult)
							m_hLib = LoadLibrary(strPath);
					}
				}
			}
			if (m_hLib == NULL) {
				CString strProgramFiles = ShellGetFolderPath(CSIDL_PROGRAM_FILES);
				if (!strProgramFiles.IsEmpty()) {
					LPTSTR pszResult = PathCombine(strPath.GetBuffer(MAX_PATH), strProgramFiles, _T("MediaInfo\\MEDIAINFO.DLL"));
					strPath.ReleaseBuffer();
					if (pszResult)
						m_hLib = LoadLibrary(strPath);
				}
			}
			if (m_hLib != NULL) {
				// Note from MediaInfo developer
				// -----------------------------
				// Note : versioning method, for people who develop with LoadLibrary method
				// - if one of 2 first numbers change, there is no guaranty that the DLL is compatible with old one
				// - if one of 2 last numbers change, there is a guaranty that the DLL is compatible with old one.
				// So you should test the version of the DLL, and if one of the 2 first numbers change, not load it.
				// ---
				ULONGLONG ullVersion = GetModuleVersion(m_hLib);
				if (ullVersion == 0) { // MediaInfoLib - v0.4.0.1 does not have a Win32 version info resource record
					char* (__stdcall * fpMediaInfo4_Info_Version)();
					(FARPROC&)fpMediaInfo4_Info_Version = GetProcAddress(m_hLib, "MediaInfo_Info_Version");
					if (fpMediaInfo4_Info_Version) {
						char* pszVersion = (*fpMediaInfo4_Info_Version)();
						if (pszVersion && strcmp(pszVersion, "MediaInfoLib - v0.4.0.1 - http://mediainfo.sourceforge.net") == 0) {
							(FARPROC&)m_pfnMediaInfo4_Open = GetProcAddress(m_hLib, "MediaInfo_Open");
							(FARPROC&)m_pfnMediaInfo4_Close = GetProcAddress(m_hLib, "MediaInfo_Close");
							(FARPROC&)m_pfnMediaInfo4_Get = GetProcAddress(m_hLib, "MediaInfo_Get");
							(FARPROC&)m_pfnMediaInfo4_Count_Get = GetProcAddress(m_hLib, "MediaInfo_Count_Get");
							if (m_pfnMediaInfo4_Open && m_pfnMediaInfo4_Close && m_pfnMediaInfo4_Get)
								m_ullVersion = MAKEDLLVERULL(0, 4, 0, 1);
						}
					}
				}
				else if (ullVersion >= MAKEDLLVERULL(0, 5, 0, 0) && ullVersion < MAKEDLLVERULL(0, 7, 0, 0)) {
					// eMule currently handles v0.5.1.0, v0.6.0.0, v0.6.1.0
					// Don't use 'MediaInfo_Info_Version' with versions v0.5+. This function is exported,
					// can be called, but does not return a valid version string.

					(FARPROC&)m_pfnMediaInfo5_Open = GetProcAddress(m_hLib, "MediaInfo_Open");
					(FARPROC&)m_pfnMediaInfo_Close = GetProcAddress(m_hLib, "MediaInfo_Close");
					(FARPROC&)m_pfnMediaInfo_Get = GetProcAddress(m_hLib, "MediaInfo_Get");
					(FARPROC&)m_pfnMediaInfo_Count_Get = GetProcAddress(m_hLib, "MediaInfo_Count_Get");
					if (m_pfnMediaInfo5_Open && m_pfnMediaInfo_Close && m_pfnMediaInfo_Get)
						m_ullVersion = ullVersion;
				}
				else if (ullVersion < MAKEDLLVERULL(21, 10, 0, 0)) { //here ullVersion >= 7.0
					(FARPROC&)m_pfnMediaInfo_New = GetProcAddress(m_hLib, "MediaInfo_New");
					(FARPROC&)m_pfnMediaInfo_Delete = GetProcAddress(m_hLib, "MediaInfo_Delete");
					(FARPROC&)m_pfnMediaInfo_Open = GetProcAddress(m_hLib, "MediaInfo_Open");
					(FARPROC&)m_pfnMediaInfo_Close = GetProcAddress(m_hLib, "MediaInfo_Close");
					(FARPROC&)m_pfnMediaInfo_Get = GetProcAddress(m_hLib, "MediaInfo_Get");
					(FARPROC&)m_pfnMediaInfo_Count_Get = GetProcAddress(m_hLib, "MediaInfo_Count_Get");
					if (m_pfnMediaInfo_New && m_pfnMediaInfo_Delete && m_pfnMediaInfo_Open && m_pfnMediaInfo_Close && m_pfnMediaInfo_Get)
						m_ullVersion = ullVersion;
				}
				if (!m_ullVersion) {
					m_pfnMediaInfo4_Open = NULL;
					m_pfnMediaInfo4_Close = NULL;
					m_pfnMediaInfo4_Get = NULL;
					m_pfnMediaInfo4_Count_Get = NULL;
					m_pfnMediaInfo_New = NULL;
					m_pfnMediaInfo_Delete = NULL;
					m_pfnMediaInfo_Open = NULL;
					m_pfnMediaInfo_Close = NULL;
					m_pfnMediaInfo_Get = NULL;
					m_pfnMediaInfo_Count_Get = NULL;
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
		if (m_pfnMediaInfo4_Open)
			return (*m_pfnMediaInfo4_Open)(const_cast<LPSTR>((LPCSTR)CStringA(File)));
		if (m_pfnMediaInfo5_Open)
			return (*m_pfnMediaInfo5_Open)(File);
		if (m_pfnMediaInfo_New) {
			void* Handle = (*m_pfnMediaInfo_New)();
			if (Handle)
				(*m_pfnMediaInfo_Open)(Handle, File);
			return Handle;
		}
		return NULL;
	}

	void Close(void* Handle)
	{
		if (m_pfnMediaInfo_Delete)
			(*m_pfnMediaInfo_Delete)(Handle);	// File is automatically closed
		else if (m_pfnMediaInfo4_Close)
			(*m_pfnMediaInfo4_Close)(Handle);
		else if (m_pfnMediaInfo_Close)
			(*m_pfnMediaInfo_Close)(Handle);
	}

	CString Get(void* Handle, stream_t_C StreamKind, int StreamNumber, LPCTSTR Parameter, info_t_C KindOfInfo, info_t_C KindOfSearch)
	{
		if (m_pfnMediaInfo4_Get)
			return CString((*m_pfnMediaInfo4_Get)(Handle, StreamKind, StreamNumber, (LPSTR)(LPCSTR)CStringA(Parameter), KindOfInfo, KindOfSearch));
		if (m_pfnMediaInfo_Get) {
			CString strNewParameter(Parameter);
			if (m_ullVersion >= MAKEDLLVERULL(0, 7, 1, 0)) {
				// Convert old tags to new tags
				strNewParameter.Replace(_T('_'), _T('/'));

				// Workaround for a bug in MediaInfoLib
				if (strNewParameter == _T("Channels"))
					strNewParameter = _T("Channel(s)");
			}
			return (*m_pfnMediaInfo_Get)(Handle, StreamKind, StreamNumber, strNewParameter, KindOfInfo, KindOfSearch);
		}
		return CString();
	}

	int Count_Get(void* Handle, stream_t_C StreamKind, int StreamNumber) const
	{
		if (m_pfnMediaInfo4_Get)
			return (*m_pfnMediaInfo4_Count_Get)(Handle, StreamKind, StreamNumber);
		if (m_pfnMediaInfo_Count_Get)
			return (*m_pfnMediaInfo_Count_Get)(Handle, StreamKind, StreamNumber);
		return 0;
	}

protected:
	ULONGLONG m_ullVersion;
	HINSTANCE m_hLib;
	bool m_bInitialized;

	// MediaInfoLib - v0.4.0.1
	void* (__stdcall* m_pfnMediaInfo4_Open)(char* File) throw(...);
	void(__stdcall* m_pfnMediaInfo4_Close)(void* Handle) throw(...);
	char* (__stdcall* m_pfnMediaInfo4_Get)(void* Handle, stream_t_C StreamKind, int StreamNumber, char* Parameter, info_t_C KindOfInfo, info_t_C KindOfSearch) throw(...);
	int(__stdcall* m_pfnMediaInfo4_Count_Get)(void* Handle, stream_t_C StreamKind, int StreamNumber) throw(...);

	// MediaInfoLib - v0.5+
	void* (__stdcall* m_pfnMediaInfo5_Open)(const wchar_t* File) throw(...);
	void(__stdcall* m_pfnMediaInfo_Close)(void* Handle) throw(...);
	const wchar_t* (__stdcall* m_pfnMediaInfo_Get)(void* Handle, stream_t_C StreamKind, int StreamNumber, const wchar_t* Parameter, info_t_C KindOfInfo, info_t_C KindOfSearch) throw(...);
	int(__stdcall* m_pfnMediaInfo_Count_Get)(void* Handle, stream_t_C StreamKind, int StreamNumber) throw(...);

	// MediaInfoLib - v0.7.*, 17.*
	int(__stdcall* m_pfnMediaInfo_Open)(void* Handle, const wchar_t* File); //throw(...);
	void* (__stdcall* m_pfnMediaInfo_New)(); // throw(...);
	void(__stdcall* m_pfnMediaInfo_Delete)(void* Handle); // throw(...);
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
	CSimpleArray<CObject*> m_pFiles;
public:
	CFileInfoDialog();   // standard constructor
	virtual	~CFileInfoDialog() = default;
	virtual BOOL OnInitDialog();

	void SetFiles(const CSimpleArray<CObject*> *paFiles)	{ m_paFiles = paFiles; m_bDataChanged = true; }
	void SetReducedDialog()									{ m_bReducedDlg = true; }
	void Localize();

protected:
	const CSimpleArray<CObject*> *m_paFiles;
	bool m_bDataChanged;
	CRichEditCtrlX m_fi;
	bool m_bReducedDlg;
	//CHARFORMAT m_cfDef;
	//CHARFORMAT m_cfBold;
	//CHARFORMAT m_cfRed;

	//bool GetMediaInfo(const CKnownFile *file, SMediaInfo *mi, bool bSingleFile);
	//void AddFileInfo(LPCTSTR pszFmt, ...);

	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support
	virtual BOOL OnSetActive();

	DECLARE_MESSAGE_MAP()
	afx_msg LRESULT OnMediaInfoResult(WPARAM, LPARAM);
	afx_msg LRESULT OnDataChanged(WPARAM, LPARAM);
	afx_msg void OnDestroy();
};