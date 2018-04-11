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
#include "IrcSocket.h"
#include "AsyncProxySocketLayer.h"
#include "IrcMain.h"
#include "Preferences.h"
#include "OtherFunctions.h"
#include "Statistics.h"
#include "Log.h"
#include "Exceptions.h"
#include "opcodes.h"
#include "StringConversion.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


CIrcSocket::CIrcSocket(CIrcMain* pIrcMain)
{
	m_pIrcMain = pIrcMain;
	m_pProxyLayer = NULL;
}

CIrcSocket::~CIrcSocket()
{
	CIrcSocket::RemoveAllLayers();
}

BOOL CIrcSocket::Create(UINT uSocketPort, int iSocketType, long lEvent, const CString& sSocketAddress)
{
	const ProxySettings& proxy = thePrefs.GetProxySettings();
	if (proxy.UseProxy && proxy.type != PROXYTYPE_NOPROXY) {
		m_pProxyLayer = new CAsyncProxySocketLayer;
		switch (proxy.type) {
		case PROXYTYPE_SOCKS4:
		case PROXYTYPE_SOCKS4A:
			m_pProxyLayer->SetProxy(proxy.type, proxy.name, proxy.port);
			break;
		case PROXYTYPE_SOCKS5:
		case PROXYTYPE_HTTP10:
		case PROXYTYPE_HTTP11:
			if (proxy.EnablePassword)
				m_pProxyLayer->SetProxy(proxy.type, proxy.name, proxy.port, proxy.user, proxy.password);
			else
				m_pProxyLayer->SetProxy(proxy.type, proxy.name, proxy.port);
			break;
		default:
			ASSERT(0);
		}
		AddLayer(m_pProxyLayer);
	}
	return CAsyncSocketEx::Create(uSocketPort, iSocketType, lEvent, sSocketAddress);
}

void CIrcSocket::Connect()
{
	int iPort = 6667;
	CString strServer = thePrefs.GetIRCServer();
	int iIndex = strServer.Find(_T(':'));
	if (iIndex != -1)
	{
		iPort = _tstoi(strServer.Mid(iIndex + 1));
		if (iPort <= 0)
			iPort = 6667;
		strServer = strServer.Left(iIndex);
	}
	CAsyncSocketEx::Connect(strServer, iPort);
}

void CIrcSocket::OnReceive(int iErrorCode)
{
	if (iErrorCode)
	{
		if (thePrefs.GetVerbose())
			AddDebugLogLine(false, _T("IRC socket: Failed to read - %s"), (LPCTSTR)GetErrorMessage(iErrorCode, 1));
		return;
	}

#define RCVBUFSIZE (1024)
	TRACE("CIrcSocket::OnReceive\n");
	try
	{
		int iLength;
		do
		{
			char cBuffer[RCVBUFSIZE];
			iLength = Receive(cBuffer, RCVBUFSIZE - 1);
			TRACE("iLength=%d\n", iLength);
			if (iLength < 0)
			{
				if (thePrefs.GetVerbose())
					AddDebugLogLine(false, _T("IRC socket: Failed to read - %s"), (LPCTSTR)GetErrorMessage(GetLastError(), 1));
				return;
			}
			if (iLength > 0)
			{
				cBuffer[iLength] = '\0';
				theStats.AddDownDataOverheadOther(iLength);
				m_pIrcMain->PreParseMessage(cBuffer);
			}
		}
		while (iLength > RCVBUFSIZE - 2);
	}
	CATCH_DFLT_EXCEPTIONS(_T(__FUNCTION__))
	CATCH_DFLT_ALL(_T(__FUNCTION__))
}

void CIrcSocket::OnSend(int iErrorCode)
{
	if (iErrorCode)
	{
		if (thePrefs.GetVerbose())
			AddDebugLogLine(false, _T("IRC socket: Failed to send - %s"), (LPCTSTR)GetErrorMessage(iErrorCode, 1));
		return;
	}
	TRACE("CIrcSocket::OnSend\n");
}

void CIrcSocket::OnConnect(int iErrorCode)
{
	if (iErrorCode)
	{
		LogError(LOG_STATUSBAR, _T("IRC socket: Failed to connect - %s"), (LPCTSTR)GetErrorMessage(iErrorCode, 1));
		m_pIrcMain->Disconnect();
		return;
	}
	m_pIrcMain->SetConnectStatus(true);
	m_pIrcMain->SendLogin();
}

void CIrcSocket::OnClose(int iErrorCode)
{
	if (iErrorCode && thePrefs.GetVerbose())
		AddDebugLogLine(false, _T("IRC socket: Failed to close - %s"), (LPCTSTR)GetErrorMessage(iErrorCode, 1));
	m_pIrcMain->Disconnect();
}

int CIrcSocket::SendString(const CString& sMessage)
{
	CStringA sMessageA = thePrefs.GetIRCEnableUTF8() ? StrToUtf8(sMessage) : CStringA(sMessage);
	TRACE("CIrcSocket::SendString: %s\n", (LPCSTR)sMessageA);
	sMessageA += "\r\n";
	int iSize = sMessageA.GetLength();
	theStats.AddUpDataOverheadOther(iSize);
	return Send(sMessageA, iSize);
	//int iResult = Send(sMessageA, iSize);
	//ASSERT( iResult == iSize ); //too much noise from network errors
	//return iResult;
}

void CIrcSocket::RemoveAllLayers()
{
	CAsyncSocketEx::RemoveAllLayers();
	delete m_pProxyLayer;
	m_pProxyLayer = NULL;
}

int CIrcSocket::OnLayerCallback(const CAsyncSocketExLayer* pLayer, int nType, int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nType == LAYERCALLBACK_LAYERSPECIFIC)
	{
		ASSERT( pLayer );
		if (pLayer == m_pProxyLayer)
		{
			switch (nCode)
			{
				case PROXYERROR_NOCONN:
				case PROXYERROR_REQUESTFAILED:
					{
						CString strError(GetProxyError(nCode));
						if (lParam)
						{
							strError += _T(" - ");
							strError += (LPCSTR)lParam;
						}
						if (wParam)
						{
							CString strErrInf;
							if (GetErrorMessage((DWORD)wParam, strErrInf, 1)) {
								strError += _T(" - ");
								strError += strErrInf;
							}
						}
						LogWarning(LOG_STATUSBAR, _T("IRC socket: %s"), (LPCTSTR)strError);
						break;
					}
				default:
					LogWarning(LOG_STATUSBAR, _T("IRC socket: %s"), (LPCTSTR)GetProxyError(nCode));
			}
		}
	}
	return 1;
}
