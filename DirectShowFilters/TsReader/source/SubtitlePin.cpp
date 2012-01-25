/*
 *  Copyright (C) 2005 Team MediaPortal
 *  http://www.team-mediaportal.com
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#pragma warning(disable:4996)
#pragma warning(disable:4995)
#include "StdAfx.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <streams.h>
#include <sbe.h>
#include "tsreader.h"
#include "SubtitlePin.h"
#include "AudioPin.h"
#include "Videopin.h"

// For more details for memory leak detection see the alloctracing.h header
#include "..\..\alloctracing.h"

#define MAX_TIME  86400000L
extern void LogDebug(const char *fmt, ...) ;
extern DWORD m_tGTStartTime;

CSubtitlePin::CSubtitlePin(LPUNKNOWN pUnk, CTsReaderFilter *pFilter, HRESULT *phr,CCritSec* section) :
  CSourceStream(NAME("pinSubtitle"), phr, pFilter, L"Subtitle"),
  m_pTsReaderFilter(pFilter),
  CSourceSeeking(NAME("pinSubtitle"),pUnk,phr,section),
  m_section(section)
{
  m_rtStart=0;
  m_bConnected=false;
  m_dwSeekingCaps =
  AM_SEEKING_CanSeekAbsolute  |
  AM_SEEKING_CanSeekForwards  |
  AM_SEEKING_CanSeekBackwards |
  AM_SEEKING_CanGetStopPos  |
  AM_SEEKING_CanGetDuration |
  //AM_SEEKING_CanGetCurrentPos |
  AM_SEEKING_Source;
  //m_bSeeking=false;
  m_bInFillBuffer=false;
}

CSubtitlePin::~CSubtitlePin()
{
  LogDebug("subPin:dtor()");
}

bool CSubtitlePin::IsInFillBuffer()
{
  return m_bInFillBuffer;
}

bool CSubtitlePin::IsConnected()
{
  //LogDebug("CSubtitlePin connected? %i",m_bConnected);
  return m_bConnected;
}

STDMETHODIMP CSubtitlePin::NonDelegatingQueryInterface( REFIID riid, void ** ppv )
{
  if (riid == IID_IMediaSeeking)
  {
    return CSourceSeeking::NonDelegatingQueryInterface( riid, ppv );
  }
  if (riid == IID_IMediaPosition)
  {
    return CSourceSeeking::NonDelegatingQueryInterface( riid, ppv );
  }
  return CSourceStream::NonDelegatingQueryInterface(riid, ppv);
}

HRESULT CSubtitlePin::GetMediaType(CMediaType *pmt)
{
  pmt->InitMediaType();
  pmt->SetType      (& MEDIATYPE_Stream);
  pmt->SetSubtype   (& MEDIASUBTYPE_MPEG2_TRANSPORT);
  pmt->SetSampleSize(1);
  pmt->SetTemporalCompression(FALSE);
  pmt->SetVariableSize();

  return S_OK;
}

HRESULT CSubtitlePin::DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pRequest)
{
  HRESULT hr;
  CheckPointer(pAlloc, E_POINTER);
  CheckPointer(pRequest, E_POINTER);

  if (pRequest->cBuffers == 0)
  {
      pRequest->cBuffers = 30;
  }
  pRequest->cbBuffer = 8192;

  ALLOCATOR_PROPERTIES Actual;
  hr = pAlloc->SetProperties(pRequest, &Actual);
  if (FAILED(hr))
  {
    return hr;
  }

  if (Actual.cbBuffer < pRequest->cbBuffer)
  {
    return E_FAIL;
  }

  return S_OK;
}

void CSubtitlePin::SetDiscontinuity(bool onOff)
{
  m_bDiscontinuity=onOff;
}

HRESULT CSubtitlePin::CheckConnect(IPin *pReceivePin)
{
  HRESULT hr;
  PIN_INFO pinInfo;
  FILTER_INFO filterInfo;

  hr=pReceivePin->QueryPinInfo(&pinInfo);
  if (!SUCCEEDED(hr)) return E_FAIL;
  else if (pinInfo.pFilter==NULL) return E_FAIL;
  else pinInfo.pFilter->Release(); // we dont need the filter just the info

  // we only want to connect to the DVB subtitle input pin
  // on the subtitle filter (and not the teletext one for example!)
  if( wcscmp(pinInfo.achName, L"In") != 0)
  {
    //LogDebug("sub pin: Cant connect to pin name %s", pinInfo.achName);
    return E_FAIL;
  }

  hr=pinInfo.pFilter->QueryFilterInfo(&filterInfo);
  filterInfo.pGraph->Release();

  if (!SUCCEEDED(hr)) return E_FAIL;
  if (wcscmp(filterInfo.achName,L"MediaPortal DVBSub2") !=0 )
  {
    //LogDebug("sub pin: Cant connect to filter name %s", filterInfo.achName);
    return E_FAIL;
  }
  return CBaseOutputPin::CheckConnect(pReceivePin);
}
HRESULT CSubtitlePin::CompleteConnect(IPin *pReceivePin)
{
  m_bInFillBuffer=false;
  LogDebug("subPin:CompleteConnect()");
  HRESULT hr = CBaseOutputPin::CompleteConnect(pReceivePin);

  if (SUCCEEDED(hr))
  {
    LogDebug("subPin:CompleteConnect() done");
    m_bConnected=true;
  }
  else
  {
    LogDebug("subPin:CompleteConnect() failed:%x",hr);
  }

  if (m_pTsReaderFilter->IsTimeShifting())
  {
    //m_rtDuration=CRefTime(MAX_TIME);
    REFERENCE_TIME refTime;
    m_pTsReaderFilter->GetDuration(&refTime);
    m_rtDuration=CRefTime(refTime);
  }
  else
  {
    REFERENCE_TIME refTime;
    m_pTsReaderFilter->GetDuration(&refTime);
    m_rtDuration=CRefTime(refTime);
  }
  LogDebug("subPin:CompleteConnect() ok");
  return hr;
}


HRESULT CSubtitlePin::BreakConnect()
{
  //LogDebug("subPin:BreakConnect() ok");
  m_bConnected = false;
  return CSourceStream::BreakConnect();
}

void CSubtitlePin::CreateEmptySample(IMediaSample *pSample)
{
  if (pSample)
  {
    pSample->SetTime(NULL, NULL);
    pSample->SetActualDataLength(0);
    pSample->SetSyncPoint(false);
    pSample->SetDiscontinuity(false);
  }
  else
    LogDebug("subPin: CreateEmptySample() invalid sample!");
}

HRESULT CSubtitlePin::DoBufferProcessingLoop(void)
{
  Command com;
  OnThreadStartPlay();

  do 
  {
    while (!CheckRequest(&com)) 
    {
      IMediaSample *pSample;
      HRESULT hr = GetDeliveryBuffer(&pSample,NULL,NULL,0);
      if (FAILED(hr)) 
      {
        Sleep(1);
        continue;	// go round again. Perhaps the error will go away
        // or the allocator is decommited & we will be asked to
        // exit soon.
      }

      // Virtual function user will override.
      hr = FillBuffer(pSample);

      if (hr == S_OK) 
      {
        // Some decoders seem to crash when we provide empty samples 
        if ((pSample->GetActualDataLength() > 0) && !m_pTsReaderFilter->IsSeeking() && !m_pTsReaderFilter->IsStopping())
        {
          hr = Deliver(pSample);     
        }
		
        pSample->Release();

        // downstream filter returns S_FALSE if it wants us to
        // stop or an error if it's reporting an error.
        if(hr != S_OK)
        {
          DbgLog((LOG_TRACE, 2, TEXT("Deliver() returned %08x; stopping"), hr));
          return S_OK;
        }
      } 
      else if (hr == S_FALSE) 
      {
        // derived class wants us to stop pushing data
        pSample->Release();
        DeliverEndOfStream();
        return S_OK;
      } 
      else 
      {
        // derived class encountered an error
        pSample->Release();
        DbgLog((LOG_ERROR, 1, TEXT("Error %08lX from FillBuffer!!!"), hr));
        DeliverEndOfStream();
        m_pFilter->NotifyEvent(EC_ERRORABORT, hr, 0);
        return hr;
      }
     // all paths release the sample
    }
    // For all commands sent to us there must be a Reply call!
	  if (com == CMD_RUN || com == CMD_PAUSE) 
    {
      Reply(NOERROR);
	  } 
    else if (com != CMD_STOP) 
    {
      Reply((DWORD) E_UNEXPECTED);
      DbgLog((LOG_ERROR, 1, TEXT("Unexpected command!!!")));
	  }
  } while (com != CMD_STOP);
  
  return S_FALSE;
}


HRESULT CSubtitlePin::FillBuffer(IMediaSample *pSample)
{
  try
  {
    CDeMultiplexer& demux=m_pTsReaderFilter->GetDemultiplexer();
    CBuffer* buffer=NULL;

    do
    {
      //get file-duration and set m_rtDuration
      GetDuration(NULL);

      m_bInFillBuffer = true;

      //if the filter is currently seeking to a new position
      //or this pin is currently seeking to a new position then
      //we dont try to read any packets, but simply return...
      if (m_pTsReaderFilter->IsSeeking() || m_pTsReaderFilter->IsStopping() || !m_bRunning)
      {
        CreateEmptySample(pSample);
        m_bInFillBuffer = false;
        m_bDiscontinuity = TRUE; //Next good sample will be discontinuous
        Sleep(5);
        return NOERROR;
      }
      
      //CAutoLock slock (&demux.m_sectionSeekSubtitle); //Lock for seeking

      if (m_pTsReaderFilter->m_bStreamCompensated && !demux.m_bFlushRunning)
      {
        //get next buffer from demultiplexer
        CAutoLock flock (&demux.m_sectionFlushSubtitle);
        //CAutoLock lock(&m_bufferLock);
        buffer=demux.GetSubtitle();
      }
      else
      {
        buffer=NULL;
      }

      //did we reach the end of the file
      if (demux.EndOfFile())
      {
        LogDebug("subPin:set eof");
        CreateEmptySample(pSample);
        m_bInFillBuffer=false;
        return S_FALSE; //S_FALSE will notify the graph that end of file has been reached
      }

      if (buffer == NULL)
      {
        Sleep(10);
      }
      else
      {
        CRefTime RefTime, cRefTime;
        bool HasTimestamp;
        //check if it has a timestamp
        if ((HasTimestamp=buffer->MediaTime(RefTime)))
        {
          cRefTime = RefTime;
          cRefTime -= m_rtStart;

          if (cRefTime.m_time >= 0)
          {
            m_bPresentSample = true;
          }
          else
            //  Sample is too late.
            m_bPresentSample = false;              
        }

        if (m_bPresentSample && (buffer->Length() > 0))
        {
          //do we need to set the discontinuity flag?
          if (m_bDiscontinuity || buffer->GetDiscontinuity())
          {
            //ifso, set it
            LogDebug("subPin:set discontinuity");
            pSample->SetDiscontinuity(TRUE);
            m_bDiscontinuity = FALSE;
          }

          REFERENCE_TIME refTime=(REFERENCE_TIME)cRefTime;
          if (HasTimestamp)
          {
            //now we have the final timestamp, set timestamp in sample
            //REFERENCE_TIME refTime=(REFERENCE_TIME)cRefTime;
            refTime = (REFERENCE_TIME)((double)refTime/m_dRateSeeking);
            pSample->SetSyncPoint(TRUE);
            pSample->SetTime(&refTime,&refTime);
          }
          else
          {
            //buffer has no timestamp
            pSample->SetTime(NULL, NULL);
            pSample->SetSyncPoint(FALSE);
          }
          //copy buffer in sample
          BYTE* pSampleBuffer;
          pSample->SetActualDataLength(buffer->Length());
          pSample->GetPointer(&pSampleBuffer);
          memcpy(pSampleBuffer, buffer->Data(), buffer->Length());
          //delete the buffer and return
          delete buffer;
        }
        else
        { // Buffer was not displayed because it was out of date, search for next.
          delete buffer;
          buffer=NULL;
          m_bDiscontinuity = TRUE; //Next good sample will be discontinuous
        }
      }
    } while (buffer==NULL);
    
    m_bInFillBuffer=false;
    return NOERROR;
  }
  catch(...)
  {
    LogDebug("subPin:fillbuffer exception");
  }
  
  CreateEmptySample(pSample);
  m_bDiscontinuity = TRUE; //Next good sample will be discontinuous  
  m_bInFillBuffer=false;
  
  return NOERROR;
}

//******************************************************
/// Called when thread is about to start delivering data to the filter
///
HRESULT CSubtitlePin::OnThreadStartPlay()
{
  //set discontinuity flag indicating to codec that the new data
  //is not belonging to any previous data
  m_bDiscontinuity=TRUE;
  m_bInFillBuffer=false;
  m_bPresentSample=false;

  float fStart=(float)m_rtStart.Millisecs();
  fStart/=1000.0f;

  //  //tell demuxer to start deliver subtitle packets again
  //  CDeMultiplexer& demux=m_pTsReaderFilter->GetDemultiplexer();
  //  demux.SetHoldSubtitle(false);

  LogDebug("subPin:OnThreadStartPlay(%f)", fStart);

  //start playing
  DeliverNewSegment(m_rtStart, m_rtStop, m_dRateSeeking);
  return CSourceStream::OnThreadStartPlay( );
}

// CMediaSeeking
HRESULT CSubtitlePin::ChangeStart()
{
  m_pTsReaderFilter->SetSeeking(true);
  UpdateFromSeek();
  return S_OK;
}
HRESULT CSubtitlePin::ChangeStop()
{
  m_pTsReaderFilter->SetSeeking(true);
  UpdateFromSeek();
  return S_OK;
}

HRESULT CSubtitlePin::ChangeRate()
{
  if( m_dRateSeeking <= 0 )
  {
      m_dRateSeeking = 1.0;  // Reset to a reasonable value.
      return E_FAIL;
  }
  
  LogDebug("subPin: ChangeRate, m_dRateSeeking %f, Force seek done %d, IsSeeking %d",(float)m_dRateSeeking, m_pTsReaderFilter->m_bSeekAfterRcDone, m_pTsReaderFilter->IsSeeking());
  if (!m_pTsReaderFilter->m_bSeekAfterRcDone && !m_pTsReaderFilter->IsSeeking() && !m_pTsReaderFilter->IsWaitDataAfterSeek()) //Don't force seek if another pin has already triggered it
  {
    m_pTsReaderFilter->m_bForceSeekAfterRateChange = true;
    m_pTsReaderFilter->SetSeeking(true);
    UpdateFromSeek();
  }
  return S_OK;
}

void CSubtitlePin::SetStart(CRefTime rtStartTime)
{
  m_rtStart = rtStartTime ;
}

STDMETHODIMP CSubtitlePin::SetPositions(LONGLONG *pCurrent, DWORD CurrentFlags, LONGLONG *pStop, DWORD StopFlags)
{
  if (m_pTsReaderFilter->SetSeeking(true) && !m_pTsReaderFilter->IsWaitDataAfterSeek()) //We're not already seeking
  {
    return CSourceSeeking::SetPositions(pCurrent, CurrentFlags, pStop,  StopFlags);
  }
  return S_OK;
}

//******************************************************
/// UpdateFromSeek() called when need to seek to a specific timestamp in the file
/// m_rtStart contains the time we need to seek to...
///
//void CSubtitlePin::UpdateFromSeek()
//{
//  CDeMultiplexer& demux=m_pTsReaderFilter->GetDemultiplexer();
//  CTsDuration tsduration=m_pTsReaderFilter->GetDuration();
//
//  //if (m_rtStart>m_rtDuration)
//  //  m_rtStart=m_rtDuration;
//
//  //there is a bug in directshow causing UpdateFromSeek() to be called multiple times
//  //directly after eachother
//  //for a single seek operation. To 'fix' this we only perform the seeking operation
//  //if we didnt do a seek in the last 5 seconds...
//  if (GET_TIME_NOW()-m_seekTimer<5000)
//  {
//    if (m_lastSeek==m_rtStart)
//    {
//      LogDebug("subPin:skip seek");
//      return;
//    }
//  }
//
//  //Note that the seek timestamp (m_rtStart) is done in the range
//  //from earliest - latest from GetAvailable()
//  //We however would like the seek timestamp to be in the range 0-fileduration
//  m_seekTimer=GET_TIME_NOW();
//  m_lastSeek=m_rtStart;
//
//  CRefTime rtSeek=m_rtStart;
//  float seekTime=(float)rtSeek.Millisecs();
//  seekTime/=1000.0f;
//
//  //get the earliest timestamp available in the file
//  float earliesTimeStamp= tsduration.StartPcr().ToClock() - tsduration.FirstStartPcr().ToClock();
//  if (earliesTimeStamp<0) earliesTimeStamp=0;
//
//  //correct the seek time
//  seekTime-=earliesTimeStamp;
//  if (seekTime<0) seekTime=0;
//  LogDebug("sub seek to %f", seekTime);
//
//  seekTime*=1000.0f;
//  rtSeek = CRefTime((LONG)seekTime);
//
//  //if another output pin is seeking, then wait until its finished
//  m_bSeeking=true;
//  while (m_pTsReaderFilter->IsSeeking()) Sleep(1);
//
//  //tell demuxer to stop deliver subtitle data and wait until
//  //FillBuffer() finished
//  demux.SetHoldSubtitle(true);
//  while (m_bInFillBuffer) Sleep(1);
//  CAutoLock lock(&m_bufferLock);
//
//  //if a pin-output thread exists...
//  if (ThreadExists())
//  {
//    //normally the audio pin does the actual seeking
//    //check if its connected. If not, we'll do the seeking
//    if (!m_pTsReaderFilter->GetAudioPin()->IsConnected())
//    {
//      //tell the filter we are starting a seek operation
////      m_pTsReaderFilter->SeekStart();
//    }
//
//    //deliver a begin-flush to the codec filter so it stops asking for data
//    HRESULT hr=DeliverBeginFlush();
//
//    //stop the thread
//    Stop();
//    if (!m_pTsReaderFilter->GetAudioPin()->IsConnected())
//    {
//      //do the seek..
////      m_pTsReaderFilter->Seek(rtSeek, true);
//    }
//
//    //deliver a end-flush to the codec filter so it will start asking for data again
//    hr=DeliverEndFlush();
//
//    if (!m_pTsReaderFilter->GetAudioPin()->IsConnected())
//    {
//      //tell filter we're done with seeking
////      m_pTsReaderFilter->SeekDone(rtSeek);
//    }
//
//    //set our start time
//    //m_rtStart=rtSeek;
//
//    // and restart the thread
//    Run();
//  }
//  else
//  {
//    //no thread running? then simply seek to the position
//    m_pTsReaderFilter->Seek(rtSeek, false);
//  }
//
//  //tell demuxer to start deliver subtitle packets again
//  demux.SetHoldSubtitle(false);
//
//  //clear flags indiciating that the pin is seeking
//  m_bSeeking = false;
//  LogDebug("sub seek done---");
//}

void CSubtitlePin::UpdateFromSeek()
{
  LogDebug("subPin: UpdateFromSeek, m_rtStart %f, m_dRateSeeking %f",(float)m_rtStart.Millisecs()/1000.0f,(float)m_dRateSeeking);
  m_pTsReaderFilter->SeekPreStart(m_rtStart) ;
  return ;
}

//******************************************************
/// GetAvailable() returns
/// pEarliest -> the earliest (pcr) timestamp in the file
/// pLatest   -> the latest (pcr) timestamp in the file
///
STDMETHODIMP CSubtitlePin::GetAvailable( LONGLONG * pEarliest, LONGLONG * pLatest )
{
//  LogDebug("vid:GetAvailable");
  if (m_pTsReaderFilter->IsTimeShifting())
  {
    CTsDuration duration=m_pTsReaderFilter->GetDuration();
    if (pEarliest)
    {
      //return the startpcr, which is the earliest pcr timestamp available in the timeshifting file
      double d2=duration.StartPcr().ToClock();
      d2*=1000.0f;
      CRefTime mediaTime((LONG)d2);
      *pEarliest= mediaTime;
    }
    if (pLatest)
    {
      //return the endpcr, which is the latest pcr timestamp available in the timeshifting file
      double d2=duration.EndPcr().ToClock();
      d2*=1000.0f;
      CRefTime mediaTime((LONG)d2);
      *pLatest= mediaTime;
    }
    return S_OK;
  }

  //not timeshifting, then leave it to the default sourceseeking class
  //which returns earliest=0, latest=m_rtDuration
  return CSourceSeeking::GetAvailable( pEarliest, pLatest );
}

//******************************************************
/// Returns the file duration in REFERENCE_TIME
/// For nomal .ts files it returns the current pcr - first pcr in the file
/// for timeshifting files it returns the current pcr - the first pcr ever read
/// So the duration keeps growing, even if timeshifting files are wrapped and being resued!
//
STDMETHODIMP CSubtitlePin::GetDuration(LONGLONG *pDuration)
{
  if (m_pTsReaderFilter->IsTimeShifting())
  {
    CTsDuration duration = m_pTsReaderFilter->GetDuration();
    CRefTime totalDuration = duration.TotalDuration();
    m_rtDuration = totalDuration;
  }
  else
  {
    REFERENCE_TIME refTime;
    m_pTsReaderFilter->GetDuration(&refTime);
    m_rtDuration=CRefTime(refTime);
  }
  return CSourceSeeking::GetDuration(pDuration);
}

//******************************************************
/// GetCurrentPosition() simply returns that this is not implemented by this pin
/// reason is that only the audio/video renderer now exactly the
/// current playing position and they do implement GetCurrentPosition()
///
STDMETHODIMP CSubtitlePin::GetCurrentPosition(LONGLONG *pCurrent)
{
  //LogDebug("subPin:GetCurrentPosition");
  return E_NOTIMPL;//CSourceSeeking::GetCurrentPosition(pCurrent);
}

void CSubtitlePin::SetRunningStatus(bool onOff)
{
	m_bRunning = onOff;
}

void CSubtitlePin::LogCurrentPosition()
{
  IFilterGraph* pGraph = m_pTsReaderFilter->GetFilterGraph();
  IMediaSeeking* pMediaSeeking( NULL );

  if( pGraph )
  {
    pGraph->QueryInterface( &pMediaSeeking );
    pGraph->Release();
  }

  LONGLONG pos( 0 );
  pMediaSeeking->GetCurrentPosition( &pos );
  //pMediaSeeking->Release();
  float fPos = (float)pos;
  fPos = ( ( fPos / 10000000 ) );
  LogDebug("sub current position %f", fPos );
}
