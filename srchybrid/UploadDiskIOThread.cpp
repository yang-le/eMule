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

#include "StdAfx.h"
#include "updownclient.h"
#include "uploaddiskiothread.h"
#include "emule.h"
#include "UploadQueue.h"
#include "sharedfilelist.h"
#include "partfile.h"
#include "knownfile.h"
#include "log.h"
#include "preferences.h"
#include "safefile.h"
#include "listensocket.h"
#include "packets.h"
#include "Statistics.h"
#include "PeerCacheSocket.h"
#include "UploadBandwidthThrottler.h"
#include "zlib/zlib.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define SLOT_COMPRESSION_DATARATE		(1000 * 1024)	// Data rate for a single client from which we start to check if we need to disable compression
#define BIGBUFFER_MINDATARATE			(75 * 1024)

IMPLEMENT_DYNCREATE(CUploadDiskIOThread, CWinThread)

CUploadDiskIOThread::CUploadDiskIOThread()
	: m_eventThreadEnded(FALSE, TRUE)
	, m_hPort()
	, m_bRun()
	, m_bSignalThrottler()
#ifdef _DEBUG
	, dbgDataReadPending()
#endif
{
	ASSERT(theApp.uploadqueue != NULL);
	AfxBeginThread(RunProc, (LPVOID)this);
}

CUploadDiskIOThread::~CUploadDiskIOThread()
{
	ASSERT(!m_hPort);
	ASSERT(!m_bRun);
}

UINT AFX_CDECL CUploadDiskIOThread::RunProc(LPVOID pParam)
{
	DbgSetThreadName("UploadDiskIOThread");
	InitThreadLocale();
	return pParam ? static_cast<CUploadDiskIOThread*>(pParam)->RunInternal() : 1;
}

void CUploadDiskIOThread::EndThread()
{
	m_bRun = false;
	PostQueuedCompletionStatus(m_hPort, 0, 0, NULL);
	m_eventThreadEnded.Lock();
}

UINT CUploadDiskIOThread::RunInternal()
{
	m_hPort = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 1);
	if (!m_hPort)
		return ::GetLastError();

	DWORD dwRead = 0;
	ULONG_PTR completionKey = 0;
	OverlappedRead_Struct *pCurIO = NULL;
	m_bRun = true;
	while (m_bRun
			&& ::GetQueuedCompletionStatus(m_hPort, &dwRead, &completionKey, (LPOVERLAPPED*)&pCurIO, INFINITE)
			&& completionKey)
	{
		//start new I/O
		CCriticalSection *pcsUploadListRead = NULL;
		const CUploadingPtrList &rUploadList = theApp.uploadqueue->GetUploadListTS(&pcsUploadListRead);
		pcsUploadListRead->Lock();
		for (POSITION pos = rUploadList.GetHeadPosition(); pos != NULL;)
			StartCreateNextBlockPackage(rUploadList.GetNext(pos));
		pcsUploadListRead->Unlock();

		//completed I/O
		do {
			if (!completionKey)
				break;
			if (completionKey != (ULONG_PTR)(~0)) //wake up call
				ReadCompletionRoutine(dwRead, pCurIO);
		} while (::GetQueuedCompletionStatus(m_hPort, &dwRead, &completionKey, (LPOVERLAPPED*)&pCurIO, 0));

		//thread termination
		if (!completionKey) // && !dwRead && !pCurIO)
			break;

		// if we have put a new data on any socket, tell the throttler
		if (m_bSignalThrottler && theApp.uploadBandwidthThrottler != NULL) {
			theApp.uploadBandwidthThrottler->NewUploadDataAvailable();
			m_bSignalThrottler = false;
		}
	}
	m_bRun = false; //set non-running state in case of an I/O port error

	//Improper termination of asynchronous I/O follows...
	//close file handles to release the I/O completion port
	while (!m_listPendingIO.IsEmpty())
		ReadCompletionRoutine(0, m_listPendingIO.RemoveHead());

	::CloseHandle(m_hPort);
	m_hPort = 0;

	m_eventThreadEnded.SetEvent();
	return 0;
}

bool CUploadDiskIOThread::AssociateFile(CKnownFile *pFile)
{
	ASSERT(m_hPort && m_bRun);
	if (pFile && pFile->m_hRead == INVALID_HANDLE_VALUE && !pFile->bNoNewReads) {
		CString fullname = (pFile->IsPartFile())
			? RemoveFileExtension(static_cast<const CPartFile*>(pFile)->GetFullName())
			: pFile->GetFilePath();
		pFile->m_hRead = ::CreateFile(fullname, GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		if (pFile->m_hRead == INVALID_HANDLE_VALUE) {
			DebugLogError(_T("Failed to open \"%s\" for overlapped read: %s"), (LPCTSTR)fullname, (LPCTSTR)GetErrorMessage(::GetLastError(), 1));
			return false;
		}
		if (m_hPort != ::CreateIoCompletionPort(pFile->m_hRead, m_hPort, (ULONG_PTR)pFile, 0)) {
			DebugLogError(_T("Failed to associate \"%s\" with reading IOCP: %s"), (LPCTSTR)fullname, (LPCTSTR)GetErrorMessage(::GetLastError(), 1));
			DissociateFile(pFile);
			return false;
		}
		pFile->bCompress = ShouldCompressBasedOnFilename(fullname);
	}
	return true;
}

void CUploadDiskIOThread::DissociateFile(CKnownFile *pFile)
{
	ASSERT(pFile);
	if (pFile->m_hRead != INVALID_HANDLE_VALUE) {
		VERIFY(::CloseHandle(pFile->m_hRead));
		pFile->m_hRead = INVALID_HANDLE_VALUE;
	}
}

void CUploadDiskIOThread::StartCreateNextBlockPackage(UploadingToClient_Struct *pUploadClientStruct)
{
	if (pUploadClientStruct->m_bIOError || pUploadClientStruct->m_BlockRequests_queue.IsEmpty())
		return;
	// when calling this function we already have a lock on the uploadlist
	// (so pUploadClientStruct and its members are safe in terms of not getting deleted/changed)
	CUpDownClient *pClient = pUploadClientStruct->m_pClient;
	CClientReqSocket *pSock = pClient->socket;
	if (pSock == NULL
		|| !pSock->IsConnected()
		|| (pClient->IsUploadingToPeerCache() && pClient->m_pPCUpSocket == NULL))
	{
		return;
	}

	// now also get a lock on the Block lists
	CSingleLock lockBlockLists(&pUploadClientStruct->m_csBlockListsLock, TRUE);
	// See if we can do an early return.
	// There may be no new blocks to load from disk and add to buffer, or buffer may be large enough already.

	uint64 nCurQueueSessionPayloadUp = pClient->GetQueueSessionPayloadUp();
	// GetQueueSessionPayloadUp is probably outdated so also add the value reported by the sockets as sent
	nCurQueueSessionPayloadUp += pSock->GetSentPayloadSinceLastCall(false);
	uint64 addedPayloadQueueSession = pClient->GetQueueSessionUploadAdded();
	// buffer at least 1 block (180KB) for normal uploads, and 5 blocks (~900KB) for fast uploads
	const uint32 nBufferLimit = (pClient->GetUploadDatarate() > BIGBUFFER_MINDATARATE) ? (5 * EMBLOCKSIZE + 1) : (EMBLOCKSIZE + 1);

	if (addedPayloadQueueSession > nCurQueueSessionPayloadUp && addedPayloadQueueSession - nCurQueueSessionPayloadUp >= nBufferLimit)
		return; // the buffered data is large enough already

	try {
		// Get more data if currently buffered was less than nBufferLimit Bytes
		while (!pUploadClientStruct->m_BlockRequests_queue.IsEmpty()
			&& (addedPayloadQueueSession <= nCurQueueSessionPayloadUp || addedPayloadQueueSession - nCurQueueSessionPayloadUp < nBufferLimit))
		{
			Requested_Block_Struct *currentblock = pUploadClientStruct->m_BlockRequests_queue.GetHead();
			if (!md4equ(currentblock->FileID, pClient->GetUploadFileID())) {
				// the UploadFileID differs. That's normally not a problem, we just switch it, but
				// we don't want to do so in this thread for synchronization issues. So return and wait
				// until the main thread which checks the block request periodically does so.
				// This should happen very rarely anyway so no problem performance wise
				theApp.QueueDebugLogLine(false, _T("CUploadDiskIOThread::StartCreateNextBlockPackage: Switched fileid, waiting for the main thread"));
				return;
			}

			CKnownFile *pFile = theApp.sharedfiles->GetFileByID(currentblock->FileID);
			if (pFile == NULL)
				throwCStr(_T("CUploadDiskIOThread::StartCreateNextBlockPackage: shared file not found"));
			if (pFile->bNoNewReads) //should be moving to incoming
				return;

			// we already have done all important sanity checks for the block request in the main thread when adding it; just redo some quick important ones
			if (currentblock->StartOffset >= currentblock->EndOffset || currentblock->EndOffset > pFile->GetFileSize())
				throwCStr(_T("Invalid Block Offsets"));
			uint64 uTogo = currentblock->EndOffset - currentblock->StartOffset;
			if (uTogo > EMBLOCKSIZE * 3)
				throw GetResString(IDS_ERR_LARGEREQBLOCK);

			if (!AssociateFile(pFile))
				throwCStr(_T("StartCreateNextBlockPackage: cannot open CKnownFile"));

			// initiate read
			OverlappedRead_Struct *pOverlappedRead = new OverlappedRead_Struct;
			pOverlappedRead->oOverlap.Internal = 0;
			pOverlappedRead->oOverlap.InternalHigh = 0;
			//pOverlappedRead->oOverlap.Offset = LODWORD(currentblock->StartOffset);
			//pOverlappedRead->oOverlap.OffsetHigh = HIDWORD(currentblock->StartOffset);
			*(uint64*)&pOverlappedRead->oOverlap.Offset = currentblock->StartOffset;
			pOverlappedRead->oOverlap.hEvent = 0;
			pOverlappedRead->pFile = pFile;
			pOverlappedRead->pUploadClientStruct = pUploadClientStruct;
			pOverlappedRead->uStartOffset = currentblock->StartOffset;
			pOverlappedRead->uEndOffset = currentblock->EndOffset;
			pOverlappedRead->pBuffer = new byte[(size_t)uTogo];

			if (!::ReadFile(pFile->m_hRead, pOverlappedRead->pBuffer, (DWORD)uTogo, NULL, (LPOVERLAPPED)pOverlappedRead)) {
				DWORD dwError = ::GetLastError();
				if (dwError != ERROR_IO_PENDING) {
					delete[] pOverlappedRead->pBuffer;
					delete pOverlappedRead;

					if (dwError == ERROR_INVALID_USER_BUFFER || dwError == ERROR_NOT_ENOUGH_MEMORY || dwError == ERROR_NOT_ENOUGH_QUOTA) {
						theApp.QueueDebugLogLineEx(LOG_WARNING, _T("ReadFile failed, possibly too many pending requests, trying again later"));
						return; // make this a recoverable error, as it might just be that we have too many requests in which case we just need to wait
					}
					throw _T("ReadFile Error: ") + GetErrorMessage(::GetLastError());
				}
			}
			++pFile->nInUse;
			pOverlappedRead->pos = m_listPendingIO.AddTail(pOverlappedRead);
			DEBUG_ONLY(dbgDataReadPending += uTogo);

			addedPayloadQueueSession += uTogo;
			pClient->SetQueueSessionUploadAdded(addedPayloadQueueSession);
			pUploadClientStruct->m_DoneBlocks_list.AddHead(pUploadClientStruct->m_BlockRequests_queue.RemoveHead());
		}
		return; //no errors
	} catch (const CString &error) {
		theApp.QueueDebugLogLineEx(LOG_ERROR, GetResString(IDS_ERR_CLIENTERRORED), pClient->GetUserName(), (LPCTSTR)error);
	} catch (CFileException *e) {
		TCHAR szError[MAX_PATH + 256];
		GetExceptionMessage(*e, szError, _countof(szError));
		theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Failed to create upload package for %s - %s"), pClient->GetUserName(), szError);
		e->Delete();
	}
	pUploadClientStruct->m_bIOError = true; // will let remove this client from the list in the main thread
}

void CUploadDiskIOThread::ReadCompletionRoutine(DWORD dwRead, const OverlappedRead_Struct *pOvRead)
{
	if (pOvRead == NULL) {
		ASSERT(0);
		return;
	}
	ASSERT(pOvRead->pFile && pOvRead->pos);

	--pOvRead->pFile->nInUse;

	CKnownFile *pKnownFile = pOvRead->pFile;
	if (m_bRun) {
		m_listPendingIO.RemoveAt(pOvRead->pos);
		bool bReadError = !dwRead;
		if (bReadError)
			Debug(_T("  Completed read, dwRead=0\n"));
		else if (dwRead != pOvRead->uEndOffset - pOvRead->uStartOffset) {
			theApp.QueueDebugLogLineEx(LOG_ERROR, _T("ReadCompletionRoutine: Didn't read requested data count - wanted: %lu, read: %lu")
				, (DWORD)(pOvRead->uEndOffset - pOvRead->uStartOffset), dwRead);
			bReadError = true;
		}
		if (pKnownFile->m_hRead != INVALID_HANDLE_VALUE) { //discard data from closed files
			UploadingToClient_Struct *pStruct = pOvRead->pUploadClientStruct;
			DEBUG_ONLY(dbgDataReadPending -= pOvRead->uEndOffset - pOvRead->uStartOffset);
			// check if the client struct is still in the upload list (otherwise it is a deleted pointer)
			CCriticalSection *pcsUploadListRead = NULL;
			const CUploadingPtrList &rUploadList = theApp.uploadqueue->GetUploadListTS(&pcsUploadListRead);
			CSingleLock lockUploadListRead(pcsUploadListRead, TRUE);
			ASSERT(lockUploadListRead.IsLocked());
			bool bFound = (rUploadList.Find(pStruct) != NULL);

			// all important prechecks done, create the packets
			if (bFound && !bReadError) {
				// Keep the uploadlist locked while working with the client object.
				// Instead of sending the packets immediately, we store them
				// and send after we have released the uploadlist lock -
				// just to be sure there is no chance of a deadlock (now or in future version, also it doesn't cost us much)
				CPacketList packetsList;
				CUpDownClient *pClient = pStruct->m_pClient;
				bool bPeerCache = pClient->IsUploadingToPeerCache();
				CClientReqSocket *pSocket = bPeerCache ? pClient->m_pPCUpSocket : pClient->socket;
				if (pSocket && (bPeerCache || pSocket->IsConnected())) {
					// Try to use compression whenever possible (see CreatePackedPackets notes)
					if (bPeerCache)
						CreatePeerCachePackets(*pOvRead, packetsList);
					else if (pKnownFile->bCompress)
						CreatePackedPackets(*pOvRead, packetsList);
					else
						CreateStandardPackets(*pOvRead, packetsList);

					m_bSignalThrottler = true;
				} else
					theApp.QueueDebugLogLineEx(LOG_ERROR, _T("ReadCompletionRoutine: Client has no connected socket, %s"), (LPCTSTR)pClient->DbgGetClientInfo(true));

				lockUploadListRead.Unlock();

				// now send out all packets we have made. By default, our socket object is not safe
				// to use now either, because we have no lock on itself (to make sure it is not deleted)
				// however the way we handle sockets, they cannot get deleted directly (takes 10s),
				// so this isn't a problem in our case
				while (!packetsList.IsEmpty() && pSocket != NULL) {
					Packet *packet = packetsList.RemoveHead();
					pSocket->SendPacket(packet, false, packet->uStatsPayLoad);
				}
			} else { // bReadError is true
				if (bFound)
					pStruct->m_bIOError = true;
				else
					theApp.QueueDebugLogLineEx(LOG_WARNING, _T("ReadCompletionRoutine: Client not found in uploadlist when reading finished; discarding block"));
				lockUploadListRead.Unlock();
			}
		}
	} else if (pKnownFile)
		DissociateFile(pKnownFile);

	// cleanup
	delete[] pOvRead->pBuffer;
	delete pOvRead;
}

bool CUploadDiskIOThread::ShouldCompressBasedOnFilename(const CString &strFileName)
{
	LPCTSTR pDot = ::PathFindExtension(strFileName);
	CString strExt(pDot + static_cast<int>(*pDot != _T('\0')));
	strExt.MakeLower();
	if (strExt == _T("avi"))
		return !thePrefs.GetDontCompressAvi();
	return strExt != _T("zip") && strExt != _T("rar") && strExt != _T("7z") && strExt != _T("cbz") && strExt != _T("cbr") && strExt != _T("ace") && strExt != _T("ogm");
}

void CUploadDiskIOThread::CreateStandardPackets(const OverlappedRead_Struct &OverlappedRead, CPacketList &rOutPacketList)
{
	const uchar *pucMD4FileHash = OverlappedRead.pFile->GetFileHash();
	bool bIsPartFile = OverlappedRead.pFile->IsPartFile();
	CString sDbgClientInfo;
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		sDbgClientInfo = OverlappedRead.pUploadClientStruct->m_pClient->DbgGetClientInfo(true);

	uint32 togo = (uint32)(OverlappedRead.uEndOffset - OverlappedRead.uStartOffset);
	CMemFile memfile((BYTE*)OverlappedRead.pBuffer, togo);
	while (togo) {
		uint32 nPacketSize = (togo < 13000) ? togo : 10240u;
		togo -= nPacketSize;

		uint64 endpos = OverlappedRead.uEndOffset - togo;
		uint64 startpos = endpos - nPacketSize;

		LPCTSTR sOp;
		Packet *packet;
		if (endpos > _UI32_MAX) {
			packet = new Packet(OP_SENDINGPART_I64, nPacketSize + 32, OP_EMULEPROT, bIsPartFile);
			md4cpy(&packet->pBuffer[0], pucMD4FileHash);
			PokeUInt64(&packet->pBuffer[16], startpos);
			PokeUInt64(&packet->pBuffer[24], endpos);
			memfile.Read(&packet->pBuffer[32], nPacketSize);
			theStats.AddUpDataOverheadFileRequest(32);
			sOp = _T("OP_SendingPart_I64");
		} else {
			packet = new Packet(OP_SENDINGPART, nPacketSize + 24, OP_EDONKEYPROT, bIsPartFile);
			md4cpy(&packet->pBuffer[0], pucMD4FileHash);
			PokeUInt32(&packet->pBuffer[16], (uint32)startpos);
			PokeUInt32(&packet->pBuffer[20], (uint32)endpos);
			memfile.Read(&packet->pBuffer[24], nPacketSize);
			theStats.AddUpDataOverheadFileRequest(24);
			sOp = _T("OP_SendingPart");
		}
		if (thePrefs.GetDebugClientTCPLevel() > 0) {
			Debug(_T(">>> %-20s to   %s; %s\n"), sOp, (LPCTSTR)sDbgClientInfo, (LPCTSTR)md4str(pucMD4FileHash));
			Debug(_T("  Start=%I64u  End=%I64u  Size=%u\n"), startpos, endpos, nPacketSize);
		}
		packet->uStatsPayLoad = nPacketSize;
		rOutPacketList.AddTail(packet);
	}
}

void CUploadDiskIOThread::CreatePackedPackets(const OverlappedRead_Struct &OverlappedRead, CPacketList &rOutPacketList)
{
	const uint64 uStartOffset = OverlappedRead.uStartOffset;
	const uint64 uEndOffset = OverlappedRead.uEndOffset;
	const uchar *pucMD4FileHash = OverlappedRead.pFile->GetFileHash();
	bool bIsPartFile = OverlappedRead.pFile->IsPartFile();

	uint32 togo = (uint32)(uEndOffset - uStartOffset);
	uLongf newsize = togo + 300;
	BYTE *output = new BYTE[newsize];

	// Use the lowest compression level 1 instead of the highest 9 because typically for 10240 blocks:
	// - compressed size difference is usually small enough (~4% for .exe, .avi, .pdf and 12% for .c text)
	// - time was 1.5-2.5 better
	// Of course, throughput of the deflate() routine depends on processor and data bytes,
	// but should not be the bottleneck because 50-70 MB/s rates were seen when using one CPU core.
	if (compress2(output, &newsize, OverlappedRead.pBuffer, togo, 1) != Z_OK || togo <= newsize) {
		delete[] output;
		CreateStandardPackets(OverlappedRead, rOutPacketList);
		return;
	}
	CString sDbgClientInfo;
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		sDbgClientInfo = OverlappedRead.pUploadClientStruct->m_pClient->DbgGetClientInfo(true);

	CMemFile memfile(output, newsize);
	uint32 oldSize = togo;
	togo = newsize;

	uint32 totalPayloadSize = 0;

	while (togo) {
		uint32 nPacketSize = (togo < 13000) ? togo : 10240u;
		togo -= nPacketSize;
		Packet *packet;
		LPCTSTR sOp;
		if (uEndOffset > UINT32_MAX) {
			packet = new Packet(OP_COMPRESSEDPART_I64, nPacketSize + 28, OP_EMULEPROT, bIsPartFile);
			md4cpy(&packet->pBuffer[0], pucMD4FileHash);
			PokeUInt64(&packet->pBuffer[16], uStartOffset);
			PokeUInt32(&packet->pBuffer[24], newsize);
			memfile.Read(&packet->pBuffer[28], nPacketSize);
			sOp = _T("OP_CompressedPart_I64");
		} else {
			packet = new Packet(OP_COMPRESSEDPART, nPacketSize + 24, OP_EMULEPROT, bIsPartFile);
			md4cpy(&packet->pBuffer[0], pucMD4FileHash);
			PokeUInt32(&packet->pBuffer[16], (uint32)uStartOffset);
			PokeUInt32(&packet->pBuffer[20], newsize);
			memfile.Read(&packet->pBuffer[24], nPacketSize);
			sOp = _T("OP_CompressedPart");
		}

		if (thePrefs.GetDebugClientTCPLevel() > 0) {
			Debug(_T(">>> %-20s to   %s; %s\n"), sOp, (LPCTSTR)sDbgClientInfo, (LPCTSTR)md4str(pucMD4FileHash));
			Debug(_T("  Start=%I64u  BlockSize=%u  Size=%u\n"), uStartOffset, newsize, nPacketSize);
		}
		// approximate payload size
		uint32 payloadSize = togo ? nPacketSize * oldSize / newsize : oldSize - totalPayloadSize;

		totalPayloadSize += payloadSize;

		theStats.AddUpDataOverheadFileRequest(24);
		packet->uStatsPayLoad = payloadSize;
		rOutPacketList.AddTail(packet);
	}
	memfile.Close();
	delete[] output;
}

// not tested for the new threaded upload, if we ever use the PeerCache code again, take a look before
void CUploadDiskIOThread::CreatePeerCachePackets(const OverlappedRead_Struct &OverlappedRead, CPacketList &rOutPacketList)
{
	byte *pbyData = OverlappedRead.pBuffer;
	uint64 uFileSize = (uint64)OverlappedRead.pFile->GetFileSize();
	bool bIsPartFile = OverlappedRead.pFile->IsPartFile();
	uint32 togo = (uint32)(OverlappedRead.uEndOffset - OverlappedRead.uStartOffset);
	CUpDownClient *pClient = OverlappedRead.pUploadClientStruct->m_pClient;
	ASSERT(uFileSize);

	while (togo) {
		uint32 nPacketSize = (togo < 13000) ? togo : 10240u;
		togo -= nPacketSize;

		CSafeMemFile dataHttp(nPacketSize + 512); //additional space for an http header
		if (pClient->GetHttpSendState() == 0) {
			CStringA str;
			str.Format("HTTP/1.0 206\r\n"
				"Content-Range: bytes %I64u-%I64u/%I64u\r\n"
				"Content-Type: application/octet-stream\r\n"
				"Content-Length: %u\r\n"
				"Server: eMule/%S\r\n"
				"\r\n"
				, OverlappedRead.uStartOffset, OverlappedRead.uEndOffset - 1, uFileSize
				, (unsigned)(OverlappedRead.uEndOffset - OverlappedRead.uStartOffset)
				, (LPCTSTR)theApp.m_strCurVersionLong);

			dataHttp.Write((LPCSTR)str, str.GetLength());
			theStats.AddUpDataOverheadFileRequest((uint32)dataHttp.GetLength());

			pClient->SetHttpSendState(1);
			/*if (thePrefs.GetDebugClientTCPLevel() > 0) {
				DebugSend("PeerCache-HTTP", this, pucMD4FileHash);
				Debug(_T("  %hs\n"), str);
			}*/
		}
		dataHttp.Write(pbyData, nPacketSize);
		pbyData += nPacketSize;

		/*if (thePrefs.GetDebugClientTCPLevel() > 1) {
			DebugSend("PeerCache-HTTP data", this, GetUploadFileID());
			Debug(_T("  Start=%I64u  End=%I64u  Size=%u\n"), startpos, endpos, nPacketSize);
		}*/

		char *pRawPacketData = (char*)dataHttp.Detach();
		CRawPacket *packet = new CRawPacket(pRawPacketData, (UINT)dataHttp.GetLength(), bIsPartFile);
		packet->uStatsPayLoad = nPacketSize;
		rOutPacketList.AddTail(packet);
		free(pRawPacketData);
	}
}

void CUploadDiskIOThread::WakeUpCall()
{
	if (m_bRun)
		PostQueuedCompletionStatus(m_hPort, 0, (ULONG_PTR)(~0), NULL);
}