//this file is part of eMule
//Copyright (C)2002-2023 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
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
#include "stdafx.h"
#include "SearchListCtrl.h"
#include "emule.h"
#include "ResizableLib/ResizableSheet.h"
#include "SearchFile.h"
#include "SearchList.h"
#include "emuledlg.h"
#include "MetaDataDlg.h"
#include "CommentDialogLst.h"
#include "SearchDlg.h"
#include "SearchParams.h"
#include "ClosableTabCtrl.h"
#include "PreviewDlg.h"
#include "UpDownClient.h"
#include "ClientList.h"
#include "MemDC.h"
#include "SharedFileList.h"
#include "DownloadQueue.h"
#include "PartFile.h"
#include "KnownFileList.h"
#include "MenuCmds.h"
#include "Opcodes.h"
#include "Packets.h"
#include "WebServices.h"
#include "Log.h"
#include "HighColorTab.hpp"
#include "ListViewWalkerPropertySheet.h"
#include "UserMsgs.h"
#include "SearchDlg.h"
#include "SearchResultsWnd.h"
#include "ServerConnect.h"
#include "server.h"
#include "MediaInfo.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#define COLLAPSE_ONLY	0
#define EXPAND_ONLY		1
#define EXPAND_COLLAPSE	2

#define	TREE_WIDTH		10


//////////////////////////////////////////////////////////////////////////////
// CSearchResultFileDetailSheet

class CSearchResultFileDetailSheet : public CListViewWalkerPropertySheet
{
	DECLARE_DYNAMIC(CSearchResultFileDetailSheet)

	void Localize();
public:
	CSearchResultFileDetailSheet(CTypedPtrList<CPtrList, CSearchFile*> &paFiles, UINT uInvokePage = 0, CListCtrlItemWalk *pListCtrl = NULL);

protected:
	CMetaDataDlg m_wndMetaData;
	CCommentDialogLst m_wndComments;

	UINT m_uInvokePage;
	static LPCTSTR m_pPshStartPage;

	void UpdateTitle();

	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnDestroy();
	afx_msg LRESULT OnDataChanged(WPARAM, LPARAM);
};

LPCTSTR CSearchResultFileDetailSheet::m_pPshStartPage;

IMPLEMENT_DYNAMIC(CSearchResultFileDetailSheet, CListViewWalkerPropertySheet)

BEGIN_MESSAGE_MAP(CSearchResultFileDetailSheet, CListViewWalkerPropertySheet)
	ON_WM_DESTROY()
	ON_MESSAGE(UM_DATA_CHANGED, OnDataChanged)
END_MESSAGE_MAP()

void CSearchResultFileDetailSheet::Localize()
{
	m_wndMetaData.Localize();
	SetTabTitle(IDS_META_DATA, &m_wndMetaData, this);
	m_wndComments.Localize();
	SetTabTitle(IDS_CMT_READALL, &m_wndComments, this);
}

CSearchResultFileDetailSheet::CSearchResultFileDetailSheet(CTypedPtrList<CPtrList, CSearchFile*> &paFiles, UINT uInvokePage, CListCtrlItemWalk *pListCtrl)
	: CListViewWalkerPropertySheet(pListCtrl)
	, m_uInvokePage(uInvokePage)
{
	for (POSITION pos = paFiles.GetHeadPosition(); pos != NULL;)
		m_aItems.Add(paFiles.GetNext(pos));
	m_psh.dwFlags &= ~PSH_HASHELP;
	m_psh.dwFlags |= PSH_NOAPPLYNOW;

	m_wndMetaData.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndMetaData.m_psp.dwFlags |= PSP_USEICONID;
	m_wndMetaData.m_psp.pszIcon = _T("METADATA");
	if (thePrefs.IsExtControlsEnabled() && m_aItems.GetSize() == 1) {
		m_wndMetaData.SetFiles(&m_aItems);
		AddPage(&m_wndMetaData);
	}

	m_wndComments.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndComments.m_psp.dwFlags |= PSP_USEICONID;
	m_wndComments.m_psp.pszIcon = _T("FileComments");
	m_wndComments.SetFiles(&m_aItems);
	AddPage(&m_wndComments);

	LPCTSTR pPshStartPage = m_pPshStartPage;
	if (m_uInvokePage != 0)
		pPshStartPage = MAKEINTRESOURCE(m_uInvokePage);
	for (int i = (int)m_pages.GetSize(); --i >= 0;)
		if (GetPage(i)->m_psp.pszTemplate == pPshStartPage) {
			m_psh.nStartPage = i;
			break;
		}
}

void CSearchResultFileDetailSheet::OnDestroy()
{
	if (m_uInvokePage == 0)
		m_pPshStartPage = GetPage(GetActiveIndex())->m_psp.pszTemplate;
	CListViewWalkerPropertySheet::OnDestroy();
}

BOOL CSearchResultFileDetailSheet::OnInitDialog()
{
	EnableStackedTabs(FALSE);
	BOOL bResult = CListViewWalkerPropertySheet::OnInitDialog();
	HighColorTab::UpdateImageList(*this);
	InitWindowStyles(this);
	EnableSaveRestore(_T("SearchResultFileDetailsSheet")); // call this after(!) OnInitDialog
	Localize();
	UpdateTitle();
	return bResult;
}

LRESULT CSearchResultFileDetailSheet::OnDataChanged(WPARAM, LPARAM)
{
	UpdateTitle();
	return 1;
}

void CSearchResultFileDetailSheet::UpdateTitle()
{
	CString sTitle(GetResString(IDS_DETAILS));
	if (m_aItems.GetSize() == 1)
		sTitle.AppendFormat(_T(": %s"), (LPCTSTR)(static_cast<CSearchFile*>(m_aItems[0])->GetFileName()));
	SetWindowText(sTitle);
}


//////////////////////////////////////////////////////////////////////////////
// CSearchListCtrl

IMPLEMENT_DYNAMIC(CSearchListCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CSearchListCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(LVN_DELETEALLITEMS, OnLvnDeleteAllItems)
	ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnLvnGetDispInfo)
	ON_NOTIFY_REFLECT(LVN_GETINFOTIP, OnLvnGetInfoTip)
	ON_NOTIFY_REFLECT(LVN_KEYDOWN, OnLvnKeyDown)
	ON_NOTIFY_REFLECT(NM_CLICK, OnNmClick)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNmDblClk)
	ON_WM_CONTEXTMENU()
	ON_WM_DESTROY()
	ON_WM_KEYDOWN()
	ON_WM_SYSCOLORCHANGE()
END_MESSAGE_MAP()

CSearchListCtrl::CSearchListCtrl()
	: CListCtrlItemWalk(this)
	, searchlist()
	, m_crSearchResultDownloading()
	, m_crSearchResultDownloadStopped()
	, m_crSearchResultKnown()
	, m_crSearchResultShareing()
	, m_crSearchResultCancelled()
	, m_crShades()
	, m_nResultsID()
{
	SetGeneralPurposeFind(true);
	m_eFileSizeFormat = (EFileSizeFormat)theApp.GetProfileInt(_T("eMule"), _T("SearchResultsFileSizeFormat"), fsizeDefault);
	SetSkinKey(_T("SearchResultsLv"));
}

void CSearchListCtrl::OnDestroy()
{
	theApp.WriteProfileInt(_T("eMule"), _T("SearchResultsFileSizeFormat"), m_eFileSizeFormat);
	__super::OnDestroy();
}

void CSearchListCtrl::SetStyle()
{
	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
}

void CSearchListCtrl::SetAllIcons()
{
	ApplyImageList(NULL);
	m_ImageList.DeleteImageList();
	m_ImageList.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	m_ImageList.Add(CTempIconLoader(_T("EMPTY"))); //0
	m_ImageList.Add(CTempIconLoader(_T("Rating_NotRated"))); //1
	m_ImageList.Add(CTempIconLoader(_T("Rating_Fake"))); //2
	m_ImageList.Add(CTempIconLoader(_T("Rating_Poor"))); //3
	m_ImageList.Add(CTempIconLoader(_T("Rating_Fair"))); //4
	m_ImageList.Add(CTempIconLoader(_T("Rating_Good"))); //5
	m_ImageList.Add(CTempIconLoader(_T("Rating_Excellent"))); //6
	m_ImageList.Add(CTempIconLoader(_T("Collection_Search"))); //7 rating for comments are searched on kad
	m_ImageList.Add(CTempIconLoader(_T("Spam"))); //8 spam indicator
	m_ImageList.SetOverlayImage(m_ImageList.Add(CTempIconLoader(_T("FileCommentsOvl"))), 1);
	// Apply the image list also to the listview control, even if we use our own 'DrawItem'.
	// This is needed to give the listview control a chance to initialize the row height.
	ASSERT((GetStyle() & LVS_SHAREIMAGELISTS) != 0);
	VERIFY(ApplyImageList(m_ImageList) == NULL);

	// NOTE: There is another image list applied to this particular listview control!
	// See also the 'Init' function.
}

void CSearchListCtrl::Init(CSearchList *in_searchlist)
{
	SetPrefsKey(_T("SearchListCtrl"));
	ASSERT((GetStyle() & LVS_SINGLESEL) == 0);
	SetStyle();

	CToolTipCtrl *tooltip = GetToolTips();
	if (tooltip) {
		m_tooltip.SetFileIconToolTip(true);
		m_tooltip.SubclassWindow(*tooltip);
		tooltip->ModifyStyle(0, TTS_NOPREFIX);
		tooltip->SetDelayTime(TTDT_AUTOPOP, SEC2MS(20));
		//tooltip->SetDelayTime(TTDT_INITIAL, SEC2MS(thePrefs.GetToolTipDelay()));
	}
	searchlist = in_searchlist;

	InsertColumn(0,		_T(""),	LVCFMT_LEFT,	DFLT_FILENAME_COL_WIDTH);			//IDS_DL_FILENAME
	InsertColumn(1,		_T(""),	LVCFMT_RIGHT,	DFLT_SIZE_COL_WIDTH);				//IDS_DL_SIZE
	InsertColumn(2,		_T(""),	LVCFMT_RIGHT,	60);								//IDS_SEARCHAVAIL
	InsertColumn(3,		_T(""),	LVCFMT_RIGHT,	70);								//IDS_COMPLSOURCES
	InsertColumn(4,		_T(""),	LVCFMT_LEFT,	DFLT_FILETYPE_COL_WIDTH);			//IDS_TYPE
	InsertColumn(5,		_T(""),	LVCFMT_LEFT,	DFLT_HASH_COL_WIDTH, -1, true);		//IDS_FILEID
	InsertColumn(6,		_T(""),	LVCFMT_LEFT,	DFLT_ARTIST_COL_WIDTH);				//IDS_ARTIST
	InsertColumn(7,		_T(""),	LVCFMT_LEFT,	DFLT_ALBUM_COL_WIDTH);				//IDS_ALBUM
	InsertColumn(8,		_T(""),	LVCFMT_LEFT,	DFLT_TITLE_COL_WIDTH);				//IDS_TITLE
	InsertColumn(9,		_T(""),	LVCFMT_RIGHT,	DFLT_LENGTH_COL_WIDTH);				//IDS_LENGTH
	InsertColumn(10,	_T(""),	LVCFMT_RIGHT,	DFLT_BITRATE_COL_WIDTH);			//IDS_BITRATE
	InsertColumn(11,	_T(""),	LVCFMT_LEFT,	DFLT_CODEC_COL_WIDTH);				//IDS_CODEC
	InsertColumn(12,	_T(""),	LVCFMT_LEFT,	DFLT_FOLDER_COL_WIDTH, -1, true);	//IDS_FOLDER
	InsertColumn(13,	_T(""),	LVCFMT_LEFT,	50);								//IDS_KNOWN
	InsertColumn(14,	_T(""),	LVCFMT_LEFT,	DFLT_HASH_COL_WIDTH, -1, true);		//IDS_AICHHASH

	SetAllIcons();

	// This states image list with that particular width is only there to let the listview control
	// auto-size the column width properly (double clicking on header divider). The items in the
	// list view contain a file type icon and optionally also a 'tree' icon (in case there are
	// more search entries related to one file hash). The width of that 'tree' icon (even if it is
	// not drawn) has to be known by the default list view control code to determine the total width
	// needed to show a particular item. The image list itself can be even empty, it is used by
	// the listview control just for querying the width of on image in the list, even if that image
	// was never added.
	CImageList imlDummyStates;
	imlDummyStates.Create(TREE_WIDTH, 16, ILC_COLOR, 0, 0);
	CImageList *pOldStates = SetImageList(&imlDummyStates, LVSIL_STATE);
	imlDummyStates.Detach();
	if (pOldStates)
		pOldStates->DeleteImageList();

	CreateMenus();

	LoadSettings();
	SetHighlightColors();

	// Barry - Use preferred sort order from preferences
	if (GetSortItem() != -1) {// don't force sorting if '-1' is specified;  we can see better how the search results are arriving
		SetSortArrow();
		SortItems(SortProc, MAKELONG(GetSortItem(), !GetSortAscending()));
	}
}

CSearchListCtrl::~CSearchListCtrl()
{
	for (POSITION pos = m_mapSortSelectionStates.GetStartPosition(); pos != NULL;) {
		int nKey;
		CSortSelectionState *pValue;
		m_mapSortSelectionStates.GetNextAssoc(pos, nKey, pValue);
		delete pValue;
	}
}

void CSearchListCtrl::Localize()
{
	static const UINT uids[15] =
	{
		IDS_DL_FILENAME, IDS_DL_SIZE, 0/*IDS_SEARCHAVAIL*/, IDS_COMPLSOURCES, IDS_TYPE
		, IDS_FILEID, IDS_ARTIST, IDS_ALBUM, IDS_TITLE, IDS_LENGTH
		, IDS_BITRATE, IDS_CODEC, IDS_FOLDER, IDS_KNOWN, IDS_AICHHASH
	};

	LocaliseHeaderCtrl(uids, _countof(uids));

	HDITEM hdi;
	hdi.mask = HDI_TEXT;
	CString strRes(GetResString(IDS_SEARCHAVAIL));
	if (thePrefs.IsExtControlsEnabled())
		strRes.AppendFormat(_T(" (%s)"), (LPCTSTR)GetResString(IDS_DL_SOURCES)); //modify "availability" header
	hdi.pszText = const_cast<LPTSTR>((LPCTSTR)strRes);
	GetHeaderCtrl()->SetItem(2, &hdi);

	CreateMenus();
}

void CSearchListCtrl::AddResult(const CSearchFile *toshow)
{
	bool bFilterActive = !theApp.emuledlg->searchwnd->m_pwndResults->m_astrFilter.IsEmpty();
	bool bItemFiltered = bFilterActive && IsFilteredOut(toshow);

	// update tab-counter for the given searchfile
	CClosableTabCtrl &searchselect = theApp.emuledlg->searchwnd->GetSearchSelector();
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	for (int iItem = searchselect.GetItemCount(); --iItem >= 0;)
		if (searchselect.GetItem(iItem, &ti) && ti.lParam != NULL) {
			const SSearchParams* pSearchParams = reinterpret_cast<SSearchParams*>(ti.lParam);
			if (pSearchParams->dwSearchID == toshow->GetSearchID()) {
				UINT iAvailResults = searchlist->GetFoundFiles(toshow->GetSearchID());
				CString strTabLabel(pSearchParams->strSearchTitle);
				if (bFilterActive) {
					int iFilteredResult = GetItemCount() + static_cast<int>(!bItemFiltered);
					strTabLabel.AppendFormat(_T(" (%i/%u)"), iFilteredResult, iAvailResults);
				} else
					strTabLabel.AppendFormat(_T(" (%u)"), iAvailResults);
				DupAmpersand(strTabLabel);
				ti.pszText = const_cast<LPTSTR>((LPCTSTR)strTabLabel);
				ti.mask = TCIF_TEXT;
				searchselect.SetItem(iItem, &ti);
				if (searchselect.GetCurSel() != iItem)
					searchselect.HighlightItem(iItem);
				break;
			}
		}

	if (bItemFiltered || toshow->GetSearchID() != m_nResultsID)
		return;

	// Turn off updates
	EUpdateMode eCurUpdateMode = SetUpdateMode(none);
	// Add item
	int iItem = InsertItem(LVIF_TEXT | LVIF_PARAM, GetItemCount(), toshow->GetFileName(), 0, 0, 0, (LPARAM)toshow);
	// Add all sub items as callbacks and restore updating with last sub item.
	// The callbacks are only needed for 'Find' functionality, not for any drawing.
	const int iSubItems = 13;
	for (int i = 1; i <= iSubItems; ++i) {
		if (i == iSubItems)
			SetUpdateMode(eCurUpdateMode);
		SetItemText(iItem, i, LPSTR_TEXTCALLBACK);
	}
}

void CSearchListCtrl::UpdateSources(const CSearchFile *toupdate)
{
	LVFINDINFO find;
	find.flags = LVFI_PARAM;
	find.lParam = (LPARAM)toupdate;
	int iItem = FindItem(&find);
	if (iItem >= 0) {
		uint32 nSources = toupdate->GetSourceCount();
		int iClients = toupdate->GetClientsCount();
		CString strBuffer;
		if (thePrefs.IsExtControlsEnabled() && iClients > 0)
			strBuffer.Format(_T("%u (%i)"), nSources, iClients);
		else
			strBuffer.Format(_T("%u"), nSources);
		SetItemText(iItem, 2, strBuffer);
		SetItemText(iItem, 3, GetCompleteSourcesDisplayString(toupdate, nSources));

		if (toupdate->IsListExpanded()) {
			const SearchList *list = theApp.searchlist->GetSearchListForID(toupdate->GetSearchID());
			for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
				const CSearchFile *cur_file = list->GetNext(pos);
				if (cur_file->GetListParent() == toupdate) {
					LVFINDINFO find1;
					find1.flags = LVFI_PARAM;
					find1.lParam = (LPARAM)cur_file;
					int index = FindItem(&find1);
					if (index >= 0)
						Update(index);
					else
						InsertItem(LVIF_PARAM | LVIF_TEXT, iItem + 1, cur_file->GetFileName(), 0, 0, 0, (LPARAM)cur_file);
				}
			}
		}
		Update(iItem);
	}
}

void CSearchListCtrl::UpdateSearch(CSearchFile *toupdate)
{
	if (toupdate && !theApp.IsClosing()) {
		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)toupdate;
		int iItem = FindItem(&find);
		if (iItem >= 0)
			Update(iItem);
	}
}

bool CSearchListCtrl::IsComplete(const CSearchFile *pFile, UINT uSources) const
{
	int iComplete = pFile->IsComplete(uSources, pFile->GetCompleteSourceCount());

	if (iComplete < 0)			// '< 0' ... unknown
		return true;			// treat 'unknown' as complete

	if (iComplete > 0)			// '> 0' ... we know it's complete
		return true;

	return false;				// '= 0' ... we know it's not complete
}

CString CSearchListCtrl::GetCompleteSourcesDisplayString(const CSearchFile *pFile, UINT uSources, bool *pbComplete) const
{
	UINT uCompleteSources = pFile->GetCompleteSourceCount();
	int iComplete = pFile->IsComplete(uSources, uCompleteSources);

	// If we have no 'Complete' info at all but the file size is <= PARTSIZE,
	// though we know that the file is complete (otherwise it would not be shared).
	if (iComplete < 0 && (uint64)pFile->GetFileSize() <= PARTSIZE) {
		iComplete = 1;
		// If this search result is from a remote client's shared file list, we know the 'complete' count.
		if (pFile->GetDirectory() != NULL)
			uCompleteSources = 1;
	}

	CString str;
	if (iComplete < 0) {		// '< 0' ... unknown
		str += _T('?');
		if (pbComplete)
			*pbComplete = true;	// treat 'unknown' as complete
	} else if (iComplete > 0) {	// '> 0' ... we know it's complete
		if (uSources && uCompleteSources) {
			str.Format(_T("%u%%"), (uCompleteSources * 100) / uSources);
			if (thePrefs.IsExtControlsEnabled())
				str.AppendFormat(_T(" (%u)"), uCompleteSources);
		} else {
			// we know it's complete, but we don't know the degree. (for files <= PARTSIZE in Kad searches)
			str = GetResString(IDS_YES);
		}
		if (pbComplete)
			*pbComplete = true;
	} else {					// '= 0' ... we know it's not complete
		str = _T("0%");
		if (thePrefs.IsExtControlsEnabled())
			str.AppendFormat(_T(" (0)"));
		if (pbComplete)
			*pbComplete = false;
	}
	return str;
}

void CSearchListCtrl::RemoveResult(const CSearchFile *toremove)
{
	LVFINDINFO find;
	find.flags = LVFI_PARAM;
	find.lParam = (LPARAM)toremove;
	int iItem = FindItem(&find);
	if (iItem >= 0)
		DeleteItem(iItem);
}

void CSearchListCtrl::ShowResults(uint32 nResultsID)
{
	if (m_nResultsID != 0 && nResultsID != m_nResultsID) {
		// store the current state
		CSortSelectionState *pCurState = new CSortSelectionState();
		for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;)
			pCurState->m_aSelectedItems.Add(GetNextSelectedItem(pos));

		pCurState->m_nSortItem = GetSortItem();
		pCurState->m_bSortAscending = GetSortAscending();
		pCurState->m_nScrollPosition = GetTopIndex();
		m_mapSortSelectionStates[m_nResultsID] = pCurState;
	}

	DeleteAllItems();

	// recover stored state
	CSortSelectionState *pNewState;
	if (nResultsID != 0 && nResultsID != m_nResultsID && m_mapSortSelectionStates.Lookup(nResultsID, pNewState)) {
		m_mapSortSelectionStates.RemoveKey(nResultsID);

		// sort order
		SetSortArrow(pNewState->m_nSortItem, pNewState->m_bSortAscending);
		SortItems(SortProc, MAKELONG(pNewState->m_nSortItem, !pNewState->m_bSortAscending));
		// fill in the items
		m_nResultsID = nResultsID;
		searchlist->ShowResults(m_nResultsID);
		// set stored selectionstates
		for (int i = (int)pNewState->m_aSelectedItems.GetCount(); --i >= 0;)
			SetItemState(pNewState->m_aSelectedItems[i], LVIS_SELECTED, LVIS_SELECTED);

		if (pNewState->m_nScrollPosition > 0) {
			POINT Point;
			GetItemPosition(pNewState->m_nScrollPosition - 1, &Point);
			Point.x = 0;
			Scroll((CSize)Point);
		}
		delete pNewState;
	} else {
		m_nResultsID = nResultsID;
		searchlist->ShowResults(m_nResultsID);
	}
}

void CSearchListCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	bool sortAscending;
	if (GetSortItem() != pNMLV->iSubItem)
		switch (pNMLV->iSubItem) {
		case 2: // Availability
		case 3: // Complete Sources
			sortAscending = false;
			break;
		default:
			sortAscending = true;
		}
	else
		sortAscending = !GetSortAscending();

	// Sort table
	UpdateSortHistory(MAKELONG(pNMLV->iSubItem, !sortAscending));
	SetSortArrow(pNMLV->iSubItem, sortAscending);
	SortItems(SortProc, MAKELONG(pNMLV->iSubItem, !sortAscending));
	*pResult = 0;
}

int CALLBACK CSearchListCtrl::SortProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	const CSearchFile *item1 = reinterpret_cast<CSearchFile*>(lParam1);
	const CSearchFile *item2 = reinterpret_cast<CSearchFile*>(lParam2);
	bool bDirect = !HIWORD(lParamSort);

	int iResult;
	if (item1->GetListParent() == NULL && item2->GetListParent() != NULL) {
		if (item1 == item2->GetListParent())
			return -1;
		iResult = Compare(item1, item2->m_list_parent, lParamSort, bDirect);
		if (!bDirect)
			iResult = -iResult;
	} else if (item2->GetListParent() == NULL && item1->GetListParent() != NULL) {
		if (item1->m_list_parent == item2)
			return 1;
		iResult = Compare(item1->GetListParent(), item2, lParamSort, bDirect);
		if (!bDirect)
			iResult = -iResult;
	} else if (item1->GetListParent() == NULL) {
		iResult = Compare(item1, item2, lParamSort, bDirect);
		if (!bDirect)
			iResult = -iResult;
	} else {
		iResult = Compare(item1->GetListParent(), item2->GetListParent(), lParamSort, bDirect);
		if (iResult != 0)
			return bDirect ? iResult : -iResult;

		if ((item1->GetListParent() == NULL && item2->GetListParent() != NULL) || (item2->GetListParent() == NULL && item1->GetListParent() != NULL))
			return item1->GetListParent() ? 1 : -1;
		iResult = CompareChild(item1, item2, lParamSort);
	}

	//call secondary sort order, if the first one resulted as equal
	if (iResult == 0) {
		LPARAM iNextSort = theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.GetNextSortOrder(lParamSort);
		if (iNextSort != -1)
			iResult = SortProc(lParam1, lParam2, iNextSort);
	}

	return iResult;
}

int CSearchListCtrl::CompareChild(const CSearchFile *item1, const CSearchFile *item2, LPARAM lParamSort)
{
	int iResult;
	switch (LOWORD(lParamSort)) {
	case 0:	//filename
		iResult = CompareLocaleStringNoCase(item1->GetFileName(), item2->GetFileName());
		break;
	case 14: // AICH Hash
		iResult = CompareAICHHash(item1->GetFileIdentifierC(), item2->GetFileIdentifierC(), true);
		break;
	default: // always sort by descending availability
		iResult = -CompareUnsigned(item1->GetSourceCount(), item2->GetSourceCount());
	}
	return HIWORD(lParamSort) ? -iResult : iResult;
}

int CSearchListCtrl::Compare(const CSearchFile *item1, const CSearchFile *item2, LPARAM lParamSort, bool bSortAscending)
{
	if (thePrefs.IsSearchSpamFilterEnabled()) {
		// files marked as spam are always put to the bottom of the list (maybe as option later)
		if (item1->IsConsideredSpam() ^ item2->IsConsideredSpam()) {
			if (bSortAscending)
				return item1->IsConsideredSpam() ? 1 : -1;
			return item1->IsConsideredSpam() ? -1 : 1;
		}
	}

	switch (LOWORD(lParamSort)) {
	case 0: //filename asc
		return CompareLocaleStringNoCase(item1->GetFileName(), item2->GetFileName());
	case 1: //size asc
		return CompareUnsigned(item1->GetFileSize(), item2->GetFileSize());
	case 2: //sources asc
		return CompareUnsigned(item1->GetSourceCount(), item2->GetSourceCount());
	case 3: // complete sources asc
		if (item1->GetSourceCount() == 0 || item2->GetSourceCount() == 0 || item1->IsKademlia() || item2->IsKademlia())
			return 0; // should never happen, just a sanity check
		return CompareUnsigned((item1->GetCompleteSourceCount() * 100) / item1->GetSourceCount(), (item2->GetCompleteSourceCount() * 100) / item2->GetSourceCount());
	case 4: //type asc
		{
			int iResult = item1->GetFileTypeDisplayStr().Compare(item2->GetFileTypeDisplayStr());
			if (iResult)
				return iResult;
			// the types are equal, sub-sort by extension
			LPCTSTR pszExt1 = ::PathFindExtension(item1->GetFileName());
			LPCTSTR pszExt2 = ::PathFindExtension(item2->GetFileName());
			if (!*pszExt1 ^ !*pszExt2)
				return *pszExt1 ? -1 : 1;
			return  *pszExt1 ? _tcsicmp(pszExt1, pszExt2) : 0;
		}
	case 5: //file hash asc
		return memcmp(item1->GetFileHash(), item2->GetFileHash(), 16);
	case 6:
		return CompareOptLocaleStringNoCaseUndefinedAtBottom(item1->GetStrTagValue(FT_MEDIA_ARTIST), item2->GetStrTagValue(FT_MEDIA_ARTIST), bSortAscending);
	case 7:
		return CompareOptLocaleStringNoCaseUndefinedAtBottom(item1->GetStrTagValue(FT_MEDIA_ALBUM), item2->GetStrTagValue(FT_MEDIA_ALBUM), bSortAscending);
	case 8:
		return CompareOptLocaleStringNoCaseUndefinedAtBottom(item1->GetStrTagValue(FT_MEDIA_TITLE), item2->GetStrTagValue(FT_MEDIA_TITLE), bSortAscending);
	case 9:
		return CompareUnsignedUndefinedAtBottom(item1->GetIntTagValue(FT_MEDIA_LENGTH), item2->GetIntTagValue(FT_MEDIA_LENGTH), bSortAscending);
	case 10:
		return CompareUnsignedUndefinedAtBottom(item1->GetIntTagValue(FT_MEDIA_BITRATE), item2->GetIntTagValue(FT_MEDIA_BITRATE), bSortAscending);
	case 11:
		return CompareOptLocaleStringNoCaseUndefinedAtBottom(GetCodecDisplayName(item1->GetStrTagValue(FT_MEDIA_CODEC)), GetCodecDisplayName(item2->GetStrTagValue(FT_MEDIA_CODEC)), bSortAscending);
	case 12: //path asc
		return CompareOptLocaleStringNoCaseUndefinedAtBottom(item1->GetDirectory(), item2->GetDirectory(), bSortAscending);
	case 13:
		return item1->GetKnownType() - item2->GetKnownType();
	case 14:
		return CompareAICHHash(item1->GetFileIdentifierC(), item2->GetFileIdentifierC(), bSortAscending);
	}
	return 0;
}

void CSearchListCtrl::OnContextMenu(CWnd*, CPoint point)
{
	int iSelected = 0;
	int iToDownload = 0;
	int iToPreview = 0;
	bool bContainsNotSpamFile = false;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		const CSearchFile *pFile = reinterpret_cast<CSearchFile*>(GetItemData(GetNextSelectedItem(pos)));
		if (pFile) {
			++iSelected;
			iToPreview += static_cast<int>(pFile->IsPreviewPossible());
			iToDownload += static_cast<int>(!theApp.downloadqueue->IsFileExisting(pFile->GetFileHash(), false));
			if (!pFile->IsConsideredSpam())
				bContainsNotSpamFile = true;
		}
	}

	m_SearchFileMenu.EnableMenuItem(MP_RESUME, iToDownload > 0 ? MF_ENABLED : MF_GRAYED);
	if (thePrefs.IsExtControlsEnabled()) {
		m_SearchFileMenu.EnableMenuItem(MP_RESUMEPAUSED, iToDownload > 0 ? MF_ENABLED : MF_GRAYED);
		m_SearchFileMenu.EnableMenuItem(MP_DETAIL, iSelected == 1 ? MF_ENABLED : MF_GRAYED);
	}
	m_SearchFileMenu.EnableMenuItem(MP_CMT, iSelected > 0 ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_GETED2KLINK, iSelected > 0 ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_GETHTMLED2KLINK, iSelected > 0 ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_REMOVESELECTED, iSelected > 0 ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_REMOVE, theApp.emuledlg->searchwnd->CanDeleteSearch() ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_REMOVEALL, theApp.emuledlg->searchwnd->CanDeleteAllSearches() ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_SEARCHRELATED, iSelected > 0 && theApp.emuledlg->searchwnd->CanSearchRelatedFiles() ? MF_ENABLED : MF_GRAYED);
	UINT uInsertedMenuItem = 0;
	if (iToPreview == 1) {
		if (m_SearchFileMenu.InsertMenu(MP_FIND, MF_STRING | MF_ENABLED, MP_PREVIEW, GetResString(IDS_DL_PREVIEW), _T("Preview")))
			uInsertedMenuItem = MP_PREVIEW;
	}
	m_SearchFileMenu.EnableMenuItem(MP_FIND, GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED);

	UINT uInsertedMenuItem2 = 0;
	if (thePrefs.IsSearchSpamFilterEnabled() && m_SearchFileMenu.InsertMenu(MP_REMOVESELECTED, MF_STRING | MF_ENABLED, MP_MARKASSPAM, (bContainsNotSpamFile || iSelected == 0) ? GetResString(IDS_MARKSPAM) : GetResString(IDS_MARKNOTSPAM), _T("Spam"))) {
		uInsertedMenuItem2 = MP_MARKASSPAM;
		m_SearchFileMenu.EnableMenuItem(MP_MARKASSPAM, iSelected > 0 ? MF_ENABLED : MF_GRAYED);
	}
	CTitleMenu WebMenu;
	WebMenu.CreateMenu();
	WebMenu.AddMenuTitle(NULL, true);
	int iWebMenuEntries = theWebServices.GetFileMenuEntries(&WebMenu);
	UINT flag2 = (iWebMenuEntries == 0 || iSelected != 1) ? MF_GRAYED : MF_STRING;
	m_SearchFileMenu.AppendMenu(MF_POPUP | flag2, (UINT_PTR)WebMenu.m_hMenu, GetResString(IDS_WEBSERVICES), _T("WEB"));

	if (iToDownload > 0)
		m_SearchFileMenu.SetDefaultItem((!thePrefs.AddNewFilesPaused() || !thePrefs.IsExtControlsEnabled()) ? MP_RESUME : MP_RESUMEPAUSED);
	else
		m_SearchFileMenu.SetDefaultItem(UINT_MAX);

	GetPopupMenuPos(*this, point);
	m_SearchFileMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
	if (uInsertedMenuItem)
		VERIFY(m_SearchFileMenu.RemoveMenu(uInsertedMenuItem, MF_BYCOMMAND));
	if (uInsertedMenuItem2)
		VERIFY(m_SearchFileMenu.RemoveMenu(uInsertedMenuItem2, MF_BYCOMMAND));
	m_SearchFileMenu.RemoveMenu(m_SearchFileMenu.GetMenuItemCount() - 1, MF_BYPOSITION);
	VERIFY(WebMenu.DestroyMenu());
}

BOOL CSearchListCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	wParam = LOWORD(wParam);

	if (wParam == MP_FIND) {
		OnFindStart();
		return TRUE;
	}

	CTypedPtrList<CPtrList, CSearchFile*> selectedList;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		int index = GetNextSelectedItem(pos);
		if (index >= 0)
			selectedList.AddTail(reinterpret_cast<CSearchFile*>(GetItemData(index)));
	}

	if (!selectedList.IsEmpty()) {
		CSearchFile *file = selectedList.GetHead();

		switch (wParam) {
		case MP_GETED2KLINK:
			{
				CWaitCursor curWait;
				CString clpbrd;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					if (file) {
						if (!clpbrd.IsEmpty())
							clpbrd += _T("\r\n");
						clpbrd += file->GetED2kLink();
					}
				}
				theApp.CopyTextToClipboard(clpbrd);
			}
			return TRUE;
		case MP_GETHTMLED2KLINK:
			{
				CWaitCursor curWait;
				CString clpbrd;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					if (file) {
						if (!clpbrd.IsEmpty())
							clpbrd += _T("<br>\r\n");
						clpbrd += file->GetED2kLink(false, true);
					}
				}
				theApp.CopyTextToClipboard(clpbrd);
			}
			return TRUE;
		case MP_RESUME:
			if (thePrefs.IsExtControlsEnabled())
				theApp.emuledlg->searchwnd->DownloadSelected(false);
			else
				theApp.emuledlg->searchwnd->DownloadSelected();
			return TRUE;
		case MP_RESUMEPAUSED:
			theApp.emuledlg->searchwnd->DownloadSelected(true);
			return TRUE;
		case IDA_ENTER:
			theApp.emuledlg->searchwnd->DownloadSelected();
			return TRUE;
		case MP_REMOVESELECTED:
		case MPG_DELETE:
			{
				CWaitCursor curWait;
				SetRedraw(false);
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					HideSources(file);
					theApp.searchlist->RemoveResult(file);
				}
				AutoSelectItem();
				SetRedraw(true);
			}
			return TRUE;
		case MP_DETAIL:
		case MPG_ALTENTER:
		case MP_CMT:
			{
				CSearchResultFileDetailSheet sheet(selectedList, (wParam == MP_CMT ? IDD_COMMENTLST : 0), this);
				sheet.DoModal();
			}
			return TRUE;
		case MP_PREVIEW:
			if (file) {
				if (file->GetPreviews().GetSize() > 0) {
					// already have previews
					(new PreviewDlg())->SetFile(file);
				} else {
					CUpDownClient *newclient = new CUpDownClient(NULL, file->GetClientPort(), file->GetClientID(), file->GetClientServerIP(), file->GetClientServerPort(), true);
					if (!theApp.clientlist->AttachToAlreadyKnown(&newclient, NULL))
						theApp.clientlist->AddClient(newclient);

					newclient->SendPreviewRequest(*file);
					// add to res - later
					AddLogLine(true, _T("Preview Requested - Please wait"));
				}
			}
			return TRUE;
		case MP_SEARCHRELATED:
			// just a shortcut for the user typing into the search field "related::[filehash]"
			theApp.emuledlg->searchwnd->SearchRelatedFiles(selectedList);
			return TRUE;
		case MP_MARKASSPAM:
			{
				CWaitCursor curWait;
				SetRedraw(false);
				bool bContainsNotSpamFile = false;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					if (!file->IsConsideredSpam()) {
						bContainsNotSpamFile = true;
						break;
					}
				}
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					if (file->IsConsideredSpam()) {
						if (!bContainsNotSpamFile)
							theApp.searchlist->MarkFileAsNotSpam(file, false, true);
					} else if (bContainsNotSpamFile)
						theApp.searchlist->MarkFileAsSpam(file, false, true);
				}
				theApp.searchlist->RecalculateSpamRatings(file->GetSearchID(), bContainsNotSpamFile, !bContainsNotSpamFile, true);
				SetRedraw(true);
			}
			return TRUE;
		default:
			if (wParam >= MP_WEBURL && wParam <= MP_WEBURL + 256) {
				theWebServices.RunURL(file, (UINT)wParam);
				return TRUE;
			}
		}
	}
	switch (wParam) {
	case MP_REMOVEALL:
		{
			CWaitCursor curWait;
			theApp.emuledlg->searchwnd->DeleteAllSearches();
		}
		break;
	case MP_REMOVE:
		{
			CWaitCursor curWait;
			theApp.emuledlg->searchwnd->DeleteSearch(m_nResultsID);
		}
	}

	return FALSE;
}

void CSearchListCtrl::OnLvnDeleteAllItems(LPNMHDR, LRESULT *pResult)
{
	// To suppress subsequent LVN_DELETEITEM notification messages, return TRUE.
	*pResult = TRUE;
}

void CSearchListCtrl::CreateMenus()
{
	if (m_SearchFileMenu)
		VERIFY(m_SearchFileMenu.DestroyMenu());

	m_SearchFileMenu.CreatePopupMenu();
	m_SearchFileMenu.AddMenuTitle(GetResString(IDS_FILE), true);
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_RESUME, GetResString(IDS_DOWNLOAD), _T("Resume"));
	if (thePrefs.IsExtControlsEnabled()) {
		CString sResumePaused(GetResString(IDS_DOWNLOAD));
		sResumePaused.AppendFormat(_T(" (%s)"), (LPCTSTR)GetResString(IDS_PAUSED));
		m_SearchFileMenu.AppendMenu(MF_STRING, MP_RESUMEPAUSED, sResumePaused);
	}
	if (thePrefs.IsExtControlsEnabled())
		m_SearchFileMenu.AppendMenu(MF_STRING, MP_DETAIL, GetResString(IDS_SHOWDETAILS), _T("FileInfo"));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_CMT, GetResString(IDS_CMT_ADD), _T("FILECOMMENTS"));
	m_SearchFileMenu.AppendMenu(MF_SEPARATOR);
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_GETED2KLINK, GetResString(IDS_DL_LINK1), _T("ED2KLink"));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_GETHTMLED2KLINK, GetResString(IDS_DL_LINK2), _T("ED2KLink"));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_REMOVESELECTED, GetResString(IDS_REMOVESELECTED), _T("DeleteSelected"));
	//m_SearchFileMenu.AppendMenu(MF_STRING, MP_MARKASSPAM, GetResString(IDS_MARKSPAM), _T("Spam"));
	m_SearchFileMenu.AppendMenu(MF_SEPARATOR);
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_REMOVE, GetResString(IDS_REMOVESEARCHSTRING), _T("Delete"));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_REMOVEALL, GetResString(IDS_REMOVEALLSEARCH), _T("ClearComplete"));
	m_SearchFileMenu.AppendMenu(MF_SEPARATOR);
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_FIND, GetResString(IDS_FIND), _T("Search"));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_SEARCHRELATED, GetResString(IDS_SEARCHRELATED), _T("KadFileSearch"));
}

void CSearchListCtrl::OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult)
{
	LPNMLVGETINFOTIP pGetInfoTip = reinterpret_cast<LPNMLVGETINFOTIP>(pNMHDR);
	LVHITTESTINFO hti;
	if (pGetInfoTip->iSubItem == 0 && ::GetCursorPos(&hti.pt)) {
		ScreenToClient(&hti.pt);
		bool bOverMainItem = (SubItemHitTest(&hti) != -1 && hti.iItem == pGetInfoTip->iItem && hti.iSubItem == 0);

		// those tooltips are very nice for debugging/testing but pretty annoying for general usage
		// enable tooltips only if Ctrl is currently pressed
		bool bShowInfoTip = bOverMainItem && (GetSelectedCount() > 1 || GetKeyState(VK_CONTROL) < 0);
		if (bShowInfoTip && GetSelectedCount() > 1) {
			// Don't show the tooltip if the mouse cursor is not over at least one of the selected items
			bool bInfoTipItemIsPartOfMultiSelection = false;
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				if (GetNextSelectedItem(pos) == pGetInfoTip->iItem) {
					bInfoTipItemIsPartOfMultiSelection = true;
					break;
				}
			}
			if (!bInfoTipItemIsPartOfMultiSelection)
				bShowInfoTip = false;
		}

		if (!bShowInfoTip) {
			if (!bOverMainItem) {
				// don't show the default label tip for the main item, if the mouse is not over the main item
				if ((pGetInfoTip->dwFlags & LVGIT_UNFOLDED) == 0 && pGetInfoTip->cchTextMax > 0 && pGetInfoTip->pszText)
					pGetInfoTip->pszText[0] = _T('\0');
			}
			return;
		}

		if (GetSelectedCount() <= 1) {
			const CSearchFile *file = (CSearchFile*)GetItemData(pGetInfoTip->iItem);
			if (file && pGetInfoTip->pszText && pGetInfoTip->cchTextMax > 0) {
				CString strInfo;
				CString strHead(file->GetFileName());
				strHead.AppendFormat(_T("\n") _T("%s %s\n") _T("%s %s\n<br_head>\n")
					, (LPCTSTR)GetResString(IDS_FD_HASH), (LPCTSTR)md4str(file->GetFileHash())
					, (LPCTSTR)GetResString(IDS_FD_SIZE), (LPCTSTR)CastItoXBytes((uint64)file->GetFileSize()));

				const CArray<CTag*, CTag*> &tags = file->GetTags();
				for (int i = 0; i < tags.GetCount(); ++i) {
					const CTag *tag = tags[i];
					if (tag) {
						CString strTag;
						switch (tag->GetNameID()) {
						/*case FT_FILENAME:
							strTag.Format(_T("%s: %s"), (LPCTSTR)GetResString(IDS_SW_NAME), (LPCTSTR)tag->GetStr());
							break;
						case FT_FILESIZE:
							strTag.Format(_T("%s: %s"), (LPCTSTR)GetResString(IDS_DL_SIZE), FormatFileSize(tag->GetInt64()));
							break;*/
						case FT_FILETYPE:
							strTag.Format(_T("%s: %s"), (LPCTSTR)GetResString(IDS_TYPE), (LPCTSTR)tag->GetStr());
							break;
						case FT_FILEFORMAT:
							strTag.Format(_T("%s: %s"), (LPCTSTR)GetResString(IDS_SEARCHEXTENTION), (LPCTSTR)tag->GetStr());
							break;
						case FT_SOURCES:
							strTag.Format(_T("%s: %u"), (LPCTSTR)GetResString(IDS_SEARCHAVAIL), file->GetSourceCount());
							break;
						case 0x13: // remote client's upload file priority (tested with Hybrid 0.47)
							{
								strTag.Format(_T("%s: "), (LPCTSTR)GetResString(IDS_PRIORITY));
								UINT uid = 0;
								switch ((int)tag->GetInt()) {
								case 0:
									uid = IDS_PRIONORMAL;
									break;
								case 2:
									uid = IDS_PRIOHIGH;
									break;
								case -2:
									uid = IDS_PRIOLOW;
									break;
#ifdef _DEBUG
								default:
									strTag.AppendFormat(_T("%u (***Unknown***)"), tag->GetInt());
#endif
								}
								if (uid)
									strTag += GetResString(IDS_PRIORITY);
							}
							break;
						default:
							{
								bool bSkipTag = false;
								if (tag->GetNameID() == FT_FILENAME || tag->GetNameID() == FT_FILESIZE)
									bSkipTag = true;
								else if (tag->HasName()) {
									strTag.Format(_T("%hs: "), tag->GetName());
									strTag.SetAt(0, _totupper(strTag[0]));
								} else {
									extern CString GetName(const CTag *pTag);
									const CString &strTagName(GetName(tag));
									if (strTagName.IsEmpty()) {
#ifdef _DEBUG
										strTag.Format(_T("Unknown tag #%02X: "), tag->GetNameID());
#endif
										break;
									}
									strTag.Format(_T("%s: "), (LPCTSTR)strTagName);
								}
								if (!bSkipTag) {
									if (tag->IsStr())
										strTag += tag->GetStr();
									else if (tag->IsInt()) {
										if (tag->GetNameID() == FT_MEDIA_LENGTH)
											strTag += SecToTimeLength(tag->GetInt());
										else
											strTag.AppendFormat(_T("%u"), tag->GetInt());
									} else if (tag->IsFloat())
										strTag.AppendFormat(_T("%f"), tag->GetFloat());
									else
#ifdef _DEBUG
										strTag.AppendFormat(_T("Unknown value type=#%02X"), tag->GetType());
#else
										strTag.Empty();
#endif
								}
							}
						}
						if (!strTag.IsEmpty()) {
							if (!strInfo.IsEmpty())
								strInfo += _T('\n');
							strInfo += strTag;
							if (strInfo.GetLength() >= pGetInfoTip->cchTextMax)
								break;
						}
					}
				}

#ifdef USE_DEBUG_DEVICE
				if (file->GetClientsCount()) {
					bool bFirst = true;
					if (file->GetClientID() && file->GetClientPort()) {
						uint32 uClientIP = file->GetClientID();
						uint32 uServerIP = file->GetClientServerIP();
						CString strSource;
						if (bFirst) {
							bFirst = false;
							strSource = _T("Sources");
						}
						strSource.AppendFormat(_T(": %u.%u.%u.%u:%u  Server: %u.%u.%u.%u:%u"),
							(uint8)uClientIP, (uint8)(uClientIP >> 8), (uint8)(uClientIP >> 16), (uint8)(uClientIP >> 24), file->GetClientPort(),
							(uint8)uServerIP, (uint8)(uServerIP >> 8), (uint8)(uServerIP >> 16), (uint8)(uServerIP >> 24), file->GetClientServerPort());
						if (!strInfo.IsEmpty())
							strInfo += _T('\n');
						strInfo += strSource;
					}

					const CSimpleArray<CSearchFile::SClient> &aClients = file->GetClients();
					for (int i = 0; i < aClients.GetSize(); ++i) {
						uint32 uClientIP = aClients[i].m_nIP;
						uint32 uServerIP = aClients[i].m_nServerIP;
						CString strSource;
						if (bFirst) {
							bFirst = false;
							strSource = _T("Sources");
						}
						strSource.AppendFormat(_T(": %u.%u.%u.%u:%u  Server: %u.%u.%u.%u:%u"),
							(uint8)uClientIP, (uint8)(uClientIP >> 8), (uint8)(uClientIP >> 16), (uint8)(uClientIP >> 24), aClients[i].m_nPort,
							(uint8)uServerIP, (uint8)(uServerIP >> 8), (uint8)(uServerIP >> 16), (uint8)(uServerIP >> 24), aClients[i].m_nServerPort);
						if (!strInfo.IsEmpty())
							strInfo += _T('\n');
						strInfo += strSource;
						if (strInfo.GetLength() >= pGetInfoTip->cchTextMax)
							break;
					}
				}

				if (file->GetServers().GetSize()) {
					const CSimpleArray<CSearchFile::SServer> &aServers = file->GetServers();
					for (int i = 0; i < aServers.GetSize(); ++i) {
						uint32 uServerIP = aServers[i].m_nIP;
						CString strServer;
						if (i == 0)
							strServer = _T("Servers");
						strServer.AppendFormat(_T(": %u.%u.%u.%u:%u  Avail: %u"),
							(uint8)uServerIP, (uint8)(uServerIP >> 8), (uint8)(uServerIP >> 16), (uint8)(uServerIP >> 24), aServers[i].m_nPort, aServers[i].m_uAvail);
						if (!strInfo.IsEmpty())
							strInfo += _T('\n');
						strInfo += strServer;
						if (strInfo.GetLength() >= pGetInfoTip->cchTextMax)
							break;
					}
				}
#endif
				strInfo.Insert(0, strHead);
				strInfo += TOOLTIP_AUTOFORMAT_SUFFIX_CH;
				_tcsncpy(pGetInfoTip->pszText, strInfo, pGetInfoTip->cchTextMax);
				pGetInfoTip->pszText[pGetInfoTip->cchTextMax - 1] = _T('\0');
			}
		} else {
			int iSelected = 0;
			ULONGLONG ulTotalSize = 0;
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				const CSearchFile *pFile = (CSearchFile*)GetItemData(GetNextSelectedItem(pos));
				if (pFile) {
					++iSelected;
					ulTotalSize += (uint64)pFile->GetFileSize();
				}
			}

			if (iSelected > 0) {
				CString strInfo(GetResString(IDS_FILES));
				strInfo.AppendFormat(_T(": %i\r\n%s: %s%c")
					, iSelected
					, (LPCTSTR)GetResString(IDS_DL_SIZE)
					, (LPCTSTR)FormatFileSize(ulTotalSize)
					, TOOLTIP_AUTOFORMAT_SUFFIX_CH
				);
				_tcsncpy(pGetInfoTip->pszText, strInfo, pGetInfoTip->cchTextMax);
				pGetInfoTip->pszText[pGetInfoTip->cchTextMax - 1] = _T('\0');
			}
		}
	}

	*pResult = 0;
}

void CSearchListCtrl::ExpandCollapseItem(int iItem, int iAction)
{
	if (iItem == -1)
		return;

	CSearchFile *searchfile = (CSearchFile*)GetItemData(iItem);
	if (searchfile->GetListParent() != NULL) {
		searchfile = searchfile->GetListParent();

		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)searchfile;
		iItem = FindItem(&find);
		if (iItem == -1)
			return;
	}
	if (!searchfile)
		return;

	if (!searchfile->IsListExpanded()) {
		if (iAction > COLLAPSE_ONLY) {
			// only expand when more than one child (more than the original entry itself)
			if (searchfile->GetListChildCount() < 2)
				return;

			// Go through the whole list to find out the sources for this file
			SetRedraw(false);
			const SearchList *list = theApp.searchlist->GetSearchListForID(searchfile->GetSearchID());
			for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
				const CSearchFile *cur_file = list->GetNext(pos);
				if (cur_file->GetListParent() == searchfile) {
					searchfile->SetListExpanded(true);
					InsertItem(LVIF_PARAM | LVIF_TEXT, iItem + 1, cur_file->GetFileName(), 0, 0, 0, (LPARAM)cur_file);
				}
			}
			SetRedraw(true);
		}
	} else {
		if (iAction == EXPAND_COLLAPSE || iAction == COLLAPSE_ONLY) {
			if (GetItemState(iItem, LVIS_SELECTED | LVIS_FOCUSED) != (LVIS_SELECTED | LVIS_FOCUSED)) {
				SetItemState(iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
				SetSelectionMark(iItem);
			}
			HideSources(searchfile);
		}
	}

	Update(iItem);
}

void CSearchListCtrl::HideSources(CSearchFile *toCollapse)
{
	SetRedraw(false);
	for (int i = GetItemCount(); --i >= 0;)
		if (reinterpret_cast<CSearchFile*>(GetItemData(i))->GetListParent() == toCollapse)
			DeleteItem(i);
	toCollapse->SetListExpanded(false);
	SetRedraw(true);
}

void CSearchListCtrl::OnNmClick(LPNMHDR pNMHDR, LRESULT*)
{
	POINT pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);
	if (pt.x < TREE_WIDTH) {
		LPNMITEMACTIVATE pNMIA = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
		ExpandCollapseItem(pNMIA->iItem, EXPAND_COLLAPSE);
	}
}

void CSearchListCtrl::OnNmDblClk(LPNMHDR, LRESULT*)
{
	POINT point;
	::GetCursorPos(&point);
	ScreenToClient(&point);
	if (point.x > TREE_WIDTH) {
		if (GetKeyState(VK_MENU) & 0x8000) {
			int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
			if (iSel >= 0) {
				/*const*/ CSearchFile *file = reinterpret_cast<CSearchFile*>(GetItemData(iSel));
				if (file) {
					CTypedPtrList<CPtrList, CSearchFile*> aFiles;
					aFiles.AddTail(file);
					CSearchResultFileDetailSheet sheet(aFiles, 0, this);
					sheet.DoModal();
				}
			}
		} else
			theApp.emuledlg->searchwnd->DownloadSelected();
	}
}

void CSearchListCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	if (!lpDrawItemStruct->itemData || theApp.IsClosing())
		return;

	CRect rcItem(lpDrawItemStruct->rcItem);
	CMemoryDC dc(CDC::FromHandle(lpDrawItemStruct->hDC), rcItem);
	BOOL bCtrlFocused;
	InitItemMemDC(dc, lpDrawItemStruct, bCtrlFocused);
	RECT rcClient;
	GetClientRect(&rcClient);
	CSearchFile *content = reinterpret_cast<CSearchFile*>(lpDrawItemStruct->itemData);
	if (!g_bLowColorDesktop || (lpDrawItemStruct->itemState & ODS_SELECTED) == 0)
		dc.SetTextColor(GetSearchItemColor(content));

	bool isChild = (content->GetListParent() != NULL);
	bool notLast = (lpDrawItemStruct->itemID + 1 != (UINT)GetItemCount());
	bool notFirst = (lpDrawItemStruct->itemID != 0);
	int tree_start = 0;
	int tree_end = 0;

	const CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	int iCount = pHeaderCtrl->GetItemCount();
	LONG itemLeft = rcItem.left;
	rcItem.right = rcItem.left - sm_iLabelOffset;
	rcItem.left += sm_iIconOffset;

	// icon
	LONG iIndent = isChild ? 8 : 0; // indent child items
	LONG iIconY = max((rcItem.Height() - theApp.GetSmallSytemIconSize().cy - 1) / 2, 0);
	const POINT point = { itemLeft + iIndent + TREE_WIDTH + 18, rcItem.top + iIconY };
	// spam indicator takes the place of comments & rating icon
	int iImage;
	if (thePrefs.IsSearchSpamFilterEnabled() && content->IsConsideredSpam())
		iImage = 8;
	else if (thePrefs.ShowRatingIndicator() && (content->HasComment() || content->HasRating() || content->IsKadCommentSearchRunning()))
		iImage = content->UserRating(true) + 1;
	else
		iImage = 0;
	if (iImage)
		m_ImageList.Draw(dc, iImage, point, ILD_NORMAL);

	iImage = theApp.GetFileTypeSystemImageIdx(content->GetFileName());
	::ImageList_Draw(theApp.GetSystemImageList(), iImage, dc, point.x - 18, point.y, ILD_TRANSPARENT);

	for (int iCurrent = 0; iCurrent < iCount; ++iCurrent) {
		int iColumn = pHeaderCtrl->OrderToIndex(iCurrent);
		if (IsColumnHidden(iColumn))
			continue;

		UINT uDrawTextAlignment;
		int iColumnWidth = GetColumnWidth(iColumn, uDrawTextAlignment);
		rcItem.left = itemLeft;
		rcItem.right = itemLeft + iColumnWidth - sm_iLabelOffset;
		switch (iColumn) {
		case 0: //file name & tree
			//set up tree vars
			tree_start = rcItem.left + 1;
			rcItem.left += min(8, iColumnWidth);
			tree_end = rcItem.left;
		default:
			rcItem.left += sm_iLabelOffset;
			if (rcItem.left < rcItem.right && HaveIntersection(rcClient, rcItem))
				if (isChild)
					DrawSourceChild(dc, iColumn, &rcItem, uDrawTextAlignment, content);
				else
					DrawSourceParent(dc, iColumn, &rcItem, uDrawTextAlignment, content);
		}
		itemLeft += iColumnWidth;
	}

	DrawFocusRect(dc, &lpDrawItemStruct->rcItem, lpDrawItemStruct->itemState & ODS_FOCUS, bCtrlFocused, lpDrawItemStruct->itemState & ODS_SELECTED);

	//draw the tree last, over selected and focus (looks better)
	if (tree_start < tree_end) {
		//set new bounds
		RECT tree_rect = { tree_start, lpDrawItemStruct->rcItem.top, tree_end, lpDrawItemStruct->rcItem.bottom };
		dc.SetBoundsRect(&tree_rect, DCB_DISABLE);

		//gather some information
		bool hasNext = notLast && reinterpret_cast<CSearchFile*>(GetItemData(lpDrawItemStruct->itemID + 1))->GetListParent() != NULL;
		bool isOpenRoot = hasNext && !isChild;

		//might as well calculate these now
		int treeCenter = tree_start + 4;
		int middle = (rcItem.top + rcItem.bottom + 1) / 2;

		//set up a new pen for drawing the tree
		COLORREF crLine = (!g_bLowColorDesktop || (lpDrawItemStruct->itemState & ODS_SELECTED) == 0) ? RGB(128, 128, 128) : m_crHighlightText;
		CPen pn;
		pn.CreatePen(PS_SOLID, 1, crLine);
		CPen *oldpn = dc.SelectObject(&pn);

		if (isChild) {
			//draw the line to the status bar
			dc.MoveTo(tree_end + 10, middle);
			dc.LineTo(tree_start + 4, middle);

			//draw the line to the child node
			if (hasNext) {
				dc.MoveTo(treeCenter, middle);
				dc.LineTo(treeCenter, rcItem.bottom + 1);
			}
		} else if (isOpenRoot || content->GetListChildCount() > 1) {
			//draw box
			const RECT circle_rec = { treeCenter - 4, middle - 5, treeCenter + 5, middle + 4 };
			CBrush brush(crLine);
			dc.FrameRect(&circle_rec, &brush);
			CPen penBlack;
			penBlack.CreatePen(PS_SOLID, 1, (!g_bLowColorDesktop || (lpDrawItemStruct->itemState & ODS_SELECTED) == 0) ? m_crWindowText : m_crHighlightText);
			CPen *pOldPen2 = dc.SelectObject(&penBlack);
			dc.MoveTo(treeCenter - 2, middle - 1);
			dc.LineTo(treeCenter + 3, middle - 1);

			if (!content->IsListExpanded()) {
				dc.MoveTo(treeCenter, middle - 3);
				dc.LineTo(treeCenter, middle + 2);
			}
			dc.SelectObject(pOldPen2);
			//draw the line to the child node
			if (hasNext) {
				dc.MoveTo(treeCenter, middle + 4);
				dc.LineTo(treeCenter, rcItem.bottom + 1);
			}
		}

		//draw the line back up to parent node
		if (notFirst && isChild) {
			dc.MoveTo(treeCenter, middle);
			dc.LineTo(treeCenter, rcItem.top - 1);
		}

		//put the old pen back
		dc.SelectObject(oldpn);
		pn.DeleteObject();
	}
}

COLORREF CSearchListCtrl::GetSearchItemColor(/*const*/ CSearchFile *src)
{
	const CKnownFile *pFile = theApp.downloadqueue->GetFileByID(src->GetFileHash());

	if (pFile) {
		if (pFile->IsPartFile()) {
			src->SetKnownType(CSearchFile::Downloading);
			if (static_cast<const CPartFile*>(pFile)->GetStatus() == PS_PAUSED)
				return m_crSearchResultDownloadStopped;
			return m_crSearchResultDownloading;
		}
		src->SetKnownType(CSearchFile::Shared);
		return m_crSearchResultShareing;
	}
	if (theApp.sharedfiles->GetFileByID(src->GetFileHash())) {
		src->SetKnownType(CSearchFile::Shared);
		return m_crSearchResultShareing;
	}
	if (theApp.knownfiles->FindKnownFileByID(src->GetFileHash())) {
		src->SetKnownType(CSearchFile::Downloaded);
		return m_crSearchResultKnown;
	}
	if (theApp.knownfiles->IsCancelledFileByID(src->GetFileHash())) {
		src->SetKnownType(CSearchFile::Cancelled);
		return m_crSearchResultCancelled;
	}

	// Spam check
	if (src->IsConsideredSpam() && thePrefs.IsSearchSpamFilterEnabled())
		return ::GetSysColor(COLOR_GRAYTEXT);

	// unknown file -> show shades of a color
	uint32 srccnt = src->GetSourceCount();
	srccnt -= static_cast<uint32>(srccnt > 0);
	return m_crShades[min(srccnt, AVBLYSHADECOUNT - 1)];
}

void CSearchListCtrl::DrawSourceChild(CDC *dc, int nColumn, LPRECT lpRect, UINT uDrawTextAlignment, const CSearchFile *src)
{
	const CString &sItem(GetItemDisplayText(src, nColumn));
	switch (nColumn) {
	case 0: // file name
		lpRect->left += 8 + 8 + theApp.GetSmallSytemIconSize().cy;// +sm_iLabelOffset;
		if ((thePrefs.ShowRatingIndicator() && (src->HasComment() || src->HasRating() || src->IsKadCommentSearchRunning()))
			|| (thePrefs.IsSearchSpamFilterEnabled() && src->IsConsideredSpam()))
		{
			lpRect->left += 16;
		}
	default:
		dc->DrawText(sItem, -1, lpRect, MLC_DT_TEXT | uDrawTextAlignment);
	case 4: // file type
	case 5: // file hash
		break;
	}
}

void CSearchListCtrl::DrawSourceParent(CDC *dc, int nColumn, LPRECT lpRect, UINT uDrawTextAlignment, const CSearchFile *src)
{
	const CString &sItem(GetItemDisplayText(src, nColumn));
	switch (nColumn) {
	case 0: // file name
		lpRect->left += 8 + theApp.GetSmallSytemIconSize().cx;
		if ((thePrefs.ShowRatingIndicator() && (src->HasComment() || src->HasRating() || src->IsKadCommentSearchRunning()))
			|| (thePrefs.IsSearchSpamFilterEnabled() && src->IsConsideredSpam()))
		{
			lpRect->left += 16;
		}
	default:
		dc->DrawText(sItem, -1, lpRect, MLC_DT_TEXT | uDrawTextAlignment);
		break;
	case 3: // complete sources
		{
			bool bComplete = IsComplete(src, src->GetSourceCount());
			COLORREF crOldTextColor = (bComplete ? 0 : dc->SetTextColor(RGB(255, 0, 0)));
			dc->DrawText(sItem, -1, lpRect, MLC_DT_TEXT | uDrawTextAlignment);
			if (!bComplete)
				dc->SetTextColor(crOldTextColor);
		}
	}
}

void CSearchListCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (nChar == 'C' && GetKeyState(VK_CONTROL) < 0) {
		// Ctrl+C: Copy listview items to clipboard
		SendMessage(WM_COMMAND, MP_GETED2KLINK);
		return;
	}
	CMuleListCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CSearchListCtrl::SetHighlightColors()
{
	// Default colors
	// --------------
	//	Blue:	User does not know that file; shades of blue are used to indicate availability of file
	//  Red:	User already has the file; it is currently downloading or it is currently shared
	//			-> 'Red' means: User can not add this file
	//	Green:	User 'knows' the file (it was already download once, but is currently not in share)
	COLORREF crSearchResultAvblyBase = RGB(0, 0, 255);
	m_crSearchResultDownloading = RGB(255, 0, 0);
	m_crSearchResultDownloadStopped = RGB(255, 0, 0);
	m_crSearchResultShareing = RGB(255, 0, 0);
	m_crSearchResultKnown = RGB(0, 128, 0);
	m_crSearchResultCancelled = RGB(0, 128, 0);

	theApp.LoadSkinColor(GetSkinKey() + _T("Fg_Downloading"), m_crSearchResultDownloading);
	if (!theApp.LoadSkinColor(_T("Fg_DownloadStopped"), m_crSearchResultDownloadStopped))
		m_crSearchResultDownloadStopped = m_crSearchResultDownloading;
	theApp.LoadSkinColor(GetSkinKey() + _T("Fg_Sharing"), m_crSearchResultShareing);
	theApp.LoadSkinColor(GetSkinKey() + _T("Fg_Known"), m_crSearchResultKnown);
	theApp.LoadSkinColor(GetSkinKey() + _T("Fg_AvblyBase"), crSearchResultAvblyBase);

	// precalculate sources shades
	COLORREF normFGC = GetTextColor();
	float rdelta = (GetRValue(crSearchResultAvblyBase) - GetRValue(normFGC)) / (float)AVBLYSHADECOUNT;
	float gdelta = (GetGValue(crSearchResultAvblyBase) - GetGValue(normFGC)) / (float)AVBLYSHADECOUNT;
	float bdelta = (GetBValue(crSearchResultAvblyBase) - GetBValue(normFGC)) / (float)AVBLYSHADECOUNT;

	for (int shades = 0; shades < AVBLYSHADECOUNT; ++shades)
		m_crShades[shades] = RGB(GetRValue(normFGC) + (rdelta * shades),
			GetGValue(normFGC) + (gdelta * shades),
			GetBValue(normFGC) + (bdelta * shades));
}

void CSearchListCtrl::OnSysColorChange()
{
	CMuleListCtrl::OnSysColorChange();
	SetHighlightColors();
}

void CSearchListCtrl::OnLvnKeyDown(LPNMHDR pNMHDR, LRESULT *pResult)
{
	LPNMLVKEYDOWN pLVKeyDown = reinterpret_cast<LPNMLVKEYDOWN>(pNMHDR);

	bool bAltKey = GetKeyState(VK_MENU) < 0;
	int iAction;
	if (pLVKeyDown->wVKey == VK_ADD || (bAltKey && pLVKeyDown->wVKey == VK_RIGHT))
		iAction = EXPAND_ONLY;
	else if (pLVKeyDown->wVKey == VK_SUBTRACT || (bAltKey && pLVKeyDown->wVKey == VK_LEFT))
		iAction = COLLAPSE_ONLY;
	else
		iAction = EXPAND_COLLAPSE;
	if (iAction < EXPAND_COLLAPSE)
		ExpandCollapseItem(GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED), iAction);
	*pResult = 0;
}

void CSearchListCtrl::ClearResultViewState(uint32 nResultsID)
{
	// just clean up our stored states for this search
	CSortSelectionState *pState;
	if (m_mapSortSelectionStates.Lookup(nResultsID, pState)) {
		m_mapSortSelectionStates.RemoveKey(nResultsID);
		delete pState;
	}
}

CString CSearchListCtrl::GetItemDisplayText(const CSearchFile *src, int iSubItem) const
{
	CString sText;
	switch (iSubItem) {
	case 0: //file name
		sText = src->GetFileName();
		break;
	case 1: //file size
		if (src->GetListParent() == NULL
			|| (thePrefs.GetDebugSearchResultDetailLevel() >= 1 && src->GetFileSize() != src->GetListParent()->GetFileSize()))
		{
			sText = FormatFileSize(src->GetFileSize());
		}
		break;
	case 2: //avail
		if (src->GetListParent() == NULL) {
			sText.Format(_T("%u"), src->GetSourceCount());
			if (thePrefs.IsExtControlsEnabled()) {
				if (src->IsKademlia()) {
					uint32 nKnownPublisher = (src->GetKadPublishInfo() >> 16) & 0xffu;
					if (nKnownPublisher > 0)
						sText.AppendFormat(_T(" (%u)"), nKnownPublisher);
				} else {
					int iClients = src->GetClientsCount();
					if (iClients > 0)
						sText.AppendFormat(_T(" (%i)"), iClients);
				}
			}
#ifdef _DEBUG
			if (src->GetKadPublishInfo() == 0)
				sText += _T(" | -");
			else
				sText.AppendFormat(_T(" | Names:%u, Pubs:%u, Trust:%0.2f")
					, (src->GetKadPublishInfo() >> 24) & 0xffu
					, (src->GetKadPublishInfo() >> 16) & 0xffu
					, (src->GetKadPublishInfo() & 0xffffu) / 100.0f);
#endif
		} else
			sText.Format(_T("%u"), src->GetListChildCount());
		break;
	case 3: //complete sources
		if (src->GetListParent() == NULL
			|| (thePrefs.IsExtControlsEnabled() && thePrefs.GetDebugSearchResultDetailLevel() >= 1))
		{
			sText = GetCompleteSourcesDisplayString(src, src->GetSourceCount());
		}
		break;
	case 4: //file type
		if (src->GetListParent() == NULL)
			sText = src->GetFileTypeDisplayStr();
		break;
	case 5: //file hash
		if (src->GetListParent() == NULL)
			sText = md4str(src->GetFileHash());
		break;
	case 6:
		sText = src->GetStrTagValue(FT_MEDIA_ARTIST);
		break;
	case 7:
		sText = src->GetStrTagValue(FT_MEDIA_ALBUM);
		break;
	case 8:
		sText = src->GetStrTagValue(FT_MEDIA_TITLE);
		break;
	case 9:
		{
			uint32 nMediaLength = src->GetIntTagValue(FT_MEDIA_LENGTH);
			if (nMediaLength)
				sText = SecToTimeLength(nMediaLength);
		}
		break;
	case 10:
		{
			uint32 nBitrate = src->GetIntTagValue(FT_MEDIA_BITRATE);
			if (nBitrate)
				sText.Format(_T("%u %s"), nBitrate, (LPCTSTR)GetResString(IDS_KBITSSEC));
		}
		break;
	case 11:
		sText = GetCodecDisplayName(src->GetStrTagValue(FT_MEDIA_CODEC));
		break;
	case 12: // dir
		if (src->GetDirectory())
			sText = src->GetDirectory();
		break;
	case 13: //known
		{
			UINT uid;
			switch (src->m_eKnown) {
			case CSearchFile::Shared:
				uid = IDS_SHARED;
				break;
			case CSearchFile::Downloading:
				uid = IDS_DOWNLOADING;
				break;
			case CSearchFile::Downloaded:
				uid = IDS_DOWNLOADED;
				break;
			case CSearchFile::Cancelled:
				uid = IDS_CANCELLED;
				break;
			default:
				uid = (src->IsConsideredSpam() && thePrefs.IsSearchSpamFilterEnabled()) ? IDS_SPAM : 0;
			}
			if (uid)
				sText = GetResString(uid);
#ifdef _DEBUG
			sText.AppendFormat(&_T(" SR: %u%%")[static_cast<size_t>(sText.IsEmpty())], src->GetSpamRating());
#endif
		}
		break;
	case 14: //AICH hash
		if (src->GetFileIdentifierC().HasAICHHash())
			sText = src->GetFileIdentifierC().GetAICHHash().GetString();
	}
	return sText;
}

void CSearchListCtrl::OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult)
{
	if (!theApp.IsClosing()) {
		// Although we have an owner drawn listview control we store the text for the primary item in the
		// listview, to be capable of quick searching those items via the keyboard. Because our listview
		// items may change their contents, we do this via a text callback function. The listview control
		// will send us the LVN_DISPINFO notification if it needs to know the contents of the primary item.
		//
		// But, the listview control sends this notification all the time, even if we do not search for an item.
		// At least this notification is only sent for the visible items and not for all items in the list.
		// Though, because this function is invoked *very* often, do *NOT* put any time consuming code in here.
		//
		// Vista: That callback is used to get the strings for the label tips for the sub(!)-items.
		//
		const LVITEMW &rItem = reinterpret_cast<NMLVDISPINFO*>(pNMHDR)->item;
		if (rItem.mask & LVIF_TEXT) {
			const CSearchFile *pSearchFile = reinterpret_cast<CSearchFile*>(rItem.lParam);
			if (pSearchFile != NULL)
				_tcsncpy_s(rItem.pszText, rItem.cchTextMax, GetItemDisplayText(pSearchFile, rItem.iSubItem), _TRUNCATE);
		}
	}
	*pResult = 0;
}

CString CSearchListCtrl::FormatFileSize(ULONGLONG ullFileSize) const
{
	if (m_eFileSizeFormat == fsizeKByte)
		// Always round up to next KiB (this is same as Windows Explorer is doing)
		return GetFormatedUInt64((ullFileSize + 1024 - 1) / 1024) + _T(' ') + GetResString(IDS_KBYTES);

	if (m_eFileSizeFormat == fsizeMByte) {
		//return GetFormatedUInt64((ullFileSize + 1024 * 1024 - 1) / (1024 * 1024)) + _T(' ') + GetResString(IDS_MBYTES);
		double fFileSize = ullFileSize / (1024.0 * 1024.0);
		if (fFileSize < 0.01)
			fFileSize = 0.01;

		static NUMBERFMT nf;
		if (nf.Grouping == 0) {
			nf.NumDigits = 2;
			nf.LeadingZero = 1;
			nf.Grouping = 3;
			// we are hardcoding the following two format chars by intention because the C-RTL also has the decimal sep hardcoded to '.'
			nf.lpDecimalSep = _T(".");
			nf.lpThousandSep = _T(",");
			nf.NegativeOrder = 0;
		}
		CString sVal, strVal;
		sVal.Format(_T("%.2f"), fFileSize);
		int iResult = GetNumberFormat(LOCALE_SYSTEM_DEFAULT, 0, sVal, &nf, strVal.GetBuffer(80), 80);
		strVal.ReleaseBuffer();
		return (iResult ? strVal : sVal) + _T(' ') + GetResString(IDS_MBYTES);
	}

	return CastItoXBytes(ullFileSize);
}

void CSearchListCtrl::SetFileSizeFormat(EFileSizeFormat eFormat)
{
	m_eFileSizeFormat = eFormat;
	Invalidate();
	UpdateWindow();
}

bool CSearchListCtrl::IsFilteredOut(const CSearchFile *pSearchFile) const
{
	if (pSearchFile->m_flags.noshow) //do not show
		return true;
	const CStringArray &rastrFilter = theApp.emuledlg->searchwnd->m_pwndResults->m_astrFilter;
	if (!rastrFilter.IsEmpty()) {
		// filtering is done by text only for all columns to keep it consistent and simple
		// for the user even if that doesn't allow complex filters
		// for example for a file size range - but this could be done at server search time already
		const CString &szFilterTarget(GetItemDisplayText(pSearchFile, theApp.emuledlg->searchwnd->m_pwndResults->GetFilterColumn()));

		for (INT_PTR i = rastrFilter.GetCount(); --i >= 0;) {
			LPCTSTR pszText = (LPCTSTR)rastrFilter[i];
			bool bAnd = (*pszText != _T('-'));
			if (!bAnd)
				++pszText;

			bool bFound = (stristr(szFilterTarget, pszText) != NULL);
			if (bAnd != bFound)
				return true;
		}
	}
	return false;
}