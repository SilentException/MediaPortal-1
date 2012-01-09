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
#include <afx.h>
#include <afxwin.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <streams.h>
#include <sbe.h>
#include "tsreader.h"
#include "audiopin.h"
#include "videopin.h"
#include "pmtparser.h"

// For more details for memory leak detection see the alloctracing.h header
#include "..\..\alloctracing.h"

#define MAX_TIME  86400000L
byte MPEG1AudioFormat[] =
{
  0x50, 0x00,       //wFormatTag
  0x02, 0x00,       //nChannels
  0x80, 0xBB, 0x00, 0x00, //nSamplesPerSec
  0x00, 0x7D, 0x00, 0x00, //nAvgBytesPerSec
  0x00, 0x03,       //nBlockAlign
  0x00, 0x00,       //wBitsPerSample
  0x16, 0x00,       //cbSize
  0x02, 0x00,       //wValidBitsPerSample
  0x00, 0xE8,       //wSamplesPerBlock
  0x03, 0x00,       //wReserved
  0x01, 0x00, 0x01,0x00,  //dwChannelMask
  0x01, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

};
extern void LogDebug(const char *fmt, ...) ;
extern DWORD m_tGTStartTime;

CAudioPin::CAudioPin(LPUNKNOWN pUnk, CTsReaderFilter *pFilter, HRESULT *phr,CCritSec* section) :
  CSourceStream(NAME("pinAudio"), phr, pFilter, L"Audio"),
  m_pTsReaderFilter(pFilter),
  CSourceSeeking(NAME("pinAudio"),pUnk,phr,section),
  m_section(section)
{
  m_bConnected=false;
  m_rtStart=0;
  m_dwSeekingCaps =
    AM_SEEKING_CanSeekAbsolute  |
    AM_SEEKING_CanSeekForwards  |
    AM_SEEKING_CanSeekBackwards |
    AM_SEEKING_CanGetStopPos  |
    AM_SEEKING_CanGetDuration |
    //AM_SEEKING_CanGetCurrentPos |
    AM_SEEKING_Source;
  m_bSubtitleCompensationSet=false;
  m_bInFillBuffer=false;
}

CAudioPin::~CAudioPin()
{
  LogDebug("pin:dtor()");
}
STDMETHODIMP CAudioPin::NonDelegatingQueryInterface( REFIID riid, void ** ppv )
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

HRESULT CAudioPin::GetMediaType(CMediaType *pmt)
{
  //LogDebug("audPin:GetMediaType()");
  CDeMultiplexer& demux=m_pTsReaderFilter->GetDemultiplexer();

  int audioIndex = 0;
  demux.GetAudioStream(audioIndex);

  //demux.GetAudioStreamType(demux.GetAudioStream(), *pmt);
  demux.GetAudioStreamType(audioIndex, *pmt);
  return S_OK;
}

void CAudioPin::SetDiscontinuity(bool onOff)
{
  m_bDiscontinuity=onOff;
}

HRESULT CAudioPin::CheckConnect(IPin *pReceivePin)
{
  //LogDebug("audPin:CheckConnect()");
  return CBaseOutputPin::CheckConnect(pReceivePin);
}

HRESULT CAudioPin::DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pRequest)
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

HRESULT CAudioPin::CompleteConnect(IPin *pReceivePin)
{
  m_bInFillBuffer=false;
  LogDebug("audPin:CompleteConnect()");
  HRESULT hr = CBaseOutputPin::CompleteConnect(pReceivePin);
  if (SUCCEEDED(hr))
  {
    LogDebug("audPin:CompleteConnect() done");
    m_bConnected=true;
  }
  else
  {
    LogDebug("audPin:CompleteConnect() failed:%x",hr);
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
  //LogDebug("audPin:CompleteConnect() ok");
  return hr;
}

HRESULT CAudioPin::BreakConnect()
{
  m_bConnected=false;
  //LogDebug("audPin:BreakConnect()");
  return CSourceStream::BreakConnect();
}

void CAudioPin::CreateEmptySample(IMediaSample *pSample)
{
  if (pSample)
  {
    pSample->SetTime(NULL, NULL);
    pSample->SetActualDataLength(0);
    pSample->SetSyncPoint(false);
    pSample->SetDiscontinuity(false);
  }
  else
    LogDebug("audPin: CreateEmptySample() invalid sample!");
}

HRESULT CAudioPin::DoBufferProcessingLoop(void)
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


HRESULT CAudioPin::FillBuffer(IMediaSample *pSample)
{
  try
  {
    CDeMultiplexer& demux=m_pTsReaderFilter->GetDemultiplexer();
    CBuffer* buffer=NULL;
    bool earlyStall = false;
    
    do
    {
      //get file-duration and set m_rtDuration
      GetDuration(NULL);

      //Check if we need to wait for a while
      DWORD timeNow = GET_TIME_NOW();
      while (timeNow < (m_LastFillBuffTime + m_FillBuffSleepTime))
      {      
        Sleep(1);
        timeNow = GET_TIME_NOW();
      }
      m_LastFillBuffTime = timeNow;
      m_FillBuffSleepTime = 1;

      m_bInFillBuffer = true;

     //if the filter is currently seeking to a new position
      //or this pin is currently seeking to a new position then
      //we dont try to read any packets, but simply return...
      if (m_pTsReaderFilter->IsSeeking() || m_pTsReaderFilter->IsStopping())
      {
        //Sleep(20);
        m_FillBuffSleepTime = 5;
        CreateEmptySample(pSample);
        m_bDiscontinuity = TRUE; //Next good sample will be discontinuous
        m_bInFillBuffer = false;
        return NOERROR;
      }
      
      //CAutoLock slock (&demux.m_sectionSeekAudio); //Lock for seeking

      if (!demux.m_bFlushRunning)
      {
        CAutoLock flock (&demux.m_sectionFlushAudio);
        buffer=demux.GetAudio(earlyStall);
      }
      else
      {
        buffer=NULL;
      }

      //did we reach the end of the file
      if (demux.EndOfFile()) // || ((GET_TIME_NOW()-m_LastTickCount > 3000) && !m_pTsReaderFilter->IsTimeShifting()))
      {
        LogDebug("audPin:set eof");
        CreateEmptySample(pSample);
        m_bInFillBuffer = false;
        return S_FALSE; //S_FALSE will notify the graph that end of file has been reached
      }

      //Wait until we have audio (and video, if pin connected) 
      if (!demux.m_bAudioVideoReady || (buffer==NULL))
      {
        //Sleep(10);
        m_FillBuffSleepTime = 10;
        buffer=NULL; //Continue looping
        if (!demux.m_bAudioVideoReady && (m_nNextASD != 0))
        {
          ClearAverageSampleDur();
        }
      }
      else
      {
        m_bPresentSample = true ;
        
        int cntA,cntV ;
        CRefTime firstAudio, lastAudio;
        CRefTime firstVideo, lastVideo;
        cntA = demux.GetAudioBufferPts(firstAudio, lastAudio); // this one...
        cntV = demux.GetVideoBufferPts(firstVideo, lastVideo);
        #define PRESENT_DELAY 0000000
        
        // Ambass : Coming here means we've a minimum of audio(>=1)/video buffers waiting...
        if (!m_pTsReaderFilter->m_bStreamCompensated)
        {
          // Ambass : Find out the best compensation to apply in order to have fast Audio/Video delivery
          CRefTime BestCompensation ;
          CRefTime AddVideoCompensation ;
          LogDebug("Audio Samples : %d, First : %03.3f, Last : %03.3f",cntA, (float)firstAudio.Millisecs()/1000.0f,(float)lastAudio.Millisecs()/1000.0f);
          LogDebug("Video Samples : %d, First : %03.3f, Last : %03.3f",cntV, (float)firstVideo.Millisecs()/1000.0f,(float)lastVideo.Millisecs()/1000.0f);
                    
          if (m_pTsReaderFilter->GetVideoPin()->IsConnected())
          {
            if (firstAudio.Millisecs() < firstVideo.Millisecs())
            {
              if (lastAudio.Millisecs() - 500 < firstVideo.Millisecs()) //Less than 500ms A/V overlap
              {
                BestCompensation = lastAudio - (500*10000) - m_pTsReaderFilter->m_RandomCompensation - m_rtStart ;
                AddVideoCompensation = ( firstVideo - lastAudio + (200*10000) ) ; //Do not fully compensate video - helps with smooth startup
                LogDebug("Compensation : ( Rnd : %d mS ) Audio pts greatly ahead Video pts . Add %03.3f sec of extra video comp to start now !...( real time TV )",(DWORD)m_pTsReaderFilter->m_RandomCompensation/10000,(float)AddVideoCompensation.Millisecs()/1000.0f) ;
              }
              else
              {
                BestCompensation = firstVideo - m_pTsReaderFilter->m_RandomCompensation - m_rtStart ;
                AddVideoCompensation = 0 ; // ( demux.m_IframeSample-firstAudio ) ;
                LogDebug("Compensation : ( Rnd : %d mS ) Audio pts ahead Video Pts ( Recover skipping Audio ) ....",m_pTsReaderFilter->m_RandomCompensation/10000) ;
              }
            }
            else
            {
              BestCompensation = firstAudio-m_rtStart ;
              AddVideoCompensation = 0 ;
              LogDebug("Compensation : Audio pts behind Video Pts ( Recover skipping Video ) ....") ;
            }
            m_pTsReaderFilter->m_RandomCompensation += 500000 ;   // Stupid feature required to have FFRW working with DVXA ( at least ATI.. ) to avoid frozen picture. ( it just moves the sample time a bit !! )
            m_pTsReaderFilter->m_RandomCompensation = m_pTsReaderFilter->m_RandomCompensation % 1000000 ;
          }
          else
          {
            BestCompensation = firstAudio-m_rtStart - 4000000 ;   // Need add delay before playing...
            AddVideoCompensation = 0 ;
          }

          // Here, clock filter should be running, but : EVR  is generally running and need to be compensated accordingly
          //                                             with current elapsed time from "RUN" to avoid beeing late.
          //                                             VMR9 is generally waiting I-frame + 1 or 2 frames to start clock and "RUN"
          // Apply a margin of 200 mS seems safe to avoid being late.
          if (m_pTsReaderFilter->State() == State_Running)
          {
            REFERENCE_TIME RefClock = 0;
            m_pTsReaderFilter->GetMediaPosition(&RefClock) ;

            m_pTsReaderFilter->m_ClockOnStart = RefClock - m_rtStart.m_time ;
            if (m_pTsReaderFilter->m_bLiveTv)
            {
              LogDebug("Elapsed time from pause to Audio/Video ( total zapping time ) : %d mS",GET_TIME_NOW()-m_pTsReaderFilter->m_lastPause);
            }
          }
          else
          {
            m_pTsReaderFilter->m_ClockOnStart=0 ;
          }

          // set the current compensation
          CRefTime compTemp;
          compTemp.m_time=(BestCompensation.m_time - m_pTsReaderFilter->m_ClockOnStart.m_time) - PRESENT_DELAY ;
          m_pTsReaderFilter->SetCompensation(compTemp);
          m_pTsReaderFilter->AddVideoComp = AddVideoCompensation;

          LogDebug("audPin:Compensation:%03.3f, Clock on start %03.3f m_rtStart:%d ",(float)m_pTsReaderFilter->Compensation.Millisecs()/1000.0f, m_pTsReaderFilter->m_ClockOnStart.Millisecs()/1000.0f, m_rtStart.Millisecs());

          //set flag to false so we dont keep compensating
          m_pTsReaderFilter->m_bStreamCompensated = true;
          m_bSubtitleCompensationSet = false;
        }

        // Subtitle filter is "found" only after Run() has been completed
        if(!m_bSubtitleCompensationSet)
        {
          IDVBSubtitle* pDVBSubtitleFilter(m_pTsReaderFilter->GetSubtitleFilter());
          if(pDVBSubtitleFilter)
          {
            LogDebug("audPin:pDVBSubtitleFilter->SetTimeCompensation");
            pDVBSubtitleFilter->SetTimeCompensation(m_pTsReaderFilter->GetCompensation());
            m_bSubtitleCompensationSet=true;
          }
        }

        CRefTime RefTime,cRefTime ;
        bool HasTimestamp ;
        double fTime = 0.0;
        double clock = 0.0;
        double stallPoint = 0.2;
        //check if it has a timestamp
        if ((HasTimestamp=buffer->MediaTime(RefTime)))
        {
          cRefTime = RefTime ;
					cRefTime -= m_rtStart ;
          //adjust the timestamp with the compensation
          cRefTime-= m_pTsReaderFilter->GetCompensation() ;

          REFERENCE_TIME RefClock = 0;
          m_pTsReaderFilter->GetMediaPosition(&RefClock) ;
          clock = (double)(RefClock-m_rtStart.m_time)/10000000.0 ;
          fTime = ((double)cRefTime.m_time/10000000.0) - clock ;

          //Discard late samples at start of play,
          //and samples outside a sensible timing window during play 
          //(helps with signal corruption recovery)
          cRefTime -= m_pTsReaderFilter->m_ClockOnStart.m_time;
          if ((cRefTime.m_time >= 0) && (fTime > ((cRefTime.m_time >= FS_TIM_LIM) ? -0.3 : -0.5)) && (fTime < 2.0))
          {
            //Slowly increase stall point threshold over the first 8 seconds of play
            //to allow audio renderer buffer to build up to 0.4s
            stallPoint = min(0.4, (0.2 + (((double)cRefTime.m_time)/400000000.0)));
            if (fTime > stallPoint)
            {
              //Too early - stall to avoid over-filling of audio decode/renderer buffers
              //Sleep(10);
              m_FillBuffSleepTime = 10;
              buffer = NULL;
              earlyStall = true;
              continue;
            }           
          }
          else //Don't drop samples normally - it upsets the rate matching in the audio renderer
          {
            // Sample is too late.
            m_bPresentSample = false ;
            //m_bDiscontinuity = TRUE; //Next good sample will be discontinuous
          }
          cRefTime += m_pTsReaderFilter->m_ClockOnStart.m_time;
          
          //Calculate sleep times (average sample duration/4)
          m_sampleDuration = GetAverageSampleDur(RefTime.GetUnits());
          if ((m_dRateSeeking == 1.0) && (demux.GetAudioBufferCnt() < 10))
          {
            m_FillBuffSleepTime = (DWORD)min(20, max(1, m_sampleDuration/40000));
          }
        }

        if (m_bPresentSample && (m_dRateSeeking == 1.0) && (buffer->Length() > 0))
        {
          //do we need to set the discontinuity flag?
          if (m_bDiscontinuity || buffer->GetDiscontinuity())
          {
            //ifso, set it
            LogDebug("audPin:set discontinuity L:%d B:%d fTime:%03.3f", m_bDiscontinuity, buffer->GetDiscontinuity(), (float)fTime);
            pSample->SetDiscontinuity(TRUE);

            //  Adding the pmt seems to cause hangs with LAV Audio....
            //  CMediaType mt; 
            //  int audioIndex = 0;
            //  demux.GetAudioStream(audioIndex);
            //  demux.GetAudioStreamType(audioIndex, mt);
            //  pSample->SetMediaType(&mt);            

            m_bDiscontinuity=FALSE;
          }

          if (HasTimestamp)
          {
            //now we have the final timestamp, set timestamp in sample
            REFERENCE_TIME refTime=(REFERENCE_TIME)cRefTime;
            refTime = (REFERENCE_TIME)((double)refTime/m_dRateSeeking);

            pSample->SetSyncPoint(TRUE);

            pSample->SetTime(&refTime,&refTime);
            if (m_pTsReaderFilter->m_ShowBufferAudio || fTime < 0.02)
            {
              LogDebug("Aud/Ref : %03.3f, Compensated = %03.3f ( %0.3f A/V buffers=%02d/%02d), Clk : %f, SampCnt %d, Sleep %d ms, stallPt %03.3f", (float)RefTime.Millisecs()/1000.0f, (float)cRefTime.Millisecs()/1000.0f, fTime,cntA,cntV, clock, m_sampleCount, m_FillBuffSleepTime, (float)stallPoint);
            }
            if (m_pTsReaderFilter->m_ShowBufferAudio) m_pTsReaderFilter->m_ShowBufferAudio--;
          }
          else
          {
            //buffer has no timestamp
            pSample->SetTime(NULL,NULL);
            pSample->SetSyncPoint(FALSE);
          }

          //copy buffer in sample
          BYTE* pSampleBuffer;
          pSample->SetActualDataLength(buffer->Length());
          pSample->GetPointer(&pSampleBuffer);
          memcpy(pSampleBuffer,buffer->Data(),buffer->Length());
          //delete the buffer and return
          delete buffer;
          demux.EraseAudioBuff();
          //Sleep(sampSleepTime) ;
        }
        else
        { // Buffer was not displayed because it was out of date, search for next.
          delete buffer;
          demux.EraseAudioBuff();
          buffer=NULL ;
          m_bDiscontinuity = TRUE; //Next good sample will be discontinuous
          //Sleep(1) ;
        }
        m_sampleCount++ ;
      }      
      earlyStall = false;
    } while (buffer==NULL);

    m_bInFillBuffer = false;
    return NOERROR;
  }

  // Should we return something else than NOERROR when hitting an exception?
  catch(int e)
  {
    LogDebug("audPin:fillbuffer exception %d", e);
  }
  catch(...)
  {
    LogDebug("audPin:fillbuffer exception ...");
  }
  m_FillBuffSleepTime = 5;
  CreateEmptySample(pSample);
  m_bDiscontinuity = TRUE; //Next good sample will be discontinuous  
  m_bInFillBuffer = false; 
  return NOERROR;
}

void CAudioPin::ClearAverageSampleDur()
{
  m_FillBuffSleepTime = 1;
  m_sampleDuration = 10000; //1 ms

  m_llLastComp = 0;
  m_llLastASDts = 0;
  m_nNextASD = 0;
	m_fASDMean = 0;
	m_llASDSumAvg = 0;
  ZeroMemory((void*)&m_pllASD, sizeof(LONGLONG) * NB_ASDSIZE);
}

// Calculate rolling average audio sample duration
LONGLONG CAudioPin::GetAverageSampleDur(LONGLONG timeStamp)
{
  LONGLONG stsDiff;
  if (m_nNextASD > 0)
  {
    stsDiff = timeStamp - m_llLastASDts;
  }
  else
  {
    stsDiff = 10000;
  }
  
  m_llLastASDts = timeStamp;
        
    // Calculate the mean timestamp difference
  if (m_nNextASD >= NB_ASDSIZE)
  {
    m_fASDMean = m_llASDSumAvg / (LONGLONG)NB_ASDSIZE;
  }
  else if (m_nNextASD > 1)
  {
    m_fASDMean = m_llASDSumAvg / (LONGLONG)m_nNextASD;
  }
  else
  {
    m_fASDMean = stsDiff;
  }

    // Update the rolling timestamp difference sum
    // (these values are initialised in OnThreadStartPlay())
  int tempNextASD = (m_nNextASD % NB_ASDSIZE);
  m_llASDSumAvg -= m_pllASD[tempNextASD];
  m_pllASD[tempNextASD] = stsDiff;
  m_llASDSumAvg += stsDiff;
  m_nNextASD++;
  
  //LogDebug("audPin:GetAverageSampleTime, nextASD %d, TsMeanDiff %0.3f, stsDiff %0.3f", m_nNextASD, (float)m_fASDMean/10000.0f, (float)stsDiff/10000.0f);
  
  return m_fASDMean;
}

bool CAudioPin::IsInFillBuffer()
{
  return m_bInFillBuffer;
}

bool CAudioPin::IsConnected()
{
  return m_bConnected;
}

HRESULT CAudioPin::ChangeStart()
{
  m_pTsReaderFilter->SetSeeking(true);
  UpdateFromSeek();
  return S_OK;
}

HRESULT CAudioPin::ChangeStop()
{
  m_pTsReaderFilter->SetSeeking(true);
  UpdateFromSeek();
  return S_OK;
}

HRESULT CAudioPin::ChangeRate()
{
  if( m_dRateSeeking <= 0 )
  {
    m_dRateSeeking = 1.0;  // Reset to a reasonable value.
    return E_FAIL;
  }
  
  LogDebug("audPin: ChangeRate, m_dRateSeeking %f, Force seek done %d, IsSeeking %d",(float)m_dRateSeeking, m_pTsReaderFilter->m_bSeekAfterRcDone, m_pTsReaderFilter->IsSeeking());
  if (!m_pTsReaderFilter->m_bSeekAfterRcDone && !m_pTsReaderFilter->IsSeeking() && !m_pTsReaderFilter->IsWaitDataAfterSeek()) //Don't force seek if another pin has already triggered it
  {
    m_pTsReaderFilter->m_bForceSeekAfterRateChange = true;
    m_pTsReaderFilter->SetSeeking(true);
    UpdateFromSeek();
  }
  return S_OK;
}

//******************************************************
/// Called when thread is about to start delivering data to the codec
///
HRESULT CAudioPin::OnThreadStartPlay()
{
  //set flag to compensate any differences in the stream time & file time
  m_pTsReaderFilter->m_bStreamCompensated = false;
  m_pTsReaderFilter->GetDemultiplexer().m_bAudioVideoReady=false ;

  m_pTsReaderFilter->m_bForcePosnUpdate = true;
  m_pTsReaderFilter->WakeThread();

  //set discontinuity flag indicating to codec that the new data
  //is not belonging to any previous data
  m_bDiscontinuity = TRUE;
  m_bPresentSample = false;
  m_sampleCount = 0;
  m_bInFillBuffer=false;

  m_FillBuffSleepTime = 1;
  m_LastFillBuffTime = GET_TIME_NOW();
  
  ClearAverageSampleDur();

  DWORD thrdID = GetCurrentThreadId();
  LogDebug("audPin:OnThreadStartPlay(%f), rate:%02.2f, threadID:0x%x, GET_TIME_NOW:0x%x", (float)m_rtStart.Millisecs()/1000.0f, m_dRateSeeking, thrdID, GET_TIME_NOW());

  //start playing
  DeliverNewSegment(m_rtStart, m_rtStop, m_dRateSeeking);
  return CSourceStream::OnThreadStartPlay( );
}

void CAudioPin::SetStart(CRefTime rtStartTime)
{
  m_rtStart = rtStartTime ;
}
STDMETHODIMP CAudioPin::SetPositions(LONGLONG *pCurrent, DWORD CurrentFlags, LONGLONG *pStop, DWORD StopFlags)
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
void CAudioPin::UpdateFromSeek()
{
  LogDebug("audPin: UpdateFromSeek, m_rtStart %f, m_dRateSeeking %f",(float)m_rtStart.Millisecs()/1000.0f,(float)m_dRateSeeking);
  m_pTsReaderFilter->SeekPreStart(m_rtStart);

//  LogDebug("audPin: seek done %f/%f",(float)m_rtStart.Millisecs()/1000.0f,(float)m_rtDuration.Millisecs()/1000.0f);
  return ;
}

//******************************************************
/// GetAvailable() returns
/// pEarliest -> the earliest (pcr) timestamp in the file
/// pLatest   -> the latest (pcr) timestamp in the file
///
STDMETHODIMP CAudioPin::GetAvailable( LONGLONG * pEarliest, LONGLONG * pLatest )
{
  //LogDebug("audPin:GetAvailable");
  //if we are timeshifting, the earliest/latest timestamp can change
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
STDMETHODIMP CAudioPin::GetDuration(LONGLONG *pDuration)
{
  //LogDebug("audPin:GetDuration");
  if (m_pTsReaderFilter->IsTimeShifting())
  {
    CTsDuration duration=m_pTsReaderFilter->GetDuration();
    CRefTime totalDuration=duration.TotalDuration();
    m_rtDuration=totalDuration;
  }
  else
  {
    REFERENCE_TIME refTime;
    m_pTsReaderFilter->GetDuration(&refTime);
    m_rtDuration=CRefTime(refTime);
  }
  if (pDuration!=NULL)
  {
    return CSourceSeeking::GetDuration(pDuration);
  }
  return S_OK;
}

//******************************************************
/// GetCurrentPosition() simply returns that this is not implemented by this pin
/// reason is that only the audio/video renderer now exactly the
/// current playing position and they do implement GetCurrentPosition()
///
STDMETHODIMP CAudioPin::GetCurrentPosition(LONGLONG *pCurrent)
{
  //LogDebug("audPin:GetCurrentPosition");
  return E_NOTIMPL;//CSourceSeeking::GetCurrentPosition(pCurrent);
}

STDMETHODIMP CAudioPin::Notify(IBaseFilter * pSender, Quality q)
{
  return E_NOTIMPL;
}