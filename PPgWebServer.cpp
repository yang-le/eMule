//this file is part of eMule
//Copyright (C)2002-2008 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / http://www.emule-project.net )
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
#include "emule.h"
#include "PPgWebServer.h"
#include "otherfunctions.h"
#include "WebServer.h"
#include "emuledlg.h"
#include "Preferences.h"
#include "ServerWnd.h"
#include "HelpIDs.h"
#include "ppgwebserver.h"
#include "UPnPImplWrapper.h"
#include "UPnPImpl.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


static const TCHAR *sHiddenPassword = _T("*****");

IMPLEMENT_DYNAMIC(CPPgWebServer, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgWebServer, CPropertyPage)
	ON_EN_CHANGE(IDC_WSPASS, OnDataChange)
	ON_EN_CHANGE(IDC_WSPASSLOW, OnDataChange)
	ON_EN_CHANGE(IDC_WSPORT, OnDataChange)
	ON_EN_CHANGE(IDC_TMPLPATH, OnDataChange)
	ON_EN_CHANGE(IDC_CERTPATH, OnDataChange)
	ON_EN_CHANGE(IDC_KEYPATH, OnDataChange)
	ON_EN_CHANGE(IDC_WSTIMEOUT, OnDataChange)
	ON_BN_CLICKED(IDC_WSENABLED, OnEnChangeWSEnabled)
	ON_BN_CLICKED(IDC_WEB_HTTPS, OnChangeHTTPS)
	ON_BN_CLICKED(IDC_WSENABLEDLOW, OnEnChangeWSEnabled)
	ON_BN_CLICKED(IDC_WSRELOADTMPL, OnReloadTemplates)
	ON_BN_CLICKED(IDC_TMPLBROWSE, OnBnClickedTmplbrowse)
	ON_BN_CLICKED(IDC_CERTBROWSE, OnBnClickedCertbrowse)
	ON_BN_CLICKED(IDC_KEYBROWSE, OnBnClickedKeybrowse)
	ON_BN_CLICKED(IDC_WS_GZIP, OnDataChange)
	ON_BN_CLICKED(IDC_WS_ALLOWHILEVFUNC, OnDataChange)
	ON_BN_CLICKED(IDC_WSUPNP, OnDataChange)
	ON_WM_HELPINFO()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CPPgWebServer::CPPgWebServer()
	: CPropertyPage(CPPgWebServer::IDD), m_bModified(), bCreated(), m_icoBrowse()
{
}

CPPgWebServer::~CPPgWebServer()
{
}

void CPPgWebServer::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
}

BOOL CPPgWebServer::OnInitDialog()
{
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);

	AddBuddyButton(GetDlgItem(IDC_TMPLPATH)->m_hWnd, ::GetDlgItem(m_hWnd, IDC_TMPLBROWSE));
	InitAttachedBrowseButton(::GetDlgItem(m_hWnd, IDC_TMPLBROWSE), m_icoBrowse);

	AddBuddyButton(GetDlgItem(IDC_CERTPATH)->m_hWnd, ::GetDlgItem(m_hWnd, IDC_CERTBROWSE));
	InitAttachedBrowseButton(::GetDlgItem(m_hWnd, IDC_CERTBROWSE), m_icoBrowse);

	AddBuddyButton(GetDlgItem(IDC_KEYPATH)->m_hWnd, ::GetDlgItem(m_hWnd, IDC_KEYBROWSE));
	InitAttachedBrowseButton(::GetDlgItem(m_hWnd, IDC_KEYBROWSE), m_icoBrowse);

	((CEdit*)GetDlgItem(IDC_WSPASS))->SetLimitText(12);
	((CEdit*)GetDlgItem(IDC_WSPASSLOW))->SetLimitText(12);
	((CEdit*)GetDlgItem(IDC_WSPORT))->SetLimitText(5);

	LoadSettings();
	Localize();

	OnEnChangeWSEnabled();

	return TRUE;
}

void CPPgWebServer::LoadSettings()
{
	CheckDlgButton(IDC_WSENABLED, static_cast<UINT>(thePrefs.GetWSIsEnabled()));
	CheckDlgButton(IDC_WS_GZIP, static_cast<UINT>(thePrefs.GetWebUseGzip()));

	CheckDlgButton(IDC_WSUPNP, static_cast<UINT>(thePrefs.m_bWebUseUPnP));
	GetDlgItem(IDC_WSUPNP)->EnableWindow(thePrefs.IsUPnPEnabled() && thePrefs.GetWSIsEnabled());
	SetDlgItemInt(IDC_WSPORT, thePrefs.GetWSPort());

	SetDlgItemText(IDC_TMPLPATH, thePrefs.GetTemplate());
	SetDlgItemInt(IDC_WSTIMEOUT, thePrefs.GetWebTimeoutMins());

	CheckDlgButton(IDC_WEB_HTTPS, static_cast<UINT>(thePrefs.GetWebUseHttps()));
	SetDlgItemText(IDC_CERTPATH, thePrefs.GetWebCertPath());
	SetDlgItemText(IDC_KEYPATH, thePrefs.GetWebKeyPath());

	SetDlgItemText(IDC_WSPASS, sHiddenPassword);
	CheckDlgButton(IDC_WS_ALLOWHILEVFUNC, static_cast<UINT>(thePrefs.GetWebAdminAllowedHiLevFunc()));
	CheckDlgButton(IDC_WSENABLEDLOW, static_cast<UINT>(thePrefs.GetWSIsLowUserEnabled()));
	SetDlgItemText(IDC_WSPASSLOW, sHiddenPassword);

	SetModified(FALSE);	// FoRcHa
}

void CPPgWebServer::OnDataChange()
{
	SetModified();
	SetTmplButtonState();
}

BOOL CPPgWebServer::OnApply()
{
	if (m_bModified) {
		CString sBuf;
		bool bUPnP = thePrefs.GetWSUseUPnP();
		bool bWSIsEnabled = IsDlgButtonChecked(IDC_WSENABLED) != 0;
		// get and check templatefile existence...
		GetDlgItemText(IDC_TMPLPATH, sBuf);
		if (bWSIsEnabled && !PathFileExists(sBuf)) {
			CString buffer;
			buffer.Format(GetResString(IDS_WEB_ERR_CANTLOAD), (LPCTSTR)sBuf);
			AfxMessageBox(buffer, MB_OK);
			return FALSE;
		}
		thePrefs.SetTemplate(sBuf);
		theApp.webserver->ReloadTemplates();

		uint16 oldPort = thePrefs.GetWSPort();

		GetDlgItemText(IDC_WSPASS, sBuf);
		if (sBuf != sHiddenPassword) {
			thePrefs.SetWSPass(sBuf);
			SetDlgItemText(IDC_WSPASS, sHiddenPassword);
		}

		GetDlgItemText(IDC_WSPASSLOW, sBuf);
		if (sBuf != sHiddenPassword) {
			thePrefs.SetWSLowPass(sBuf);
			SetDlgItemText(IDC_WSPASSLOW, sHiddenPassword);
		}

		uint16 u = (uint16)GetDlgItemInt(IDC_WSPORT, NULL, FALSE);
		if (u != oldPort && u > 0) {
			thePrefs.SetWSPort(u);
			theApp.webserver->RestartServer();
		}

		thePrefs.m_iWebTimeoutMins = (int)GetDlgItemInt(IDC_WSTIMEOUT, NULL, FALSE);

		bool bHTTPS = IsDlgButtonChecked(IDC_WEB_HTTPS) != 0;
		GetDlgItemText(IDC_CERTPATH, sBuf);
		if (bWSIsEnabled && bHTTPS && !PathFileExists(sBuf)) {
			AfxMessageBox(GetResString(IDS_CERT_NOT_FOUND), MB_OK);
			return FALSE;
		}
		thePrefs.SetWebCertPath(sBuf);

		GetDlgItemText(IDC_KEYPATH, sBuf);
		if (bWSIsEnabled && bHTTPS && !PathFileExists(sBuf)) {
			AfxMessageBox(GetResString(IDS_KEY_NOT_FOUND), MB_OK);
			return FALSE;
		}
		thePrefs.SetWebKeyPath(sBuf);

		thePrefs.SetWSIsEnabled(bWSIsEnabled);
		thePrefs.SetWebUseGzip(IsDlgButtonChecked(IDC_WS_GZIP) != 0);
		thePrefs.SetWebUseHttps(IsDlgButtonChecked(IDC_WEB_HTTPS) != 0);
		thePrefs.SetWSIsLowUserEnabled(IsDlgButtonChecked(IDC_WSENABLEDLOW) != 0);
		theApp.webserver->StartServer();
		thePrefs.m_bAllowAdminHiLevFunc = (IsDlgButtonChecked(IDC_WS_ALLOWHILEVFUNC) != 0);

		thePrefs.m_bWebUseUPnP = (IsDlgButtonChecked(IDC_WSUPNP) != 0);
		if (bUPnP != (thePrefs.m_bWebUseUPnP && bWSIsEnabled) && thePrefs.IsUPnPEnabled() && theApp.m_pUPnPFinder != NULL) //add the port to existing mapping without having eMule restarting (if all conditions are met)
			theApp.m_pUPnPFinder->GetImplementation()->LateEnableWebServerPort(bUPnP ? 0 : thePrefs.GetWSPort());

		theApp.emuledlg->serverwnd->UpdateMyInfo();
		SetModified(FALSE);
		SetTmplButtonState();
	}

	return CPropertyPage::OnApply();
}

void CPPgWebServer::Localize()
{
	if (m_hWnd) {
		SetWindowText(GetResString(IDS_PW_WS));

		SetDlgItemText(IDC_WSENABLED, GetResString(IDS_ENABLED));
		SetDlgItemText(IDC_WS_GZIP, GetResString(IDS_WEB_GZIP_COMPRESSION));
		SetDlgItemText(IDC_WSUPNP, GetResString(IDS_WEBUPNPINCLUDE));
		SetDlgItemText(IDC_WSPORT_LBL, GetResString(IDS_PORT));

		SetDlgItemText(IDC_TEMPLATE, GetResString(IDS_WS_RELOAD_TMPL) + _T(':'));
		SetDlgItemText(IDC_WSRELOADTMPL, GetResString(IDS_SF_RELOAD));

		SetDlgItemText(IDC_STATIC_GENERAL, GetResString(IDS_PW_GENERAL));

		SetDlgItemText(IDC_WSTIMEOUTLABEL, GetResString(IDS_WEB_SESSIONTIMEOUT) + _T(':'));
		SetDlgItemText(IDC_MINS, GetResString(IDS_LONGMINS).MakeLower());

		SetDlgItemText(IDC_WEB_HTTPS, GetResString(IDS_WEB_HTTPS));
		SetDlgItemText(IDC_WEB_CERT, GetResString(IDS_CERTIFICATE) + _T(':'));
		SetDlgItemText(IDC_WEB_KEY, GetResString(IDS_KEY) + _T(':'));

		SetDlgItemText(IDC_STATIC_ADMIN, GetResString(IDS_ADMIN));
		SetDlgItemText(IDC_WSPASS_LBL, GetResString(IDS_WS_PASS));
		SetDlgItemText(IDC_WS_ALLOWHILEVFUNC, GetResString(IDS_WEB_ALLOWHILEVFUNC));

		SetDlgItemText(IDC_STATIC_LOWUSER, GetResString(IDS_WEB_LOWUSER));
		SetDlgItemText(IDC_WSENABLEDLOW, GetResString(IDS_ENABLED));
		SetDlgItemText(IDC_WSPASS_LBL2, GetResString(IDS_WS_PASS));
	}
}

void CPPgWebServer::SetUPnPState()
{
	GetDlgItem(IDC_WSUPNP)->EnableWindow(thePrefs.IsUPnPEnabled() && IsDlgButtonChecked(IDC_WSENABLED));
}

void CPPgWebServer::OnChangeHTTPS()
{
	BOOL bEnable = IsDlgButtonChecked(IDC_WSENABLED) && IsDlgButtonChecked(IDC_WEB_HTTPS);
	GetDlgItem(IDC_CERTPATH)->EnableWindow(bEnable);
	GetDlgItem(IDC_CERTBROWSE)->EnableWindow(bEnable);
	GetDlgItem(IDC_KEYPATH)->EnableWindow(bEnable);
	GetDlgItem(IDC_KEYBROWSE)->EnableWindow(bEnable);
}

void CPPgWebServer::OnEnChangeWSEnabled()
{
	BOOL bIsWIEnabled = (BOOL)IsDlgButtonChecked(IDC_WSENABLED);
	GetDlgItem(IDC_WS_GZIP)->EnableWindow(bIsWIEnabled);
	GetDlgItem(IDC_WSPORT)->EnableWindow(bIsWIEnabled);
	GetDlgItem(IDC_TMPLPATH)->EnableWindow(bIsWIEnabled);
	GetDlgItem(IDC_TMPLBROWSE)->EnableWindow(bIsWIEnabled);
	GetDlgItem(IDC_WSTIMEOUT)->EnableWindow(bIsWIEnabled);
	GetDlgItem(IDC_WEB_HTTPS)->EnableWindow(bIsWIEnabled);
	GetDlgItem(IDC_WSPASS)->EnableWindow(bIsWIEnabled);
	GetDlgItem(IDC_WS_ALLOWHILEVFUNC)->EnableWindow(bIsWIEnabled);
	GetDlgItem(IDC_WSENABLEDLOW)->EnableWindow(bIsWIEnabled);
	GetDlgItem(IDC_WSPASSLOW)->EnableWindow(bIsWIEnabled && IsDlgButtonChecked(IDC_WSENABLEDLOW));
	SetUPnPState();
	SetTmplButtonState();
	OnChangeHTTPS();

	SetModified();
}

void CPPgWebServer::OnReloadTemplates()
{
	theApp.webserver->ReloadTemplates();
}

void CPPgWebServer::OnBnClickedTmplbrowse()
{
	CString strTempl;
	GetDlgItemText(IDC_TMPLPATH, strTempl);
	CString buffer(GetResString(IDS_WS_RELOAD_TMPL) + _T(" (*.tmpl)|*.tmpl||"));
	if (DialogBrowseFile(buffer, buffer, strTempl)) {
		SetDlgItemText(IDC_TMPLPATH, buffer);
		SetModified();
	}
	SetTmplButtonState();
}

void CPPgWebServer::OnBnClickedCertbrowse()
{
	CString strCert;
	GetDlgItemText(IDC_CERTPATH, strCert);
	CString buffer(GetResString(IDS_CERTIFICATE) + _T(" (*.crt)|*.crt|All Files (*.*)|*.*||"));
	if (DialogBrowseFile(buffer, buffer, strCert)) {
		SetDlgItemText(IDC_CERTPATH, buffer);
		SetModified();
	}
	SetModified();
}

void CPPgWebServer::OnBnClickedKeybrowse()
{
	CString strKey;
	GetDlgItemText(IDC_KEYPATH, strKey);
	CString buffer(GetResString(IDS_KEY) + _T(" (*.key)|*.key|All Files (*.*)|*.*||"));
	if (DialogBrowseFile(buffer, buffer, strKey)) {
		SetDlgItemText(IDC_KEYPATH, buffer);
		SetModified();
	}
	SetModified();
}

void CPPgWebServer::SetTmplButtonState()
{
	CString buffer;
	GetDlgItemText(IDC_TMPLPATH, buffer);

	GetDlgItem(IDC_WSRELOADTMPL)->EnableWindow(IsDlgButtonChecked(IDC_WSENABLED) && (buffer.CompareNoCase(thePrefs.GetTemplate()) == 0));
}

void CPPgWebServer::OnHelp()
{
	theApp.ShowHelp(eMule_FAQ_Preferences_WebInterface);
}

BOOL CPPgWebServer::OnCommand(WPARAM wParam, LPARAM lParam)
{
	if (wParam == ID_HELP) {
		OnHelp();
		return TRUE;
	}
	return __super::OnCommand(wParam, lParam);
}

BOOL CPPgWebServer::OnHelpInfo(HELPINFO* /*pHelpInfo*/)
{
	OnHelp();
	return TRUE;
}

void CPPgWebServer::OnDestroy()
{
	CPropertyPage::OnDestroy();
	if (m_icoBrowse) {
		VERIFY(DestroyIcon(m_icoBrowse));
		m_icoBrowse = NULL;
	}
}