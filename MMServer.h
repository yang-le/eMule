//this file is part of eMule
//Copyright (C)2003 Merkur ( devs@emule-project.net / http://www.emule-project.net )
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

class CMMSocket;
class CMMData;
class CMMPacket;
class CListenMMSocket;
class CSearchFile;
class CxImage;
class CKnownFile;
class CPartFile;

#define  MMS_BLOCKTIME	600000
#define  MMS_SEARCHID	500

class CMMServer
{
public:
	CMMServer();
	~CMMServer();
	void	Init();
	void	StopServer();
	// packet processing
	bool	PreProcessPacket(char* pPacket, uint32 nSize, CMMSocket* sender);
	void	ProcessHelloPacket(CMMData* data, CMMSocket* sender);
	static void	ProcessStatusRequest(CMMSocket* sender, CMMPacket* packet = NULL);
	void	ProcessFileListRequest(CMMSocket* sender, CMMPacket* packet = NULL);
	void	ProcessFileCommand(CMMData* data, CMMSocket* sender);
	void	ProcessDetailRequest(CMMData* data, CMMSocket* sender);
	void	ProcessCommandRequest(CMMData* data, CMMSocket* sender);
	void	ProcessSearchRequest(CMMData* data, CMMSocket* sender);
	void	ProcessPreviewRequest(CMMData* data, CMMSocket* sender);
	void	ProcessDownloadRequest(CMMData* data, CMMSocket* sender);
	static void	ProcessChangeLimitRequest(CMMData* data, CMMSocket* sender);
	void	ProcessFinishedListRequest(CMMSocket* sender);
	static void	ProcessStatisticsRequest(CMMData* data, CMMSocket* sender);
	// other
	void	SearchFinished(bool bTimeOut);
	void	PreviewFinished(CxImage** imgFrames, uint8 nCount);
	void	Process();
	void	AddFinishedFile(CKnownFile* file)	{m_SentFinishedList.Add(file);}
	CStringA GetContentType() const;

	UINT_PTR h_timer;
	CMMSocket*	m_pPendingCommandSocket;
	uint8	m_byPendingCommand;

protected:
	static VOID CALLBACK CommandTimer(HWND hWnd, UINT nMsg, UINT_PTR nId, DWORD dwTime);
	void	DeleteSearchFiles();
	static void	WriteFileInfo(CPartFile* selFile, CMMPacket* packet);

private:
	CListenMMSocket*	m_pSocket;
	CArray<CPartFile*,CPartFile*>		m_SentFileList;
	CArray<CSearchFile*, CSearchFile*>	m_SendSearchList;
	CArray<CKnownFile*,CKnownFile*>		m_SentFinishedList;
	uint32				m_dwBlocked;
	uint16				m_nSessionID;
	uint16				m_nMaxDownloads;
	uint16				m_nMaxBufDownloads;
	uint8				m_cPWFailed;
	bool				m_bUseFakeContent;
	bool				m_bGrabListLogin;
};