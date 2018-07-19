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
#ifdef _DEBUG
#include "DebugHelpers.h"
#endif
#include "emule.h"
#include <timeapi.h>
#include "emsocket.h"
#include "AsyncProxySocketLayer.h"
#include "Packets.h"
#include "OtherFunctions.h"
#include "UploadBandwidthThrottler.h"
#include "Preferences.h"
#include "emuleDlg.h"
#include "Log.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


namespace
{
	inline void EMTrace(char* fmt, ...)
	{
#ifdef EMSOCKET_DEBUG
		va_list argptr;
		char bufferline[512];
		va_start(argptr, fmt);
		_vsnprintf(bufferline, 512, fmt, argptr);
		va_end(argptr);
		//(Ornis+)
		char osDate[30], osTime[30];
		char temp[1024];
		_strtime(osTime);
		_strdate(osDate);
		int len = _snprintf(temp, 1021, "%s %s: %s\r\n", osDate, osTime, bufferline);
		if (len > 0) {
			HANDLE hFile = CreateFile("c:\\EMSocket.log",	// open MYFILE.TXT
				GENERIC_WRITE,			// open for reading
				FILE_SHARE_READ,		// share for reading
				NULL,					// no security
				OPEN_ALWAYS,			// existing file only
				FILE_ATTRIBUTE_NORMAL,  // normal file
				NULL);					// no attr. template

			if (hFile != INVALID_HANDLE_VALUE) {
				DWORD nbBytesWritten = 0;
				SetFilePointer(hFile, 0, NULL, FILE_END);
				BOOL b = WriteFile(
					hFile,				// handle to file
					temp,				// data buffer
					len,				// number of bytes to write
					&nbBytesWritten,	// number of bytes written
					NULL				// overlapped buffer
				);
				CloseHandle(hFile);
			}
		}
#else
		//va_list argptr;
		//va_start(argptr, fmt);
		//va_end(argptr);
		UNREFERENCED_PARAMETER(fmt);
#endif //EMSOCKET_DEBUG
	}
}

IMPLEMENT_DYNAMIC(CEMSocket, CEncryptedStreamSocket)

CEMSocket::CEMSocket()
	: pendingHeader(), m_OverlappedCleaning(), lastFinishedStandard()
{
	byConnected = ES_NOTCONNECTED;
	m_uTimeOut = CONNECTION_TIMEOUT; // default timeout for ed2k sockets

	// Download (pseudo) rate control
	downloadLimit = 0;
	downloadLimitEnable = false;
	pendingOnReceive = false;

	// Download partial header
	pendingHeaderSize = 0;

	// Download partial packet
	pendingPacket = NULL;
	pendingPacketSize = 0;

	// Upload control
	sendbuffer = NULL;
	sendblen = 0;
	sent = 0;
	//m_bLinkedPackets = false;

	// deadlake PROXYSUPPORT
	m_pProxyLayer = NULL;
	m_bProxyConnectFailed = false;

	//m_startSendTick = 0;
	//m_lastSendLatency = 0;
	//m_latency_sum = 0;
	//m_wasBlocked = false;

	m_currentPacket_is_controlpacket = false;
	m_currentPackageIsFromPartFile = false;

	m_numberOfSentBytesCompleteFile = 0;
	m_numberOfSentBytesPartFile = 0;
	m_numberOfSentBytesControlPacket = 0;

	lastCalledSend = timeGetTime();
	lastSent = lastCalledSend > SEC2MS(1) ? lastCalledSend - SEC2MS(1) : 0;

	m_bAccelerateUpload = false;

	m_actualPayloadSize = 0;
	m_actualPayloadSizeSent = 0;

	m_bBusy = false;
	m_hasSent = false;
	m_bUsesBigSendBuffers = false;
	m_pPendingSendOperation = NULL;
	m_bOverlappedSending = thePrefs.GetUseOverlappedSockets() && theApp.IsWinSock2Available();
}

CEMSocket::~CEMSocket()
{
	EMTrace("CEMSocket::~CEMSocket() on %u", (SOCKET)this);

	// need to be locked here to know that the other methods
	// won't be in the middle of things
	sendLocker.Lock();
	byConnected = ES_DISCONNECTED;
	CancelIo((HANDLE)GetSocketHandle());
	CleanUpOverlappedSendOperation();
	sendLocker.Unlock();

	// now that we know no other method will keep adding to the queue
	// we can remove ourself from the queue
	theApp.uploadBandwidthThrottler->RemoveFromAllQueues(this);

	ClearQueues();
	CEMSocket::RemoveAllLayers(); // deadlake PROXYSUPPORT
	AsyncSelect(0);
}

// deadlake PROXYSUPPORT
// By Maverick: Connection initialization is done by class itself
bool CEMSocket::Connect(const CString& sHostAddress, UINT nHostPort)
{
	InitProxySupport();
	return CEncryptedStreamSocket::Connect(sHostAddress, nHostPort);
}
// end deadlake

// deadlake PROXYSUPPORT
// By Maverick: Connection initialization is done by class itself
BOOL CEMSocket::Connect(const LPSOCKADDR pSockAddr, int iSockAddrLen)
{
	InitProxySupport();
	return CEncryptedStreamSocket::Connect(pSockAddr, iSockAddrLen);
}
// end deadlake

void CEMSocket::InitProxySupport()
{
	m_bProxyConnectFailed = false;

	// ProxyInitialization
	const ProxySettings& settings = thePrefs.GetProxySettings();
	if (settings.UseProxy && settings.type != PROXYTYPE_NOPROXY) {
		m_bOverlappedSending = false;
		Close();

		m_pProxyLayer = new CAsyncProxySocketLayer;
		switch (settings.type) {
		case PROXYTYPE_SOCKS4:
		case PROXYTYPE_SOCKS4A:
			m_pProxyLayer->SetProxy(settings.type, settings.name, settings.port);
			break;
		case PROXYTYPE_SOCKS5:
		case PROXYTYPE_HTTP10:
		case PROXYTYPE_HTTP11:
			if (settings.EnablePassword)
				m_pProxyLayer->SetProxy(settings.type, settings.name, settings.port, settings.user, settings.password);
			else
				m_pProxyLayer->SetProxy(settings.type, settings.name, settings.port);
			break;
		default:
			ASSERT(0);
		}
		AddLayer(m_pProxyLayer);

		// Connection Initialization
		Create(0, SOCK_STREAM, FD_SIX_EVENTS, thePrefs.GetBindAddr());
		AsyncSelect(FD_SIX_EVENTS);
	}
}

void CEMSocket::ClearQueues()
{
	EMTrace("CEMSocket::ClearQueues on %u", (SOCKET)this);

	sendLocker.Lock();
	while (!controlpacket_queue.IsEmpty())
		delete controlpacket_queue.RemoveHead();

	while (!standardpacket_queue.IsEmpty())
		delete standardpacket_queue.RemoveHead().packet;
	sendLocker.Unlock();

	// Download (pseudo) rate control
	downloadLimit = 0;
	downloadLimitEnable = false;
	pendingOnReceive = false;

	// Download partial header
	pendingHeaderSize = 0;

	// Download partial packet
	delete pendingPacket;
	pendingPacket = NULL;
	pendingPacketSize = 0;

	// Upload control
	delete[] sendbuffer;
	sendbuffer = NULL;

	sendblen = 0;
	sent = 0;
}

void CEMSocket::OnClose(int nErrorCode)
{
	// need to be locked here to know that the other methods
	// won't be in the middle of things
	sendLocker.Lock();
	byConnected = ES_DISCONNECTED;
	sendLocker.Unlock();

	// now that we know no other method will keep adding to the queue
	// we can remove ourself from the queue
	theApp.uploadBandwidthThrottler->RemoveFromAllQueues(this);

	CEncryptedStreamSocket::OnClose(nErrorCode); // deadlake changed socket to PROXYSUPPORT ( AsyncSocketEx )
	RemoveAllLayers(); // deadlake PROXYSUPPORT
	ClearQueues();
}

BOOL CEMSocket::AsyncSelect(long lEvent)
{
#ifdef EMSOCKET_DEBUG
	if (lEvent&FD_READ)
		EMTrace("  FD_READ");
	if (lEvent&FD_CLOSE)
		EMTrace("  FD_CLOSE");
	if (lEvent&FD_WRITE)
		EMTrace("  FD_WRITE");
#endif
	// deadlake changed to AsyncSocketEx PROXYSUPPORT
	return (m_SocketData.hSocket == INVALID_SOCKET) || CEncryptedStreamSocket::AsyncSelect(lEvent);
}

void CEMSocket::OnReceive(int nErrorCode)
{
	// the 2 meg size was taken from another place
	static char GlobalReadBuffer[2000000];

	// Check for an error code
	if (nErrorCode != 0 && nErrorCode != WSAESHUTDOWN) {
		OnError(nErrorCode);
		return;
	}

	// Check current connection state
	if (byConnected == ES_DISCONNECTED)
		return;

	byConnected = ES_CONNECTED; // ES_DISCONNECTED, ES_NOTCONNECTED, ES_CONNECTED

	// CPU load improvement
	if (downloadLimitEnable && downloadLimit == 0 && nErrorCode != WSAESHUTDOWN) {
		EMTrace("CEMSocket::OnReceive blocked by limit");
		pendingOnReceive = true;
		return;
	}

	// Remark: an overflow can not occur here
	size_t readMax = sizeof GlobalReadBuffer - pendingHeaderSize;
	if (downloadLimitEnable && readMax > downloadLimit && nErrorCode != WSAESHUTDOWN)
		readMax = downloadLimit;

	// We attempt to read up to 2 megs at a time (minus whatever is in our internal read buffer)
	int ret = Receive(GlobalReadBuffer + pendingHeaderSize, (int)readMax);
	if (ret == SOCKET_ERROR || byConnected == ES_DISCONNECTED)
		return;

	// Bandwidth control
	if (downloadLimitEnable)
		// Update limit
		downloadLimit -= GetRealReceivedBytes();

	// CPU load improvement
	// Detect if the socket's buffer is empty (or the size did match...)
	pendingOnReceive = m_bFullReceive;

	if (ret == 0)
		return;

	// Copy back the partial header into the global read buffer for processing
	if (pendingHeaderSize > 0) {
		memcpy(GlobalReadBuffer, pendingHeader, pendingHeaderSize);
		ret += (int)pendingHeaderSize;
		pendingHeaderSize = 0;
	}

	if (IsRawDataMode()) {
		DataReceived((BYTE*)GlobalReadBuffer, ret);
		return;
	}

	char *rptr = GlobalReadBuffer; // floating index initialized with begin of buffer
	const char *rend = GlobalReadBuffer + ret; // end of buffer
	// Loop, processing packets until we run out of them
	while (rend >= rptr + PACKET_HEADER_SIZE || (pendingPacket != NULL && rend > rptr)) {
		// Two possibilities here:
		//
		// 1. There is no pending incoming packet
		// 2. There is already a partial pending incoming packet
		//
		// It's important to remember that emule exchange two kinds of packet
		// - The control packet
		// - The data packet for the transport of the block
		//
		// The biggest part of the traffic is done with the data packets.
		// The default size of one block is 10240 bytes (or less if compressed), but the
		// maximal size for one packet on the network is 1300 bytes. It's the reason
		// why most of the Blocks are splitted before to be sent.
		//
		// Conclusion: When the download limit is disabled, this method can be at least
		// called 8 times (10240/1300) by the lower layer before a splitted packet is
		// rebuild and transferred to the above layer for processing.
		//
		// The purpose of this algorithm is to limit the amount of data exchanged between buffers

		if (pendingPacket == NULL) {
			pendingPacket = new Packet(rptr);	// Create new packet container.
			rptr += PACKET_HEADER_SIZE;			// Only the header is initialized so far

			// Bugfix We still need to check for a valid protocol
			// Remark: the default eMule v0.26b had removed this test......
			switch (pendingPacket->prot) {
			case OP_EDONKEYPROT:
			case OP_PACKEDPROT:
			case OP_EMULEPROT:
				break;
			default:
				EMTrace("CEMSocket::OnReceive ERROR Wrong header");
				delete pendingPacket;
				pendingPacket = NULL;
				OnError(ERR_WRONGHEADER);
				return;
			}

			// Security: Check for buffer overflow (2MB)
			if (pendingPacket->size > sizeof GlobalReadBuffer) {
				delete pendingPacket;
				pendingPacket = NULL;
				OnError(ERR_TOOBIG);
				return;
			}

			// Init data buffer
			pendingPacket->pBuffer = new char[pendingPacket->size + 1];
			pendingPacketSize = 0;
		}

		// Bytes ready to be copied into packet's internal buffer
		ASSERT(rptr <= rend);
		uint32 toCopy = ((pendingPacket->size - pendingPacketSize) < (uint32)(rend - rptr)) ?
			(pendingPacket->size - pendingPacketSize) : (uint32)(rend - rptr);

// Copy Bytes from Global buffer to packet's internal buffer
		memcpy(&pendingPacket->pBuffer[pendingPacketSize], rptr, toCopy);
		pendingPacketSize += toCopy;
		rptr += toCopy;

		// Check if packet is complete
		ASSERT(pendingPacket->size >= pendingPacketSize);
		if (pendingPacket->size == pendingPacketSize) {
#ifdef EMSOCKET_DEBUG
			EMTrace("CEMSocket::PacketReceived on %d, opcode=%X, realSize=%d",
				(SOCKET)this, pendingPacket->opcode, pendingPacket->GetRealPacketSize());
#endif

			// Process packet
			bool bPacketResult = PacketReceived(pendingPacket);
			delete pendingPacket;
			pendingPacket = NULL;
			pendingPacketSize = 0;

			if (!bPacketResult)
				return;
		}
	}

	// Finally, if there is any data left over, save it for next time
	ASSERT(rptr <= rend);
	ASSERT(rend - rptr < PACKET_HEADER_SIZE);
	if (rptr < rend) {
		// Keep the partial head
		pendingHeaderSize = rend - rptr;
		memcpy(pendingHeader, rptr, pendingHeaderSize);
	}
}

void CEMSocket::SetDownloadLimit(uint32 limit)
{
	downloadLimit = limit;
	downloadLimitEnable = true;

	// CPU load improvement
	if (limit > 0 && pendingOnReceive)
		OnReceive(0);
}

void CEMSocket::DisableDownloadLimit()
{
	downloadLimitEnable = false;

	// CPU load improvement
	if (pendingOnReceive)
		OnReceive(0);
}

/**
 * Queues up the packet to be sent. Another thread will actually send the packet.
 *
 * If the packet is not a control packet, and if the socket decides that its queue is
 * full and forceAdd is false, then the socket is allowed to refuse to add the packet
 * to its queue. It will then return false and it is up to the calling thread to try
 * to call SendPacket for that packet again at a later time.
 *
 * @param packet address to the packet that should be added to the queue
 *
 * @param delpacket if true, the responsibility for deleting the packet after it has been sent
 *					has been transferred to this object. If false, don't delete the packet after it
 *					has been sent.
 *
 * @param controlpacket the packet is a controlpacket
 *
 * @param forceAdd this packet must be added to the queue, even if it is full. If this flag is true
 *					then the method can not refuse to add the packet, and therefore not return false.
 *
 * @return true if the packet was added to the queue, false otherwise
 */
void CEMSocket::SendPacket(Packet *packet, bool delpacket, bool controlpacket, uint32 actualPayloadSize, bool bForceImmediateSend)
{
	//EMTrace("CEMSocket::OnSenPacked1 linked: %i, controlcount %i, standardcount %i, isbusy: %i",m_bLinkedPackets, controlpacket_queue.GetCount(), standardpacket_queue.GetCount(), IsBusy());

	if (byConnected == ES_DISCONNECTED) {
		if (delpacket)
			delete packet;
		return;
	}
	if (!delpacket) {
		//ASSERT ( !packet->IsSplitted() );
		Packet *copy = new Packet(packet->opcode, packet->size);
		memcpy(copy->pBuffer, packet->pBuffer, packet->size);
		packet = copy;
	}

	//if(m_startSendTick > 0) {
	//	m_lastSendLatency = timeGetTime() - m_startSendTick;
	//}
	sendLocker.Lock();
	if (controlpacket) {
		controlpacket_queue.AddTail(packet);

		// queue up for controlpacket
		theApp.uploadBandwidthThrottler->QueueForSendingControlPacket(this, m_hasSent);
	} else {
		bool first = !((sendbuffer && !m_currentPacket_is_controlpacket) || !standardpacket_queue.IsEmpty());
		standardpacket_queue.AddTail(StandardPacketQueueEntry{packet, actualPayloadSize});

		// reset timeout for the first time
		if (first) {
			lastFinishedStandard = timeGetTime();
			m_bAccelerateUpload = true;	// Always accelerate first packet in a block
		}
	}
	sendLocker.Unlock();

	if (bForceImmediateSend) {
		ASSERT(controlpacket_queue.GetSize() == 1);
		Send(1024, 0, true);
	}
}

uint64 CEMSocket::GetSentBytesCompleteFileSinceLastCallAndReset()
{
	sendLocker.Lock();

	uint64 sentBytes = m_numberOfSentBytesCompleteFile;
	m_numberOfSentBytesCompleteFile = 0;

	sendLocker.Unlock();

	return sentBytes;
}

uint64 CEMSocket::GetSentBytesPartFileSinceLastCallAndReset()
{
	sendLocker.Lock();

	uint64 sentBytes = m_numberOfSentBytesPartFile;
	m_numberOfSentBytesPartFile = 0;

	sendLocker.Unlock();

	return sentBytes;
}

uint64 CEMSocket::GetSentBytesControlPacketSinceLastCallAndReset()
{
	sendLocker.Lock();

	uint64 sentBytes = m_numberOfSentBytesControlPacket;
	m_numberOfSentBytesControlPacket = 0;

	sendLocker.Unlock();

	return sentBytes;
}

uint64 CEMSocket::GetSentPayloadSinceLastCall(bool bReset)
{
	if (!bReset)
		return m_actualPayloadSizeSent;
	sendLocker.Lock();

	uint64 sentBytes = m_actualPayloadSizeSent;
	m_actualPayloadSizeSent = 0;

	sendLocker.Unlock();

	return sentBytes;
}

void CEMSocket::OnSend(int nErrorCode)
{
	//onSendWillBeCalledOuter = false;

	if (nErrorCode) {
		OnError(nErrorCode);
		return;
	}

	//EMTrace("CEMSocket::OnSend linked: %i, controlcount %i, standardcount %i, isbusy: %i",m_bLinkedPackets, controlpacket_queue.GetCount(), standardpacket_queue.GetCount(), IsBusy());
	CEncryptedStreamSocket::OnSend(0);

	m_bBusy = false;

	// stopped sending here.
	//StoppedSendSoUpdateStats();

	if (byConnected == ES_DISCONNECTED)
		return;

	byConnected = ES_CONNECTED;

	if (m_currentPacket_is_controlpacket) {
		// queue up for control packet
		theApp.uploadBandwidthThrottler->QueueForSendingControlPacket(this, m_hasSent);
	}

	if (!m_bOverlappedSending && (!standardpacket_queue.IsEmpty() || sendbuffer != NULL))
		theApp.uploadBandwidthThrottler->SocketAvailable();
}

SocketSentBytes CEMSocket::Send(uint32 maxNumberOfBytesToSend, uint32 minFragSize, bool onlyAllowedToSendControlPacket)
{
	if (byConnected == ES_DISCONNECTED)
		return SocketSentBytes{};
	if (m_bOverlappedSending)
		return SendOv(maxNumberOfBytesToSend, minFragSize, onlyAllowedToSendControlPacket);
	return SendStd(maxNumberOfBytesToSend, minFragSize, onlyAllowedToSendControlPacket);
}

/**
 * Try to put queued up data on the socket.
 *
 * Control packets have higher priority, and will be sent first, if possible.
 * Standard packets can be split up in several package containers. In that case
 * all the parts of a split package must be sent in a row, without any control packet
 * in between.
 *
 * @param maxNumberOfBytesToSend This is the maximum number of bytes that is allowed to be put on the socket
 *								 this call. The actual number of sent bytes will be returned from the method.
 *
 * @param onlyAllowedToSendControlPacket This call we only try to put control packets on the sockets.
 *										 If there's a standard packet "in the way", and we think that this socket
 *										 is no longer an upload slot, then it is ok to send the standard packet to
 *										 get it out of the way. But it is not allowed to pick a new standard packet
 *										 from the queue during this call. Several split packets are counted as one
 *										 standard packet though, so it is ok to finish them all off if necessary.
 *
 * @return the actual number of bytes that were put on the socket.
 */
SocketSentBytes CEMSocket::SendStd(uint32 maxNumberOfBytesToSend, uint32 minFragSize, bool onlyAllowedToSendControlPacket)
{
	//EMTrace("CEMSocket::Send linked: %i, controlcount %i, standardcount %i, isbusy: %i",m_bLinkedPackets, controlpacket_queue.GetCount(), standardpacket_queue.GetCount(), IsBusy());
	bool anErrorHasOccured = false;
	uint32 sentStandardPacketBytesThisCall = 0;
	uint32 sentControlPacketBytesThisCall = 0;

	sendLocker.Lock();
	if (byConnected == ES_CONNECTED && IsEncryptionLayerReady()) {
		if (minFragSize < 1)
			minFragSize = 1;

		maxNumberOfBytesToSend = GetNextFragSize(maxNumberOfBytesToSend, minFragSize);

		lastCalledSend = timeGetTime();
		bool bWasLongTimeSinceSend = (lastCalledSend >= lastSent + SEC2MS(1));

		while (sentStandardPacketBytesThisCall + sentControlPacketBytesThisCall < maxNumberOfBytesToSend && !anErrorHasOccured // don't send more than allowed. Also, there should have been no error in earlier loop
			&& (sendbuffer != NULL || !controlpacket_queue.IsEmpty() || !standardpacket_queue.IsEmpty()) // there must exist something to send
			&& (	!onlyAllowedToSendControlPacket // this means we are allowed to send both types of packets, so proceed
				|| (sendbuffer != NULL && m_currentPacket_is_controlpacket) // We are in the progress of sending a control packet. We are always allowed to send those
				|| (sentStandardPacketBytesThisCall + sentControlPacketBytesThisCall > 0 && (sentStandardPacketBytesThisCall + sentControlPacketBytesThisCall) % minFragSize != 0) // Once we've started, continue to send until an even minFragsize to minimize packet overhead
				|| (sendbuffer == NULL && !controlpacket_queue.IsEmpty()) // There's a control packet in queue, and we are not currently sending anything, so we will handle the control packet next
				|| (sendbuffer != NULL && !m_currentPacket_is_controlpacket && bWasLongTimeSinceSend && !controlpacket_queue.IsEmpty() && (sentStandardPacketBytesThisCall + sentControlPacketBytesThisCall) < minFragSize) // We have waited to long to clean the current packet (which may be a standard packet that is in the way). Proceed no matter what the value of onlyAllowedToSendControlPacket.
				)
			)
		{ // If we are currently not in the progress of sending a packet, we will need to find the next one to send
			if (sendbuffer == NULL) {
				Packet *curPacket = NULL;
				m_currentPacket_is_controlpacket = !controlpacket_queue.IsEmpty();
				if (m_currentPacket_is_controlpacket) {
					// There's a control packet to send
					curPacket = controlpacket_queue.RemoveHead();
				} else if (!standardpacket_queue.IsEmpty()) {
					// There's a standard packet to send
					StandardPacketQueueEntry queueEntry = standardpacket_queue.RemoveHead();
					curPacket = queueEntry.packet;
					m_actualPayloadSize = queueEntry.actualPayloadSize;

					// remember this for statistics purposes.
					m_currentPackageIsFromPartFile = curPacket->IsFromPF();
				} else {
					// Just to be safe. Shouldn't happen?
					sendLocker.Unlock();

					// if we reach this point, then there's something wrong with the while condition above!
					ASSERT(0);
					theApp.QueueDebugLogLine(true, _T("EMSocket: Couldn't get a new packet! There's an error in the first while condition in EMSocket::Send()"));

					return SocketSentBytes{sentStandardPacketBytesThisCall, sentControlPacketBytesThisCall, true};
				}

				// We found a package to send. Get the data to send from the
				// package container and dispose of the container.
				sendblen = curPacket->GetRealPacketSize();
				sendbuffer = curPacket->DetachPacket();
				sent = 0;
				delete curPacket;

				// encrypting which cannot be done transparent by base class
				CryptPrepareSendData((uchar*)sendbuffer, sendblen);
			}

			// At this point we've got a packet to send in sendbuffer. Try to send it. Loop until entire packet
			// is sent, or until we reach maximum bytes to send for this call, or until we get an error.
			// NOTE! If send would block (returns WSAEWOULDBLOCK), we will return from this method INSIDE this loop.
			while (sent < sendblen
				&& sentStandardPacketBytesThisCall + sentControlPacketBytesThisCall < maxNumberOfBytesToSend
				&& (
					!onlyAllowedToSendControlPacket // this means we are allowed to send both types of packets, so proceed
					|| m_currentPacket_is_controlpacket
					|| (bWasLongTimeSinceSend && sentStandardPacketBytesThisCall + sentControlPacketBytesThisCall < minFragSize)
					|| (sentStandardPacketBytesThisCall + sentControlPacketBytesThisCall) % minFragSize != 0
					)
				&& !anErrorHasOccured)
			{
				uint32 tosend = sendblen - sent;
				if (!onlyAllowedToSendControlPacket || m_currentPacket_is_controlpacket) {
					if (tosend > maxNumberOfBytesToSend - (sentStandardPacketBytesThisCall + sentControlPacketBytesThisCall))
						tosend = maxNumberOfBytesToSend - (sentStandardPacketBytesThisCall + sentControlPacketBytesThisCall);
				} else if (bWasLongTimeSinceSend && minFragSize > sentStandardPacketBytesThisCall + sentControlPacketBytesThisCall) {
					if (tosend > minFragSize - (sentStandardPacketBytesThisCall + sentControlPacketBytesThisCall))
						tosend = minFragSize - (sentStandardPacketBytesThisCall + sentControlPacketBytesThisCall);
				} else {
					uint32 nextFragMaxBytesToSent = GetNextFragSize(sentStandardPacketBytesThisCall + sentControlPacketBytesThisCall, minFragSize);
					if (nextFragMaxBytesToSent >= sentStandardPacketBytesThisCall + sentControlPacketBytesThisCall && tosend > nextFragMaxBytesToSent - (sentStandardPacketBytesThisCall + sentControlPacketBytesThisCall))
						tosend = nextFragMaxBytesToSent - (sentStandardPacketBytesThisCall + sentControlPacketBytesThisCall);
				}
				ASSERT(tosend != 0 && tosend <= sendblen - sent);

				lastSent = timeGetTime();

				uint32 result = CEncryptedStreamSocket::Send(sendbuffer + sent, tosend); // deadlake PROXYSUPPORT - changed to AsyncSocketEx
				if (result == (uint32)SOCKET_ERROR) {
					uint32 error = GetLastError();
					if (error == WSAEWOULDBLOCK) {
						m_bBusy = true;

						//m_wasBlocked = true;
						sendLocker.Unlock();

						// Send() blocked, onsend will be called when ready to send again
						return SocketSentBytes{sentStandardPacketBytesThisCall, sentControlPacketBytesThisCall, true};
					}
					// Send() gave an error
					anErrorHasOccured = true;
					//DEBUG_ONLY( AddDebugLogLine(true,"EMSocket: An error has occured: %i", error) );
				} else {
					// we managed to send some bytes. Perform bookkeeping.
					m_bBusy = false;
					m_hasSent = true;

					sent += result;

					// Log send bytes in correct class
					if (!m_currentPacket_is_controlpacket) {
						sentStandardPacketBytesThisCall += result;

						if (m_currentPackageIsFromPartFile)
							m_numberOfSentBytesPartFile += result;
						else
							m_numberOfSentBytesCompleteFile += result;
					} else {
						sentControlPacketBytesThisCall += result;
						m_numberOfSentBytesControlPacket += result;
					}
				}
			}

			if (sent == sendblen) {
				// we are done sending the current package. Delete it and set
				// sendbuffer to NULL so a new packet can be fetched.
				delete[] sendbuffer;
				sendbuffer = NULL;
				sendblen = 0;

				if (!m_currentPacket_is_controlpacket) {
					m_actualPayloadSizeSent += m_actualPayloadSize;
					m_actualPayloadSize = 0;

					lastFinishedStandard = timeGetTime(); // reset timeout
					m_bAccelerateUpload = false; // Safe until told otherwise
				}

				sent = 0;
			}
		}
	}

	if (onlyAllowedToSendControlPacket && (!controlpacket_queue.IsEmpty() || (sendbuffer != NULL && m_currentPacket_is_controlpacket))) {
		// enter control packet send queue
		// we might enter control packet queue several times for the same package,
		// but that costs very little overhead. Less overhead than trying to make sure
		// that we only enter the queue once.
		theApp.uploadBandwidthThrottler->QueueForSendingControlPacket(this, m_hasSent);
	}
	//CleanSendLatencyList();

	sendLocker.Unlock();
	return SocketSentBytes{sentStandardPacketBytesThisCall, sentControlPacketBytesThisCall, !anErrorHasOccured};
}

/**
 * Try to put queued up data on the socket with Overlapped methods.
 *
 * Control packets have higher priority, and will be sent first, if possible.
 *
 * @param maxNumberOfBytesToSend This is the maximum number of bytes that is allowed to be put on the socket
 *								 this call. The actual number of sent bytes will be returned from the method.
 *
 * @param onlyAllowedToSendControlPacket This call we only try to put control packets on the sockets.
 *
 * @return the actual number of bytes that were put on the socket.
 */
SocketSentBytes CEMSocket::SendOv(uint32 maxNumberOfBytesToSend, uint32 minFragSize, bool onlyAllowedToSendControlPacket)
{
	//EMTrace("CEMSocket::Send linked: %i, controlcount %i, standardcount %i, isbusy: %i",m_bLinkedPackets, controlpacket_queue.GetCount(), standardpacket_queue.GetCount(), IsBusy());
	ASSERT(m_pProxyLayer == NULL);
	bool anErrorHasOccured = false;
	uint32 sentStandardPacketBytesThisCall = 0;
	uint32 sentControlPacketBytesThisCall = 0;

	sendLocker.Lock();
	if (byConnected == ES_CONNECTED && IsEncryptionLayerReady() && !IsBusyExtensiveCheck() && maxNumberOfBytesToSend > 0) {
		if (minFragSize < 1)
			minFragSize = 1;

		maxNumberOfBytesToSend = GetNextFragSize(maxNumberOfBytesToSend, minFragSize);
		lastCalledSend = timeGetTime();
		ASSERT(m_pPendingSendOperation == NULL && m_aBufferSend.IsEmpty());
		sint32 nBytesLeft = maxNumberOfBytesToSend;
		if (sendbuffer != NULL || !controlpacket_queue.IsEmpty() || !standardpacket_queue.IsEmpty()) {
			// WSASend takes multiple buffers which is quite nice for our case, as we have to call send
			// only once regardless how many packets we want to ship without memorymoving. But before
			// we can do this, collect all buffers we want to send in this call

			// first send the existing sendbuffer (already started packet)
			if (sendbuffer != NULL) {
				WSABUF pCurBuf;
				pCurBuf.len = min(sendblen - sent, (uint32)nBytesLeft);
				pCurBuf.buf = new CHAR[pCurBuf.len];
				memcpy(pCurBuf.buf, sendbuffer + sent, pCurBuf.len);
				sent += pCurBuf.len;
				m_aBufferSend.Add(pCurBuf);
				nBytesLeft -= pCurBuf.len;
				if (sent == sendblen) { //finished the buffer
					delete[] sendbuffer;
					sendbuffer = NULL;
					sendblen = 0;
				}
				sentStandardPacketBytesThisCall += pCurBuf.len; // Sendbuffer is always a standard packet in this method
				lastFinishedStandard = timeGetTime();
				m_bAccelerateUpload = false;
				m_actualPayloadSizeSent += m_actualPayloadSize;
				m_actualPayloadSize = 0;
				if (m_currentPackageIsFromPartFile)
					m_numberOfSentBytesPartFile += pCurBuf.len;
				else
					m_numberOfSentBytesCompleteFile += pCurBuf.len;
			}

			// next send all control packets if there are any and we have bytes left
			while (!controlpacket_queue.IsEmpty() && nBytesLeft > 0) {
				// send controlpackets always completely, ignoring the limit by a few bytes if we must
				WSABUF pCurBuf;
				Packet *curPacket = controlpacket_queue.RemoveHead();
				pCurBuf.len = curPacket->GetRealPacketSize();
				pCurBuf.buf = curPacket->DetachPacket();
				delete curPacket;
				// encrypting which cannot be done transparent by base class
				CryptPrepareSendData((uchar*)pCurBuf.buf, pCurBuf.len);
				m_aBufferSend.Add(pCurBuf);
				nBytesLeft -= pCurBuf.len;
				sentControlPacketBytesThisCall += pCurBuf.len;
			}

			// and now finally the standard packets if there are any and we have bytes left and we are allowed to
			while (!standardpacket_queue.IsEmpty() && nBytesLeft > 0 && !onlyAllowedToSendControlPacket) {
				StandardPacketQueueEntry queueEntry = standardpacket_queue.RemoveHead();
				WSABUF pCurBuf;
				Packet *curPacket = queueEntry.packet;
				m_currentPackageIsFromPartFile = curPacket->IsFromPF();

				// can we send it right away or only a part of it?
				if (queueEntry.packet->GetRealPacketSize() <= (uint32)nBytesLeft) {
					// yay
					pCurBuf.len = curPacket->GetRealPacketSize();
					pCurBuf.buf = curPacket->DetachPacket();
					CryptPrepareSendData((uchar*)pCurBuf.buf, pCurBuf.len);// encrypting which cannot be done transparent by base class
					m_actualPayloadSizeSent += queueEntry.actualPayloadSize;
					lastFinishedStandard = timeGetTime();
					m_bAccelerateUpload = false;
				} else {	// aww, well first stuff everything into the sendbuffer and then send what we can of it
					ASSERT(sendbuffer == NULL);
					m_actualPayloadSize = queueEntry.actualPayloadSize;
					sendblen = curPacket->GetRealPacketSize();
					sendbuffer = curPacket->DetachPacket();
					sent = 0;
					CryptPrepareSendData((uchar*)sendbuffer, sendblen); // encrypting which cannot be done transparent by base class
					pCurBuf.len = min(sendblen - sent, (uint32)nBytesLeft);
					pCurBuf.buf = new CHAR[pCurBuf.len];
					memcpy(pCurBuf.buf, sendbuffer, pCurBuf.len);
					sent += pCurBuf.len;
					ASSERT(sent < sendblen);
					m_currentPacket_is_controlpacket = false;
				}
				delete curPacket;
				m_aBufferSend.Add(pCurBuf);
				nBytesLeft -= pCurBuf.len;
				sentStandardPacketBytesThisCall += pCurBuf.len;
				if (m_currentPackageIsFromPartFile)
					m_numberOfSentBytesPartFile += pCurBuf.len;
				else
					m_numberOfSentBytesCompleteFile += pCurBuf.len;
			}
			// all right, prepare to send our collected buffers
			m_pPendingSendOperation = new WSAOVERLAPPED();
			m_pPendingSendOperation->hEvent = theApp.uploadBandwidthThrottler->GetSocketAvailableEvent();
			if (CEncryptedStreamSocket::SendOv(m_aBufferSend, m_pPendingSendOperation) == 0)
				CleanUpOverlappedSendOperation();
			else {
				int nError = WSAGetLastError();
				if (nError != WSA_IO_PENDING) {
					anErrorHasOccured = true;
					theApp.QueueDebugLogLineEx(ERROR, _T("WSASend() Error: %u, %s"), nError, (LPCTSTR)GetErrorMessage(nError));
					CleanUpOverlappedSendOperation();
				}
			}
		}
	}

	if (onlyAllowedToSendControlPacket && !controlpacket_queue.IsEmpty()) {
		// enter control packet send queue
		// we might enter control packet queue several times for the same package,
		// but that costs very little overhead. Less overhead than trying to make sure
		// that we only enter the queue once.
		theApp.uploadBandwidthThrottler->QueueForSendingControlPacket(this, m_hasSent);
	}

	sendLocker.Unlock();
	return SocketSentBytes{sentStandardPacketBytesThisCall, sentControlPacketBytesThisCall, !anErrorHasOccured};
}

uint32 CEMSocket::GetNextFragSize(uint32 current, uint32 minFragSize)
{
	if (current % minFragSize == 0)
		return current;
	return minFragSize * (current / minFragSize + 1);
}

/**
 * Decides the (minimum) amount the socket needs to send to prevent timeout.
 *
 * @author SlugFiller
 */
uint32 CEMSocket::GetNeededBytes()
{
	sendLocker.Lock();
	if (byConnected == ES_DISCONNECTED) {
		sendLocker.Unlock();
		return 0;
	}

	bool isControlpacket = (sendbuffer == NULL) || m_currentPacket_is_controlpacket;
	if (isControlpacket && standardpacket_queue.IsEmpty()) {
		// No standard packet to send. Even if data needs to be sent to prevent timout, there's nothing to send.
		sendLocker.Unlock();
		return 0;
	}
	if (!isControlpacket && !controlpacket_queue.IsEmpty())
		m_bAccelerateUpload = true;	// We might be trying to send a block request, accelerate packet

	DWORD sendgap = timeGetTime();
	DWORD timeleft = sendgap - lastFinishedStandard;
	sendgap -= lastCalledSend;
	DWORD timetotal = m_bAccelerateUpload ? SEC2MS(45) : SEC2MS(90);
	uint64 sizeleft, sizetotal;
	if (!isControlpacket) {
		sizeleft = sendblen - sent;
		sizetotal = sendblen;
	} else
		sizeleft = sizetotal = standardpacket_queue.GetHead().packet->GetRealPacketSize();
	sendLocker.Unlock();

	if (timeleft >= timetotal)
		return (uint32)sizeleft;
	timeleft = timetotal - timeleft;
	if (timeleft * sizetotal >= timetotal * sizeleft) {
		// don't use 'GetTimeOut' here in case the timeout value is high,
		if (sendgap >= SEC2MS(20))
			return 1;	// Don't let the socket itself time out - Might happen when switching from spread(non-focus) slot to trickle slot
		return 0;
	}
	uint64 decval = timeleft * sizetotal / timetotal;
	if (!decval)
		return (uint32)sizeleft;
	if (decval < sizeleft)
		return (uint32)(sizeleft - decval + 1);	// Round up
	return 1;
}

// pach2:
// written this overriden Receive to handle transparently FIN notifications coming from calls to recv()
// This was maybe(??) the cause of a lot of socket error, notably after a brutal close from peer
// also added trace so that we can debug after the fact ...
int CEMSocket::Receive(void* lpBuf, int nBufLen, int nFlags)
{
//	EMTrace("CEMSocket::Receive on %lu, maxSize=%d", (SOCKET)this, nBufLen);
	int recvRetCode = CEncryptedStreamSocket::Receive(lpBuf, nBufLen, nFlags); // deadlake PROXYSUPPORT - changed to AsyncSocketEx
	switch (recvRetCode) {
	case 0:
		if (GetRealReceivedBytes() > 0) // we received data but it was for the underlying encryption layer - all fine
			return 0;
		//EMTrace("CEMSocket::##Received FIN on %d, maxSize=%d",(SOCKET)this,nBufLen);
		// FIN received on socket // Connection is being closed by peer
		//ASSERT (false);
		if (0 == AsyncSelect(FD_CLOSE | FD_WRITE)) { // no more READ notifications ...
			//int waserr = GetLastError(); // oups, AsyncSelect failed !!!
			ASSERT(false);
		}
		return 0;
	case SOCKET_ERROR:
		switch (GetLastError()) {
		case WSANOTINITIALISED:
			ASSERT(false);
			EMTrace("CEMSocket::OnReceive:A successful AfxSocketInit must occur before using this API.");
			break;
		case WSAENETDOWN:
			ASSERT(true);
			EMTrace("CEMSocket::OnReceive:The socket %u received a net down error", (SOCKET)this);
			break;
		case WSAENOTCONN: // The socket is not connected.
			EMTrace("CEMSocket::OnReceive:The socket %u is not connected", (SOCKET)this);
			break;
		case WSAEINPROGRESS:   // A blocking Windows Sockets operation is in progress.
			EMTrace("CEMSocket::OnReceive:The socket %u is blocked", (SOCKET)this);
			break;
		case WSAEWOULDBLOCK:   // The socket is marked as nonblocking and the Receive operation would block.
			EMTrace("CEMSocket::OnReceive:The socket %u would block", (SOCKET)this);
			break;
		case WSAENOTSOCK:   // The descriptor is not a socket.
			EMTrace("CEMSocket::OnReceive:The descriptor %u is not a socket (may have been closed or never created)", (SOCKET)this);
			break;
		case WSAEOPNOTSUPP:  // MSG_OOB was specified, but the socket is not of type SOCK_STREAM.
			break;
		case WSAESHUTDOWN:   // The socket has been shut down; it is not possible to call Receive on a socket after ShutDown has been invoked with nHow set to 0 or 2.
			EMTrace("CEMSocket::OnReceive:The socket %u has been shut down", (SOCKET)this);
			break;
		case WSAEMSGSIZE:   // The datagram was too large to fit into the specified buffer and was truncated.
			EMTrace("CEMSocket::OnReceive:The datagram was too large to fit and was truncated (socket %u)", (SOCKET)this);
			break;
		case WSAEINVAL:   // The socket has not been bound with Bind.
			EMTrace("CEMSocket::OnReceive:The socket %u has not been bound", (SOCKET)this);
			break;
		case WSAECONNABORTED:   // The virtual circuit was aborted due to timeout or other failure.
			EMTrace("CEMSocket::OnReceive:The socket %u has not been bound", (SOCKET)this);
			break;
		case WSAECONNRESET:   // The virtual circuit was reset by the remote side.
			EMTrace("CEMSocket::OnReceive:The socket %u has not been bound", (SOCKET)this);
			break;
		default:
			EMTrace("CEMSocket::OnReceive:Unexpected socket error %x on socket %u", GetLastError(), (SOCKET)this);
			break;
		}
		break;
	default:
//		EMTrace("CEMSocket::OnReceive on %u, receivedSize=%d",(SOCKET)this,recvRetCode);
		return recvRetCode;
	}
	return SOCKET_ERROR;
}

void CEMSocket::RemoveAllLayers()
{
	CEncryptedStreamSocket::RemoveAllLayers();
	delete m_pProxyLayer;
	m_pProxyLayer = NULL;
}

int CEMSocket::OnLayerCallback(std::list<t_callbackMsg>& callbacks)
{
	for (std::list<t_callbackMsg>::const_iterator iter = callbacks.begin(); iter != callbacks.end(); ++iter) {
		if (iter->nType == LAYERCALLBACK_LAYERSPECIFIC) {
			if (iter->pLayer == m_pProxyLayer) {
				m_strLastProxyError = GetProxyError((int)iter->wParam);
				switch (iter->wParam) {
				case PROXYERROR_NOCONN:
					// We failed to connect to the proxy.
					m_bProxyConnectFailed = true;
				case PROXYERROR_REQUESTFAILED:
					// We are connected to the proxy but it failed to connect to the peer.
					if (thePrefs.GetVerbose() && iter->str && iter->str[0] != '\0')
						m_strLastProxyError.AppendFormat(_T(" - %hs"), iter->str);
				}
				LogWarning(LOG_DEFAULT, _T("Proxy-Error: %s"), (LPCTSTR)m_strLastProxyError);
			}
		}
		delete[] iter->str;
	}
	return 0;
}

/**
 * Removes all packets from the standard queue that don't have to be sent for the socket to be able to send a control packet.
 *
 * Before a socket can send a new packet, the current packet has to be finished. If the current packet is part of
 * a split packet, then all parts of that split packet must be sent before the socket can send a control packet.
 *
 * This method keeps in standard queue only those packets that must be sent (rest of split packet), and removes everything
 * after it. The method doesn't touch the control packet queue.
 */
void CEMSocket::TruncateQueues()
{
	sendLocker.Lock();

	// Clear the standard queue totally
	// Please note! There may still be a standardpacket in the sendbuffer variable!
	while (!standardpacket_queue.IsEmpty())
		delete standardpacket_queue.RemoveHead().packet;

	sendLocker.Unlock();
}

#ifdef _DEBUG
void CEMSocket::AssertValid() const
{
	CEncryptedStreamSocket::AssertValid();

	const_cast<CEMSocket *>(this)->sendLocker.Lock();

	ASSERT(byConnected == ES_DISCONNECTED || byConnected == ES_NOTCONNECTED || byConnected == ES_CONNECTED);
	CHECK_BOOL(m_bProxyConnectFailed);
	CHECK_PTR(m_pProxyLayer);
	(void)downloadLimit;
	CHECK_BOOL(downloadLimitEnable);
	CHECK_BOOL(pendingOnReceive);
	//char pendingHeader[PACKET_HEADER_SIZE];
	pendingHeaderSize;
	CHECK_PTR(pendingPacket);
	(void)pendingPacketSize;
	CHECK_ARR(sendbuffer, sendblen);
	(void)sent;
	controlpacket_queue.AssertValid();
	standardpacket_queue.AssertValid();
	CHECK_BOOL(m_currentPacket_is_controlpacket);
	//(void)sendLocker;
	(void)m_numberOfSentBytesCompleteFile;
	(void)m_numberOfSentBytesPartFile;
	(void)m_numberOfSentBytesControlPacket;
	CHECK_BOOL(m_currentPackageIsFromPartFile);
	(void)lastCalledSend;
	(void)m_actualPayloadSize;
	(void)m_actualPayloadSizeSent;

	const_cast<CEMSocket *>(this)->sendLocker.Unlock();
}
#endif

#ifdef _DEBUG
void CEMSocket::Dump(CDumpContext& dc) const
{
	CEncryptedStreamSocket::Dump(dc);
}
#endif

void CEMSocket::DataReceived(const BYTE *, UINT)
{
	ASSERT(0);
}

DWORD CEMSocket::GetTimeOut() const
{
	return m_uTimeOut;
}

void CEMSocket::SetTimeOut(DWORD uTimeOut)
{
	m_uTimeOut = uTimeOut;
}

CString CEMSocket::GetFullErrorMessage(DWORD dwError)
{
	CString strError;

	// Proxy error
	if (!GetLastProxyError().IsEmpty()) {
		strError = GetLastProxyError();
		// If we had a proxy error and the socket error is WSAECONNABORTED, we just 'aborted'
		// the TCP connection ourself - no need to show that self-created error too.
		if (dwError == WSAECONNABORTED)
			return strError;
	}

	// Winsock error
	if (dwError) {
		if (!strError.IsEmpty())
			strError += _T(": ");
		strError += GetErrorMessage(dwError, 1);
	}

	return strError;
}

// increases the send buffer to a bigger size
bool CEMSocket::UseBigSendBuffer()
{
#define BIGSIZE (128 * 1024)

	if (!m_bUsesBigSendBuffers) {
		m_bUsesBigSendBuffers = true;
		int val = BIGSIZE;
		int oldval = 0;
		int vallen = sizeof oldval;
		GetSockOpt(SO_SNDBUF, &oldval, &vallen);
		if (val > oldval) {
			SetSockOpt(SO_SNDBUF, &val, sizeof val);
			val = 0;
			vallen = sizeof val;
			GetSockOpt(SO_SNDBUF, &val, &vallen);
			m_bUsesBigSendBuffers = (val >= BIGSIZE);
#if defined(_DEBUG) || defined(_BETA) || defined(_DEVBUILD)
			if (m_bUsesBigSendBuffers)
				theApp.QueueDebugLogLine(false, _T("Increased Sendbuffer for uploading socket from %uKB to %uKB"), oldval / 1024, val / 1024);
			else
				theApp.QueueDebugLogLine(false, _T("Failed to increase Sendbuffer for uploading socket, stays at %uKB"), oldval / 1024);
#endif
		}
	}
	return m_bUsesBigSendBuffers;
}

bool CEMSocket::IsBusyExtensiveCheck()
{
	if (!m_bOverlappedSending)
		return m_bBusy;

	CSingleLock lockSend(&sendLocker, TRUE);
	if (m_pPendingSendOperation == NULL)
		return false;
	DWORD dwTransferred, dwFlags;
	if (WSAGetOverlappedResult(GetSocketHandle(), m_pPendingSendOperation, &dwTransferred, FALSE, &dwFlags)) {
		CleanUpOverlappedSendOperation();
		OnSend(0);
		return false;
	}
	int nError = WSAGetLastError();
	if (nError == WSA_IO_INCOMPLETE)
		return true;
	CleanUpOverlappedSendOperation();
	theApp.QueueDebugLogLineEx(LOG_ERROR, _T("WSAGetOverlappedResult return error: %s"), (LPCTSTR)GetErrorMessage(nError));
	return false;
}

// won't always deliver the proper result (sometimes reports busy even if it isn't any more and thread related errors) but doesn't needs locks or function calls
bool CEMSocket::IsBusyQuickCheck() const
{
	return m_bOverlappedSending ? (m_pPendingSendOperation != NULL) : m_bBusy;
}

void CEMSocket::CleanUpOverlappedSendOperation()
{
//sendLock must be locked by a caller!
	if (InterlockedExchange(&m_OverlappedCleaning, 1))
		return;
	if (m_pPendingSendOperation != NULL) {
		delete m_pPendingSendOperation;
		m_pPendingSendOperation = NULL;
		for (INT_PTR i = m_aBufferSend.GetCount(); --i >= 0;)
			delete[] m_aBufferSend[i].buf;
		m_aBufferSend.RemoveAll();
	}
	_InterlockedExchange(&m_OverlappedCleaning, 0);
}

bool CEMSocket::HasQueues(bool bOnlyStandardPackets) const
{
	// not trustworthy threaded? but it's ok if we don't get the correct result now and then
	return sendbuffer != NULL || !standardpacket_queue.IsEmpty() || (!controlpacket_queue.IsEmpty() && !bOnlyStandardPackets);
}

bool CEMSocket::IsEnoughFileDataQueued(uint32 nMinFilePayloadBytes) const
{
	// check we have at least nMinFilePayloadBytes Payload data in our standardqueue
	for (POSITION pos = standardpacket_queue.GetHeadPosition(); pos != NULL;) {
		uint32 actualsize = standardpacket_queue.GetNext(pos).actualPayloadSize;
		if (actualsize > nMinFilePayloadBytes)
			return true;
		nMinFilePayloadBytes -= actualsize;
	}
	return false;
}