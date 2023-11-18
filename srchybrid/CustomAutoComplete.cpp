//--------------------------------------------------------------------------------------------
//  Name:           CCustomAutoComplete (CCUSTOMAUTOCOMPLETE.H)
//  Type:           Wrapper class
//  Description:    Matches IAutoComplete, IEnumString and the registry (optional) to provide
//					custom auto-complete functionality for EDIT controls - including those in
//					combo boxes - in WTL projects.
//
//  Author:         Klaus H. Probst [kprobst@vbbox.com]
//  URL:            http://www.vbbox.com/
//  Copyright:      This work is copyright � 2002, Klaus H. Probst
//  Usage:          You may use this code as you see fit, provided that you assume all
//                  responsibilities for doing so.
//  Distribution:   Distribute freely as long as you maintain this notice as part of the
//					file header.
//
//
//  Updates:        09-Mai-2003 [bluecow]:
//						- changed original string list code to deal with a LRU list
//						  and auto cleanup of list entries according 'iMaxItemCount'.
//						- split original code into cpp/h file
//						- removed registry stuff
//						- added file stuff
//					15-Jan-2004 [Ornis]:
//						- changed adding strings to replace existing ones on a new position
//
//
//  Notes:
//
//
//  Dependencies:
//
//					The usual ATL/WTL headers for a normal EXE, plus <atlmisc.h>
//
//--------------------------------------------------------------------------------------------
#include "stdafx.h"
#if _MSC_VER<=1800 //vs2013
#include <share.h>
#endif
#include "CustomAutoComplete.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


CCustomAutoComplete::CCustomAutoComplete()
{
	InternalInit();
}

CCustomAutoComplete::CCustomAutoComplete(const CStringArray &p_sItemList)
{
	InternalInit();
	SetList(p_sItemList);
}

CCustomAutoComplete::~CCustomAutoComplete()
{
	if (m_pac)
		m_pac.Release();
}

bool CCustomAutoComplete::Bind(HWND p_hWndEdit, DWORD p_dwOptions, LPCTSTR p_lpszFormatString)
{
	ATLASSERT(::IsWindow(p_hWndEdit));
	if (m_bBound || m_pac)
		return false;

	if (SUCCEEDED(m_pac.CoCreateInstance(CLSID_AutoComplete))) {
		if (p_dwOptions) {
			CComQIPtr<IAutoComplete2> pAC2(m_pac);
			if (pAC2) {
				pAC2->SetOptions(p_dwOptions);
				pAC2.Release();
			}
		}

		if (SUCCEEDED(m_pac->Init(p_hWndEdit, this, NULL, p_lpszFormatString))) {
			m_bBound = true;
			return true;
		}
	}
	return false;
}

void CCustomAutoComplete::Unbind()
{
	if (m_bBound && m_pac) {
		m_pac.Release();
		m_bBound = false;
	}
}

bool CCustomAutoComplete::SetList(const CStringArray &p_sItemList)
{
	ATLASSERT(!p_sItemList.IsEmpty());
	Clear();
	m_asList.Append(p_sItemList);
	return true;
}

int CCustomAutoComplete::FindItem(const CString &rstr)
{
	for (INT_PTR i = m_asList.GetCount(); --i >= 0;)
		if (m_asList[i] == rstr)
			return (int)i;
	return -1;
}

bool CCustomAutoComplete::AddItem(const CString &p_sItem, int iPos)
{
	if (p_sItem.GetLength() > 0) {
		int oldpos = FindItem(p_sItem);
		if (oldpos < 0) {
			if (iPos < 0)
				m_asList.Add(p_sItem);
			else
				m_asList.InsertAt(iPos, p_sItem);
			// use a LRU list
			if (m_asList.GetCount() > m_iMaxItemCount)
				m_asList.SetSize(m_iMaxItemCount);
			return true;
		}
		if (iPos >= 0) {
			m_asList.RemoveAt(oldpos);
			iPos -= static_cast<int>(oldpos < iPos);
			m_asList.InsertAt(iPos, p_sItem);
			return true;
		}
	}
	return false;
}

int CCustomAutoComplete::GetItemCount()
{
	return (int)m_asList.GetCount();
}

bool CCustomAutoComplete::RemoveItem(const CString &p_sItem)
{
	if (!p_sItem.IsEmpty()) {
		int iPos = FindItem(p_sItem);
		if (iPos >= 0) {
			m_asList.RemoveAt(iPos);
			return true;
		}
	}
	return false;
}

bool CCustomAutoComplete::RemoveSelectedItem()
{
	if (!m_bBound || !m_pac)
		return false;

	DWORD dwFlags;
	LPWSTR pwszItem;
	CComQIPtr<IAutoCompleteDropDown> pIAutoCompleteDropDown(m_pac);
	if (!pIAutoCompleteDropDown || FAILED(pIAutoCompleteDropDown->GetDropDownStatus(&dwFlags, &pwszItem)))
		return false;

	bool bRet = (dwFlags == ACDD_VISIBLE && pwszItem && RemoveItem(CString(pwszItem)));
	::CoTaskMemFree(pwszItem);
	return bRet;
}

bool CCustomAutoComplete::Clear()
{
	if (m_asList.IsEmpty())
		return false;
	m_asList.RemoveAll();
	return true;
}

bool CCustomAutoComplete::Disable()
{
	return m_bBound && m_pac && SUCCEEDED(EnDisable(false));
}

bool CCustomAutoComplete::Enable()
{
	return !m_bBound && m_pac && SUCCEEDED(EnDisable(true));
}

const CStringArray& CCustomAutoComplete::GetList() const
{
	return m_asList;
}

//
//	IUnknown implementation
//
STDMETHODIMP_(ULONG) CCustomAutoComplete::AddRef() noexcept
{
	return static_cast<ULONG>(::InterlockedIncrement(&m_nRefCount));
}

STDMETHODIMP_(ULONG) CCustomAutoComplete::Release() noexcept
{
	LONG nCount = ::InterlockedDecrement(&m_nRefCount);
	if (nCount == 0)
		delete this;
	return static_cast<ULONG>(nCount);
}

STDMETHODIMP CCustomAutoComplete::QueryInterface(REFIID iid, LPVOID *ppvObj) noexcept
{
	if (ppvObj == NULL)
		return E_POINTER;

	if (IID_IUnknown == iid)
		*ppvObj = static_cast<IUnknown*>(this);
	else if (IID_IEnumString == iid)
		*ppvObj = static_cast<IEnumString*>(this);
	else
		*ppvObj = NULL;
	if (*ppvObj == NULL)
		return E_NOINTERFACE;

	((LPUNKNOWN)*ppvObj)->AddRef();
	return S_OK;
}

//
//	IEnumString implementation
//
STDMETHODIMP CCustomAutoComplete::Next(ULONG celt, LPOLESTR *rgelt, ULONG *pceltFetched) noexcept
{
	if (!celt)
		celt = 1;
	if (pceltFetched)
		*pceltFetched = 0;
	ULONG i;
	for (i = 0; i < celt; ++i) {
		if (m_nCurrentElement >= (ULONG)m_asList.GetCount())
			break;

		rgelt[i] = (LPWSTR)::CoTaskMemAlloc(sizeof(WCHAR) * ((SIZE_T)m_asList[m_nCurrentElement].GetLength() + 1));
		wcscpy(rgelt[i], (CStringW)m_asList[m_nCurrentElement]);

		if (pceltFetched)
			++(*pceltFetched);

		++m_nCurrentElement;
	}
	return (i == celt) ? S_OK : S_FALSE;
}

STDMETHODIMP CCustomAutoComplete::Skip(ULONG celt) noexcept
{
	m_nCurrentElement += celt;
	if (m_nCurrentElement > (ULONG)m_asList.GetCount())
		m_nCurrentElement = 0;

	return S_OK;
}

STDMETHODIMP CCustomAutoComplete::Reset() noexcept
{
	m_nCurrentElement = 0;
	return S_OK;
}

STDMETHODIMP CCustomAutoComplete::Clone(IEnumString **ppenum) noexcept
{
	if (!ppenum)
		return E_POINTER;

	try {
		CCustomAutoComplete *pnew = new CCustomAutoComplete();
		pnew->AddRef();
		*ppenum = pnew;
	} catch (...) {
		return E_OUTOFMEMORY;
	}
	return S_OK;
}

void CCustomAutoComplete::InternalInit()
{
	m_nCurrentElement = 0;
	m_nRefCount = 0;
	m_iMaxItemCount = 30;
	m_bBound = false;
}

HRESULT CCustomAutoComplete::EnDisable(bool p_bEnable)
{
	ATLASSERT(m_pac);

	HRESULT hr = m_pac->Enable(p_bEnable);
	if (SUCCEEDED(hr))
		m_bBound = p_bEnable;
	return hr;
}

bool CCustomAutoComplete::LoadList(LPCTSTR pszFileName)
{
	FILE *fp = _tfsopen(pszFileName, _T("rb"), _SH_DENYWR);
	if (fp == NULL)
		return false;

	// verify Unicode byte order mark 0xFEFF
	WORD wBOM = fgetwc(fp);
	if (wBOM != u'\xFEFF') {
		fclose(fp);
		return false;
	}

	TCHAR szItem[256];
	while (_fgetts(szItem, _countof(szItem), fp) != NULL)
		AddItem(CString(szItem).Trim(_T(" \r\n")), -1);

	fclose(fp);
	return true;
}

bool CCustomAutoComplete::SaveList(LPCTSTR pszFileName)
{
	FILE *fp = _tfsopen(pszFileName, _T("wb"), _SH_DENYWR);
	if (fp == NULL)
		return false;
	bool ret = (fputwc(u'\xFEFF', fp) != WEOF); // write Unicode byte order mark 0xFEFF
	for (int i = 0; ret && i < m_asList.GetCount(); ++i)
		ret = (_ftprintf(fp, _T("%s\r\n"), (LPCTSTR)m_asList[i]) > 0);
	return fclose(fp) ? false : ret;
}

CString CCustomAutoComplete::GetItem(int pos)
{
	return (pos < m_asList.GetCount()) ? m_asList[pos] : CString();
}