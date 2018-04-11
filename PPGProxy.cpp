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
#include "PPGProxy.h"
#include "opcodes.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "HelpIDs.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


IMPLEMENT_DYNAMIC(CPPgProxy, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgProxy, CPropertyPage)
	ON_BN_CLICKED(IDC_ENABLEPROXY, OnBnClickedEnableProxy)
	ON_BN_CLICKED(IDC_ENABLEAUTH, OnBnClickedEnableAuthentication)
	ON_CBN_SELCHANGE(IDC_PROXYTYPE, OnCbnSelChangeProxyType)
	ON_EN_CHANGE(IDC_PROXYNAME, OnEnChangeProxyName)
	ON_EN_CHANGE(IDC_PROXYPORT, OnEnChangeProxyPort)
	ON_EN_CHANGE(IDC_USERNAME_A, OnEnChangeUserName)
	ON_EN_CHANGE(IDC_PASSWORD, OnEnChangePassword)
	ON_WM_HELPINFO()
END_MESSAGE_MAP()

CPPgProxy::CPPgProxy()
	: CPropertyPage(CPPgProxy::IDD), proxy()
{
}

CPPgProxy::~CPPgProxy()
{
}

void CPPgProxy::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
}

BOOL CPPgProxy::OnInitDialog()
{
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);

	proxy = thePrefs.GetProxySettings();
	LoadSettings();
	Localize();

	return TRUE;  // return TRUE unless you set the focus to a control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

BOOL CPPgProxy::OnApply()
{
	proxy.UseProxy = (IsDlgButtonChecked(IDC_ENABLEPROXY) != 0);
	proxy.EnablePassword = ((CButton*)GetDlgItem(IDC_ENABLEAUTH))->GetCheck() != 0;
	proxy.type = (uint16)((CComboBox*)GetDlgItem(IDC_PROXYTYPE))->GetCurSel();

	CString str;
	if (GetDlgItemText(IDC_PROXYNAME, str)) {
		CStringA strProxyA(str, 256);
		int iColon = strProxyA.Find(':');
		if (iColon > -1) {
			SetDlgItemTextA(m_hWnd, IDC_PROXYPORT, strProxyA.Mid(iColon + 1));
			strProxyA.Truncate(iColon);
		}
		proxy.name = strProxyA;
	} else {
		proxy.name.Empty();
		proxy.UseProxy = false;
	}

	proxy.port = (uint16)GetDlgItemInt(IDC_PROXYPORT, NULL, FALSE);
	if (!proxy.port)
		proxy.port = 1080;

	if (GetDlgItemText(IDC_USERNAME_A, str))
		proxy.user = CStringA(str);
	else {
		proxy.user.Empty();
		proxy.EnablePassword = false;
	}

	if (GetDlgItemText(IDC_PASSWORD, str))
		proxy.password = CStringA(str);
	else {
		proxy.password.Empty();
		proxy.EnablePassword = false;
	}

	thePrefs.SetProxySettings(proxy);
	LoadSettings();
	return TRUE;
}

void CPPgProxy::OnBnClickedEnableProxy()
{
	SetModified(TRUE);

	BOOL bEnable = ((CButton*)GetDlgItem(IDC_ENABLEPROXY))->GetCheck();
	GetDlgItem(IDC_ENABLEAUTH)->EnableWindow(bEnable);
	GetDlgItem(IDC_PROXYTYPE)->EnableWindow(bEnable);
	GetDlgItem(IDC_PROXYNAME)->EnableWindow(bEnable);
	GetDlgItem(IDC_PROXYPORT)->EnableWindow(bEnable);
	GetDlgItem(IDC_USERNAME_A)->EnableWindow(bEnable);
	GetDlgItem(IDC_PASSWORD)->EnableWindow(bEnable);
	if (bEnable) {
		OnBnClickedEnableAuthentication();
		OnCbnSelChangeProxyType();
	}
}

void CPPgProxy::OnBnClickedEnableAuthentication()
{
	SetModified(TRUE);
	BOOL bEnable = ((CButton*)GetDlgItem(IDC_ENABLEAUTH))->GetCheck();
	GetDlgItem(IDC_USERNAME_A)->EnableWindow(bEnable);
	GetDlgItem(IDC_PASSWORD)->EnableWindow(bEnable);
}

void CPPgProxy::OnCbnSelChangeProxyType()
{
	SetModified(TRUE);
	CComboBox* cbbox = (CComboBox*)GetDlgItem(IDC_PROXYTYPE);
	if (!(cbbox->GetCurSel() == PROXYTYPE_SOCKS5 || cbbox->GetCurSel() == PROXYTYPE_HTTP10 || cbbox->GetCurSel() == PROXYTYPE_HTTP11)) {
		((CButton*)GetDlgItem(IDC_ENABLEAUTH))->SetCheck(0);
		OnBnClickedEnableAuthentication();
		GetDlgItem(IDC_ENABLEAUTH)->EnableWindow(FALSE);
	} else
		GetDlgItem(IDC_ENABLEAUTH)->EnableWindow(TRUE);
}

void CPPgProxy::LoadSettings()
{
	((CButton*)GetDlgItem(IDC_ENABLEPROXY))->SetCheck(proxy.UseProxy);
	((CButton*)GetDlgItem(IDC_ENABLEAUTH))->SetCheck(proxy.EnablePassword);
	((CComboBox*)GetDlgItem(IDC_PROXYTYPE))->SetCurSel(proxy.type);
	SetWindowTextA(*GetDlgItem(IDC_PROXYNAME), proxy.name);
	SetDlgItemInt(IDC_PROXYPORT, proxy.port);
	SetWindowTextA(*GetDlgItem(IDC_USERNAME_A), proxy.user);
	SetWindowTextA(*GetDlgItem(IDC_PASSWORD), proxy.password);
	OnBnClickedEnableProxy();
}

void CPPgProxy::Localize()
{
	if (m_hWnd) {
		SetWindowText(GetResString(IDS_PW_PROXY));
		SetDlgItemText(IDC_ENABLEPROXY, GetResString(IDS_PROXY_ENABLE));
		SetDlgItemText(IDC_PROXYTYPE_LBL, GetResString(IDS_PROXY_TYPE));
		SetDlgItemText(IDC_PROXYNAME_LBL, GetResString(IDS_PROXY_HOST));
		SetDlgItemText(IDC_PROXYPORT_LBL, GetResString(IDS_PROXY_PORT));
		SetDlgItemText(IDC_ENABLEAUTH, GetResString(IDS_PROXY_AUTH));
		SetDlgItemText(IDC_USERNAME_LBL, GetResString(IDS_CD_UNAME));
		SetDlgItemText(IDC_PASSWORD_LBL, GetResString(IDS_WS_PASS) + _T(':'));
		SetDlgItemText(IDC_AUTH_LBL, GetResString(IDS_AUTH));
		SetDlgItemText(IDC_AUTH_LBL2, GetResString(IDS_PW_GENERAL));
	}
}

void CPPgProxy::OnHelp()
{
	theApp.ShowHelp(eMule_FAQ_Preferences_Proxy);
}

BOOL CPPgProxy::OnCommand(WPARAM wParam, LPARAM lParam)
{
	if (wParam == ID_HELP) {
		OnHelp();
		return TRUE;
	}
	return __super::OnCommand(wParam, lParam);
}

BOOL CPPgProxy::OnHelpInfo(HELPINFO* /*pHelpInfo*/)
{
	OnHelp();
	return TRUE;
}