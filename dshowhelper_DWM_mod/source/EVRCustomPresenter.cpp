// Copyright (C) 2005-2011 Team MediaPortal
// http://www.team-mediaportal.com
// 
// MediaPortal is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
// 
// MediaPortal is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with MediaPortal. If not, see <http://www.gnu.org/licenses/>.

#include "StdAfx.h"

#include <streams.h>
#include <atlbase.h>
#include <d3dx9.h>
#include <dvdmedia.h>
#include <mfapi.h>
#include <mferror.h>
#include <afxtempl.h> // CMap
#include <dwmapi.h>

#include "IAVSyncClock.h"
#include "dshowhelper.h"
#include "evrcustompresenter.h"
#include "scheduler.h"
#include "timesource.h"
#include "statsrenderer.h"
#include "autoint.h"

// For more details for memory leak detection see the alloctracing.h header
//#include "..\..\alloctracing.h"

void LogIID(REFIID riid)
{
  LPOLESTR str;
  LPSTR astr;
  StringFromIID(riid, &str);
  UnicodeToAnsi(str, &astr);
  Log("riid: %s", astr);
  CoTaskMemFree(str);
}


void LogGUID(REFGUID guid)
{
  LPOLESTR str;
  LPSTR astr;
  str = (LPOLESTR)CoTaskMemAlloc(200);
  StringFromGUID2(guid, str, 200);
  UnicodeToAnsi(str, &astr);
  Log("guid: %s", astr);
  CoTaskMemFree(str);
}

MPEVRCustomPresenter::MPEVRCustomPresenter(IVMR9Callback* pCallback, IDirect3DDevice9* direct3dDevice, HMONITOR monitor, IBaseFilter* EVRFilter, BOOL pIsWin7):
  m_refCount(1), 
  m_qScheduledSamples(NUM_SURFACES),
  m_EVRFilter(EVRFilter),
  m_bIsWin7(pIsWin7),
  m_bMsVideoCodec(true),
  m_pAVSyncClock(NULL),
  m_dBias(1.0),
  m_dMaxBias(1.1),
  m_dMinBias(0.9),
  m_bBiasAdjustmentDone(false),
  m_dVariableFreq(1.0),
  m_dPreviousVariableFreq(1.0),
  m_iClockAdjustmentsDone(0),
  m_nNextPhDev(0),
  m_avPhaseDiff(0.0),
  m_sumPhaseDiff(0.0),
  m_dummyEvent(INVALID_HANDLE_VALUE)
{
  ZeroMemory((void*)&m_dPhaseDeviations, sizeof(double) * NUM_PHASE_DEVIATIONS);

  timeBeginPeriod(1);
  if (m_pMFCreateVideoSampleFromSurface != NULL)
  {
    HRESULT hr;
    LogRotate();
    if (NO_MP_AUD_REND)
    {
      Log("---------- v1.4.083 part DWM ----------- instance 0x%x", this);
    }
    else
    {
      Log("---------- v0.0.083 part DWM ----------- instance 0x%x", this);
      Log("------- audio renderer testing --------- instance 0x%x", this);
    }
    m_hMonitor = monitor;
    m_pD3DDev = direct3dDevice;
    hr = m_pDXVA2CreateDirect3DDeviceManager9(&m_iResetToken, &m_pDeviceManager);
    if (FAILED(hr))
    {
      Log("Could not create DXVA2 Device Manager");
    }
    else 
    {
      m_pDeviceManager->ResetDevice(direct3dDevice, m_iResetToken);
    }
    m_pCallback                = pCallback;
    m_bEndStreaming            = FALSE;
    m_bInputAvailable          = FALSE;
    m_bFirstInputNotify        = FALSE;
    m_state                    = MP_RENDER_STATE_SHUTDOWN;
    m_bSchedulerRunning        = FALSE;
    m_fRate                    = 1.0f;
    m_iFreeSamples             = 0;
    m_pLastPresSample          = NULL;
    m_nNextJitter              = 0;
    m_llLastPerf               = 0;
    m_fAvrFps                  = 0.0;
    m_rtTimePerFrame           = 0;
    m_llLastWorkerNotification = 0;
    m_bFrameSkipping           = true;
    m_bDVDMenu                 = false;
    m_bScrubbing               = false;
    m_bZeroScrub               = false;
    m_fSeekRate                = m_fRate;
    memset(m_pllJitter,           0, sizeof(m_pllJitter));
    memset(m_pllSyncOffset,       0, sizeof(m_pllSyncOffset));
    memset(m_pllRasterSyncOffset, 0, sizeof(m_pllRasterSyncOffset));

    m_nNextSyncOffset       = 0;
    m_fJitterStdDev          = 0.0;
    m_fSyncOffsetStdDev     = 0.0;
    m_fSyncOffsetAvr        = 0.0;
    m_dOptimumDisplayCycle  = 0.0;
    m_dCycleDifference      = 0.0;
    m_uSyncGlitches         = 0;
    m_rasterSyncOffset      = 0;
    
    m_dEstRefreshCycle      = DEFAULT_FRAME_TIME/10000; //in ms
    m_dD3DRefreshCycle      = m_dEstRefreshCycle;
    m_dD3DRefreshRate       = 1000.0/m_dEstRefreshCycle;
    m_dDetectedScanlineTime = m_dEstRefreshCycle/1124.0;
    m_hnsScanlineTime       = (LONGLONG) (m_dDetectedScanlineTime * 10000.0);
    m_maxScanLine           = 1123;
    m_minVisScanLine        = 5;
    m_maxVisScanLine        = 1080;
    
    m_estRefreshLock        = false;
    m_dEstRefCycDiff        = 0.0;
    
    m_bDwmCompEnabled  = false;
    m_bDWMinit         = false;
    m_bEmptyQueue      = false;
    m_dwmBuffers       = 0;
    m_hDwmWinHandle    = NULL;
    
    // sample time correction variables
    m_LastScheduledUncorrectedSampleTime  = -1;
    m_DetectedFrameTimePos                = 0;
    m_DectedSum                           = 0;
    m_DetectedFrameTime                   = -1.0;
    m_DetFrameTimeAve                     = -1.0;
    m_DetectedLock                        = false;
    m_DetectedFrameTimeStdDev             = 0;
    m_LastEndOfPaintScanline       = 0;
    m_LastStartOfPaintScanline     = 0;
    m_frameRateRatio              = 0;
    m_rawFRRatio                  = 0;
    
    m_numFilters = 0;
    ResetTraceStats();
    ResetFrameStats();
    
    m_pD3DDev->GetDisplayMode(0, &m_displayMode);

    m_bDrawStats = false;
    m_dummyEvent = CreateEvent(NULL, TRUE, FALSE, NULL); //Placeholder event for wait functions 
  }
    
  for (int i = 0; i < 2; i++)
  {
    if (EstimateRefreshTimings(8, THREAD_PRIORITY_TIME_CRITICAL))
    {
      break; //only go round the loop again if we don't get a good result
    }
  }

  m_pStatsRenderer = new StatsRenderer(this, m_pD3DDev);
  
  //DwmEnableMMCSSOnOff(false);
}

void MPEVRCustomPresenter::SetFrameSkipping(bool onOff)
{
  Log("Evr Enable frame skipping:%d",onOff);
  m_bFrameSkipping = onOff;
}


void MPEVRCustomPresenter::EnableDrawStats(bool enable)
{
  // Reset stats when hiding them. This will easen up the troubleshooting / debugging
  if (m_bDrawStats && !enable)
  {
    ResetEVRStatCounters();
  }
  
  if (enable)
  {
    Log("Stats enabled");
  }
  else
  {
    Log("Stats disabled");
  }
  
  m_bDrawStats = enable;
}


void MPEVRCustomPresenter::ResetEVRStatCounters()
{
  m_bResetStats = true;
}

void MPEVRCustomPresenter::ReleaseCallback()
{
  CAutoLock cLock(&m_lockCallback);
  m_pCallback = NULL;
}

MPEVRCustomPresenter::~MPEVRCustomPresenter()
{
  Log("EVRCustomPresenter::dtor - instance 0x%x", this);
  
  if (m_pCallback)
  {
    m_pCallback->PresentImage(0, 0, 0, 0, 0, 0);
  }
  if(m_pAVSyncClock)
  {
    SAFE_RELEASE(m_pAVSyncClock);
  }
  StopWorkers();
  ReleaseSurfaces();
  m_pMediaType.Release();
  m_pDeviceManager =  NULL;
  for (int i=0; i < NUM_SURFACES; i++)
  {
    m_vFreeSamples[i] = NULL;
  }
  delete m_pStatsRenderer;
  timeEndPeriod(1);
  //DwmReset(true);
  Log("Done");
}  

void MPEVRCustomPresenter::DwmInit(UINT buffers, UINT rfshPerFrame)
{
  if (!ENABLE_DWM_SETUP || m_bDWMinit || (GetDisplayCycle() > DWM_REFRESH_THRESH))
  {
    return;
  }
    
  Log("EVRCustomPresenter::DwmInit, frame = %d", m_iFramesDrawn);  
  //Initialise the DWM parameters
  DwmGetState();
  
  DwmFlush();
  DwmSetParameters(TRUE, buffers, rfshPerFrame); //'Source rate' mode
  WaitForSingleObject(m_dummyEvent, 50); //Wait for 50ms
  
  DwmFlush();
  DwmSetParameters(FALSE, buffers, rfshPerFrame); //'Display rate' mode
  WaitForSingleObject(m_dummyEvent, 50); //Wait for 50ms

  DwmEnableMMCSSOnOff(DWM_ENABLE_MMCSS);
  WaitForSingleObject(m_dummyEvent, 50); //Wait for 50ms
  
  m_bDWMinit = true;
}  


void MPEVRCustomPresenter::DwmReset(bool newWinHand)
{
  if (!ENABLE_DWM_SETUP || !ENABLE_DWM_RESET || !m_bDWMinit) 
  {
    return;
  }

  Log("EVRCustomPresenter::DwmReset");  
  //Reset the DWM parameters
  if (!m_hDwmWinHandle || newWinHand)
  {
    DwmGetState();
  }
  DwmEnableMMCSSOnOff(false);
  
  DwmFlush();
  DwmSetParameters(TRUE, 2, 1); //'Source rate' mode
  WaitForSingleObject(m_dummyEvent, 50); //Wait for 50ms
  
  DwmFlush();
  DwmSetParameters(FALSE, 2, 1); //'Display rate' mode
  WaitForSingleObject(m_dummyEvent, 50); //Wait for 50ms
  
  m_bDWMinit = false;
}  


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::GetParameters(__RPC__out DWORD *pdwFlags, __RPC__out DWORD *pdwQueue)
{
  Log("GetParameters");
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::Invoke(__RPC__in_opt IMFAsyncResult *pAsyncResult)
{
  Log("Invoke");
  return S_OK;
}


// IUnknown
HRESULT MPEVRCustomPresenter::QueryInterface(REFIID riid, void** ppvObject)
{
  HRESULT hr = E_NOINTERFACE;
  if (ppvObject == NULL)
  {
    hr = E_POINTER;
  }
  else if (riid == IID_IMFVideoDeviceID)
  {
    *ppvObject = static_cast<IMFVideoDeviceID*>(this);
    AddRef();
    hr = S_OK;
  }
  else if (riid == IID_IMFTopologyServiceLookupClient)
  {
    *ppvObject = static_cast<IMFTopologyServiceLookupClient*>(this);
    AddRef();
    hr = S_OK;
  }
  else if (riid == IID_IMFVideoPresenter)
  {
    *ppvObject = static_cast<IMFVideoPresenter*>(this);
    AddRef();
    hr = S_OK;
  }
  else if (riid == IID_IMFGetService)
  {
    *ppvObject = static_cast<IMFGetService*>(this);
    AddRef();
    hr = S_OK;
  }
  else if (riid == IID_IQualProp)
  {
    *ppvObject = static_cast<IQualProp*>(this);
    AddRef();
    hr = S_OK;
  }
  else if (riid == IID_IMFRateSupport)
  {
    *ppvObject = static_cast<IMFRateSupport*>(this);
    AddRef();
    hr = S_OK;
  }
  else if (riid == IID_IMFVideoDisplayControl)
  {
    *ppvObject = static_cast<IMFVideoDisplayControl*>(this);
    AddRef();
    hr = S_OK;
  }
  else if (riid == IID_IEVRTrustedVideoPlugin)
  {
    *ppvObject = static_cast<IEVRTrustedVideoPlugin*>(this);
    AddRef();
    hr = S_OK;
  }
  else if (riid == IID_IMFVideoPositionMapper)
  {
    *ppvObject = static_cast<IMFVideoPositionMapper*>(this);
    AddRef();
    hr = S_OK;
  }
  else if (riid == IID_IUnknown)
  {
    *ppvObject = static_cast<IUnknown*>(static_cast<IMFVideoDeviceID*>(this));
    AddRef();
    hr = S_OK;
  }
  else
  {
    LogIID(riid);
    *ppvObject = NULL;
    hr = E_NOINTERFACE;
  }
  CHECK_HR(hr, "QueryInterface failed")
  return hr;
}


ULONG MPEVRCustomPresenter::AddRef()
{
  return InterlockedIncrement(&m_refCount);
}


ULONG MPEVRCustomPresenter::Release()
{
  ULONG ret = InterlockedDecrement(&m_refCount);
  if (ret == 0)
  {
    Log("MPEVRCustomPresenter::Cleanup()");
    delete this;
  }
  else
  {
    //Log("MPEVRCustomPresenter::Release(), m_refCount: 0x%x", m_refCount);
  }
  return ret;
}

HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::GetSlowestRate(MFRATE_DIRECTION eDirection, BOOL fThin, __RPC__out float *pflRate)
{
  Log("GetSlowestRate");
  // There is no minimum playback rate, so the minimum is zero.
  *pflRate = 0;
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::GetFastestRate(MFRATE_DIRECTION eDirection, BOOL fThin, __RPC__out float *pflRate)
{
  Log("GetFastestRate");
  float fMaxRate = 0.0f;

  // Get the maximum *forward* rate.
  fMaxRate = FLT_MAX;

  // For reverse playback, it's the negative of fMaxRate.
  if (eDirection == MFRATE_REVERSE)
  {
    fMaxRate = -fMaxRate;
  }

  *pflRate = fMaxRate;

  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::IsRateSupported(BOOL fThin, float flRate, __RPC__inout_opt float *pflNearestSupportedRate)
{
  Log("IsRateSupported");
  if (pflNearestSupportedRate != NULL)
  {
    *pflNearestSupportedRate = flRate;
  }
  return S_OK;
}


HRESULT MPEVRCustomPresenter::GetDeviceID(IID* pDeviceID)
{
  Log("GetDeviceID");
  if (pDeviceID == NULL)
  {
    return E_POINTER;
  }
  *pDeviceID = __uuidof(IDirect3DDevice9);
  return S_OK;
}


HRESULT MPEVRCustomPresenter::InitServicePointers(IMFTopologyServiceLookup *pLookup)
{
  Log("InitServicePointers");
  HRESULT hr = S_OK;
  DWORD cCount = 0;

  // just to make sure....
  ReleaseServicePointers();

  // Ask for the mixer
  cCount = 1;
  hr = pLookup->LookupService(
    MF_SERVICE_LOOKUP_GLOBAL,   // Not used
    0,                          // Reserved
    MR_VIDEO_MIXER_SERVICE,     // Service to look up
    __uuidof(IMFTransform),     // Interface to look up
    (void**)&m_pMixer,          // Receives the pointer.
    &cCount);                   // Number of pointers

  if (SUCCEEDED(hr))
  {
    Log("Found mixers: %d", cCount);
    ASSERT(cCount == 0 || cCount == 1);
  }

  // Ask for the clock
  cCount = 1;
  hr = pLookup->LookupService(
    MF_SERVICE_LOOKUP_GLOBAL,   // Not used
    0,                          // Reserved
    MR_VIDEO_RENDER_SERVICE,    // Service to look up
    __uuidof(IMFClock),         // Interface to look up
    (void**)&m_pClock,          // Receives the pointer.
    &cCount);                   // Number of pointers


  if (SUCCEEDED(hr))
  {
    Log("Found clock: %d", cCount);
    ASSERT(cCount == 0 || cCount == 1);
  }

  // Ask for the event-sink
  cCount = 1;
  hr = pLookup->LookupService(
    MF_SERVICE_LOOKUP_GLOBAL,   // Not used
    0,                          // Reserved
    MR_VIDEO_RENDER_SERVICE,    // Service to look up
    __uuidof(IMediaEventSink),  // Interface to look up
    (void**)&m_pEventSink,      // Receives the pointer.
    &cCount);                   // Number of pointers

  if (SUCCEEDED(hr))
  {
    Log("Found event sink: %d", cCount);
    ASSERT(cCount == 0 || cCount == 1);
  }

  return S_OK;
}


HRESULT MPEVRCustomPresenter::ReleaseServicePointers()
{
  Log("ReleaseServicePointers");
  // on some channel changes it may happen that ReleaseServicePointers is called only after InitServicePointers 
  // is called to avoid this rare condition, we only release when not in state begin_streaming
  m_pMediaType.Release();
  m_pMixer.Release();
  m_pClock.Release();
  m_pEventSink.Release();
  return S_OK;
}


HRESULT MPEVRCustomPresenter::GetCurrentMediaType(IMFVideoMediaType** ppMediaType)
{
  Log("GetCurrentMediaType");
  HRESULT hr = S_OK;

  if (ppMediaType == NULL)
  {
    return E_POINTER;
  }

  if (m_pMediaType == NULL)
  {
    CHECK_HR(hr = MF_E_NOT_INITIALIZED, "MediaType is NULL");
  }

  CHECK_HR(hr = m_pMediaType->QueryInterface(__uuidof(IMFVideoMediaType), (void**)ppMediaType), "Query interface failed in GetCurrentMediaType");

  Log("GetCurrentMediaType done");
  return hr;
}


HRESULT MPEVRCustomPresenter::TrackSample(IMFSample *pSample)
{
  HRESULT hr = S_OK;
  IMFTrackedSample *pTracked = NULL;

  CHECK_HR(hr = pSample->QueryInterface(__uuidof(IMFTrackedSample), (void**)&pTracked), "Cannot get Interface IMFTrackedSample");
  CHECK_HR(hr = pTracked->SetAllocator(this, NULL), "SetAllocator failed");

  SAFE_RELEASE(pTracked);
  return hr;
}

// 'hnsTimeOffset' can be used to correct A/V sync - positive values will cause samples to be presented earlier
HRESULT MPEVRCustomPresenter::GetTimeToSchedule(IMFSample* pSample, LONGLONG *phnsDelta, LONGLONG *hnsSystemTime, LONGLONG hnsTimeOffset)
{
  LONGLONG hnsPresentationTime = 0; // Target presentation time
  LONGLONG hnsTimeNow = 0;          // Correlated presentation time
  LONGLONG hnsSysNow = 0;          // Correlated system time
  LONGLONG hnsDelta = 0;
  HRESULT  hr;
  

  if (m_pClock == NULL)
  {
    *phnsDelta = 0;
    *hnsSystemTime = GetCurrentTimestamp();
    return S_OK;
  }

  hr = pSample->GetSampleTime(&hnsPresentationTime);
  if (SUCCEEDED(hr))
  {
    if (hnsPresentationTime == 0)
    {
      // immediate presentation
      *phnsDelta = 0;
      *hnsSystemTime = GetCurrentTimestamp();
      return S_OK;
    }
    CHECK_HR(hr = m_pClock->GetCorrelatedTime(0, &hnsTimeNow, &hnsSysNow), "Could not get correlated time!");
    *hnsSystemTime = GetCurrentTimestamp();
    hnsTimeNow = hnsTimeNow + (*hnsSystemTime - hnsSysNow) + hnsTimeOffset; //correct the value and add offset
      // Calculate the amount of time until the sample's presentation time. A negative value means the sample is late.
    hnsDelta = hnsPresentationTime - hnsTimeNow;
  }
  else
  {
    Log("Could not get sample time from %p!", pSample);
    *phnsDelta = 0;
    *hnsSystemTime = GetCurrentTimestamp();
    return hr;
  }

  // if off more than a second and not scrubbing and not DVD Menu
  if (hnsDelta > 100000000 && !m_bScrubbing && !m_bDVDMenu)
  {
    Log("dangerous and unlikely time to schedule [%p]: %I64d. scheduled time: %I64d, now: %I64d",
      pSample, hnsDelta, hnsPresentationTime, hnsTimeNow);
  }
  LOG_TRACE("Due: %I64d, Calculated delta: %I64d (rate: %f)", hnsPresentationTime, hnsDelta, m_fRate);

  *phnsDelta = hnsDelta;
  
  if (*phnsDelta == 0)
  {
    *phnsDelta = 1;   // Make sure valid presentation time is never zero
  }
  
  return hr;
}


HRESULT MPEVRCustomPresenter::GetAspectRatio(CComPtr<IMFMediaType> pType, int* piARX, int* piARY)
{
  HRESULT hr;
  UINT32 u32;
  if (SUCCEEDED(pType->GetUINT32(MF_MT_SOURCE_CONTENT_HINT, &u32)))
  {
    Log("Getting aspect ratio 'MediaFoundation style'");
    switch (u32)
    {
    case MFVideoSrcContentHintFlag_None:
      Log("Aspect ratio unknown");
    break;
    case MFVideoSrcContentHintFlag_16x9:
      Log("Source is 16:9 within 4:3!");
      *piARX = 16;
      *piARY = 9;
    break;
    case MFVideoSrcContentHintFlag_235_1:
      Log("Source is 2.35:1 within 16:9 or 4:3");
      *piARX = 47;
      *piARY = 20;
    break;
    default:
      Log("Unkown aspect ratio flag: %d", u32);
    break;
    }
  }
  else
  {
    // Try old DirectShow-Header, if above does not work
    Log("Getting aspect ratio 'DirectShow style'");
    AM_MEDIA_TYPE* pAMMediaType;
    CHECK_HR(
      hr = pType->GetRepresentation(FORMAT_VideoInfo2, (void**)&pAMMediaType),
      "Getting DirectShow Video Info failed");
    if (SUCCEEDED(hr))
    {
      VIDEOINFOHEADER2* vheader = (VIDEOINFOHEADER2*)pAMMediaType->pbFormat;
      *piARX = vheader->dwPictAspectRatioX;
      *piARY = vheader->dwPictAspectRatioY;
      pType->FreeRepresentation(FORMAT_VideoInfo2, (void*)pAMMediaType);
    }
    else
    {
      Log("Could not get directshow representation.");
    }
  }
  return hr;
}


HRESULT MPEVRCustomPresenter::SetMediaType(CComPtr<IMFMediaType> pType, BOOL* pbHasChanged)
{
  if (pType == NULL)
  {
    m_pMediaType.Release();
    return S_OK;
  }

  HRESULT hr = S_OK;
  LARGE_INTEGER u64;

  CHECK_HR(pType->GetUINT64(MF_MT_FRAME_SIZE, (UINT64*)&u64), "Getting Framesize failed!");

  MFVideoArea Area;
  UINT32 rSize;
  CHECK_HR(pType->GetBlob(MF_MT_GEOMETRIC_APERTURE, (UINT8*)&Area, sizeof(Area), &rSize), "Failed to get MF_MT_GEOMETRIC_APERTURE");
  m_iVideoWidth = u64.HighPart;
  m_iVideoHeight = u64.LowPart;
  // use video size as default value for aspect ratios
  m_iARX = m_iVideoWidth;
  m_iARY = m_iVideoHeight;
  CHECK_HR(GetAspectRatio(pType, &m_iARX, &m_iARY), "Failed to get aspect ratio");
  Log("New format: %dx%d, Ratio: %d:%d",  m_iVideoWidth, m_iVideoHeight, m_iARX, m_iARY);

  GUID subtype;
  CHECK_HR(pType->GetGUID(MF_MT_SUBTYPE, &subtype), "Failed to get MF_MT_SUBTYPE");
  LogGUID(subtype);
  if (m_pMediaType == NULL)
  {
    *pbHasChanged = TRUE;
  }
  else
  {
    BOOL doMatch;
    hr = m_pMediaType->Compare(pType, MF_ATTRIBUTES_MATCH_ALL_ITEMS, &doMatch);
    if (SUCCEEDED(hr))
    {
      *pbHasChanged = !doMatch;
    }
    else
    {
      hr = S_OK;
      Log("Could not compare media type to old media type. assuming a change (0x%x)", hr);
      *pbHasChanged = TRUE;
    }
  }
  m_pMediaType = pType;
  if (!*pbHasChanged)
  {
    Log("Detected same media type as last one.");
  }
  return S_OK;
}


void MPEVRCustomPresenter::ReAllocSurfaces()
{
  Log("ReallocSurfaces");
  //All threads must be paused by the caller
  //  CAutoLock tLock(&m_timerParams.csLock);
  //  CAutoLock wLock(&m_workerParams.csLock);
  //  CAutoLock sLock(&m_schedulerParams.csLock);
  ReleaseSurfaces();

  // set the presentation parameters
  D3DPRESENT_PARAMETERS d3dpp;
  ZeroMemory(&d3dpp, sizeof(d3dpp));
  d3dpp.BackBufferWidth = m_iVideoWidth;
  d3dpp.BackBufferHeight = m_iVideoHeight;
  d3dpp.BackBufferCount = 1;
  // TODO check media type for correct format!
  d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
  d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  d3dpp.Windowed = true;
  d3dpp.EnableAutoDepthStencil = false;
  d3dpp.AutoDepthStencilFormat = D3DFMT_X8R8G8B8;
  d3dpp.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
  d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

  HANDLE hDevice;
  IDirect3DDevice9* pDevice;
  CHECK_HR(m_pDeviceManager->OpenDeviceHandle(&hDevice), "Cannot open device handle");
  CHECK_HR(m_pDeviceManager->LockDevice(hDevice, &pDevice, TRUE), "Cannot lock device");
  HRESULT hr;
  Log("Textures will be %dx%d", m_iVideoWidth, m_iVideoHeight);
  for (int i = 0; i < NUM_SURFACES; i++)
  {
    hr = pDevice->CreateTexture(m_iVideoWidth, m_iVideoHeight, 1,
      D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT,
      &textures[i], NULL);
    if (FAILED(hr))
    {
      Log("Could not create offscreen surface. Error 0x%x", hr);
    }
    CHECK_HR(textures[i]->GetSurfaceLevel(0, &surfaces[i]), "Could not get surface from texture");

    hr = m_pMFCreateVideoSampleFromSurface(surfaces[i], &samples[i]);
    if (FAILED(hr))
    {
      Log("CreateVideoSampleFromSurface failed: 0x%x", hr);
      return;
    }
    Log("Adding sample: 0x%x", samples[i]);
    m_vFreeSamples[i] = samples[i];
    m_vAllSamples[i] = samples[i];
  }
  m_iFreeSamples = NUM_SURFACES;
  m_pLastPresSample = NULL;
  CHECK_HR(m_pDeviceManager->UnlockDevice(hDevice, FALSE), "failed: Unlock device");
  Log("Releasing device: %d", pDevice->Release());
  CHECK_HR(m_pDeviceManager->CloseDeviceHandle(hDevice), "failed: CloseDeviceHandle");
  
  m_pStatsRenderer->VideSizeChanged();

  Log("ReallocSurfaces done");
}


HRESULT MPEVRCustomPresenter::CreateProposedOutputType(IMFMediaType* pMixerType, IMFMediaType** pType)
{
  HRESULT hr;
  LARGE_INTEGER i64Size;

  hr = m_pMFCreateMediaType(pType);
  if (SUCCEEDED(hr))
  {
    CHECK_HR(hr = pMixerType->CopyAllItems(*pType), "failed: CopyAllItems. Could not clone media type");
    if (SUCCEEDED(hr))
    {
      Log("Successfully cloned media type");
    }
    (*pType)->SetUINT32(MF_MT_PAN_SCAN_ENABLED, 0);

    i64Size.HighPart = 800;
    i64Size.LowPart   = 600;

    i64Size.HighPart = 1;
    i64Size.LowPart  = 1;

    CComPtr<IMFVideoMediaType> pVideoMediaType;

    AM_MEDIA_TYPE *pAMMedia = NULL;
    MFVIDEOFORMAT *videoFormat = NULL;

    CHECK_HR(pMixerType->GetRepresentation(FORMAT_MFVideoFormat, (void**)&pAMMedia), "pMixerType->GetRepresentation failed!");
    videoFormat = (MFVIDEOFORMAT*)pAMMedia->pbFormat;
    hr = m_pMFCreateVideoMediaType(videoFormat, &pVideoMediaType);

    if (hr == 0 && videoFormat->videoInfo.FramesPerSecond.Numerator != 0)
    {
      if (!m_bMsVideoCodec || (m_bMsVideoCodec && (m_rtTimePerFrame == 0)))
        m_rtTimePerFrame = (10000000I64*videoFormat->videoInfo.FramesPerSecond.Denominator)/videoFormat->videoInfo.FramesPerSecond.Numerator;

      Log("Time Per Frame: %.3f ms", (double)m_rtTimePerFrame/10000.0);
      // HD
      if (videoFormat->videoInfo.dwHeight >= 720 || videoFormat->videoInfo.dwWidth >= 1280)
      {
        Log("Setting MFVideoTransferMatrix_BT709");
        (*pType)->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
      }
      else // SD
      {
        Log("Setting MFVideoTransferMatrix_BT601");
        (*pType)->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT601);
      }
    }
    else
    {
      LARGE_INTEGER frameRate;
      CHECK_HR((*pType)->GetUINT64(MF_MT_FRAME_RATE, (UINT64*)&frameRate.QuadPart), "Failed to get MF_MT_FRAME_RATE");
      Log("MF_MT_FRAME_RATE: %.3f fps", ((double)frameRate.HighPart/(double)frameRate.LowPart));
      
      if ( (!m_bMsVideoCodec || (m_bMsVideoCodec && (m_rtTimePerFrame == 0))) && frameRate.HighPart != 0)
        m_rtTimePerFrame = (10000000*(LONGLONG)frameRate.LowPart)/(LONGLONG)frameRate.HighPart;

      Log("Setting MFVideoTransferMatrix using m_pMFCreateVideoMediaType failed, trying MF_MT_FRAME_SIZE");
      CHECK_HR((*pType)->GetUINT64(MF_MT_FRAME_SIZE, (UINT64*)&i64Size.QuadPart), "Failed to get MF_MT_FRAME_SIZE");

      if (i64Size.LowPart >= 720 || i64Size.HighPart >= 1280)
      {
        Log("Setting MFVideoTransferMatrix_BT709");
        (*pType)->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
      }
      else // SD
      {
        Log("Setting MFVideoTransferMatrix_BT601");
        (*pType)->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT601);
      }
    }
    
    UINT32 interlaceMode;
    CHECK_HR((*pType)->GetUINT32(MF_MT_INTERLACE_MODE, &interlaceMode), "Failed to get MF_MT_INTERLACE_MODE");
    Log("MF_MT_INTERLACE_MODE: %d", interlaceMode);

    LARGE_INTEGER frameRate;
    CHECK_HR((*pType)->GetUINT64(MF_MT_FRAME_RATE, (UINT64*)&frameRate.QuadPart), "Failed to get MF_MT_FRAME_RATE");
    Log("MF_MT_FRAME_RATE: %.3f fps", ((double)frameRate.HighPart/(double)frameRate.LowPart));
    
    if (m_rtTimePerFrame == 0)
    {
      // if fps information is not provided use default (workaround for possible bugs)
      m_rtTimePerFrame = (LONGLONG)(10000 * GetDisplayCycle());
      Log("No time per frame available using default: %f", GetDisplayCycle());
    }

    CHECK_HR((*pType)->GetUINT64(MF_MT_FRAME_SIZE, (UINT64*)&i64Size.QuadPart), "Failed to get MF_MT_FRAME_SIZE");
    Log("Frame size: %dx%d", i64Size.HighPart, i64Size.LowPart);

    MFVideoArea Area;
    UINT32 rSize;
    ZeroMemory(&Area, sizeof(MFVideoArea));
    // TODO get the real screen size, and calculate area corresponding to the given aspect ratio
    Area.Area.cx = min(800, i64Size.HighPart);
    Area.Area.cy = min(450, i64Size.LowPart);
    // for hardware scaling, use the following line:
    //(*pType)->SetBlob(MF_MT_GEOMETRIC_APERTURE, (UINT8*)&Area, sizeof(MFVideoArea));
    CHECK_HR((*pType)->GetBlob(MF_MT_GEOMETRIC_APERTURE, (UINT8*)&Area, sizeof(Area), &rSize), "Failed to get MF_MT_GEOMETRIC_APERTURE");
    Log("Aperture size: %x:%x, %dx%d", Area.OffsetX.value, Area.OffsetY.value, Area.Area.cx, Area.Area.cy);

    (*pType)->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_0_255);
  }
  return hr;
}


HRESULT MPEVRCustomPresenter::LogOutputTypes()
{
  Log("--Dumping output types----");
  HRESULT hr = S_OK;
  BOOL fFoundMediaType = FALSE;

  CComPtr<IMFMediaType> pMixerType;
  CComPtr<IMFMediaType> pType;

  if (!m_pMixer)
  {
    return MF_E_INVALIDREQUEST;
  }

  // Loop through all of the mixer's proposed output types.
  DWORD iTypeIndex = 0;
  while (!fFoundMediaType && (hr != MF_E_NO_MORE_TYPES))
  {
    pMixerType.Release();
    pType.Release();
    Log("Testing media type...");

    // Step 1. Get the next media type supported by mixer.
    hr = m_pMixer->GetOutputAvailableType(0, iTypeIndex++, &pMixerType);
    if (FAILED(hr))
    {
      if (hr != MF_E_NO_MORE_TYPES)
      {
        Log("stopping, hr=0x%x!", hr);
        break;
      }
    }
    int arx, ary;
    GetAspectRatio(pMixerType, &arx, &ary);
    Log("Aspect ratio: %d:%d", arx, ary);
    UINT32 interlaceMode;
    pMixerType->GetUINT32(MF_MT_INTERLACE_MODE, &interlaceMode);
    Log("Interlace mode: %d", interlaceMode);
    
    LARGE_INTEGER frameRate;
    pMixerType->GetUINT64(MF_MT_FRAME_RATE, (UINT64*)&frameRate.QuadPart);
    Log("Frame Rate: %d / %d", frameRate.HighPart, frameRate.LowPart);
    
    GUID subtype;
    CHECK_HR(pMixerType->GetGUID(MF_MT_SUBTYPE, &subtype), "Failed to get MF_MT_SUBTYPE");
    LogGUID(subtype);
  }
  Log("--Dumping output types done----");
  return S_OK;
}


HRESULT MPEVRCustomPresenter::RenegotiateMediaOutputType()
{
  //  CAutoLock tLock(&m_timerParams.csLock);
  //  CAutoLock wLock(&m_workerParams.csLock);
  //  CAutoLock sLock(&m_schedulerParams.csLock);
  m_bFirstInputNotify = FALSE;
  Log("RenegotiateMediaOutputType");
  HRESULT hr = S_OK;
  BOOL fFoundMediaType = FALSE;

  CComPtr<IMFMediaType> pMixerType;
  CComPtr<IMFMediaType> pType;

  if (!m_pMixer)
  {
    return MF_E_INVALIDREQUEST;
  }

  // Loop through all of the mixer's proposed output types.
  DWORD iTypeIndex = 0;
  while (!fFoundMediaType && (hr != MF_E_NO_MORE_TYPES))
  {
    pMixerType.Release();
    pType.Release();
    Log("Testing media type...");

    // Step 1. Get the next media type supported by mixer.
    hr = m_pMixer->GetOutputAvailableType(0, iTypeIndex++, &pMixerType);
    if (FAILED(hr))
    {
      Log("ERR: Cannot find usable media type!");
      break;
    }

    // Step 2. Check if we support this media type.
    if (SUCCEEDED(hr))
    {
      hr = S_OK; //IsMediaTypeSupported(pMixerType);
    }

    // Step 3. Adjust the mixer's type to match our requirements.
    if (SUCCEEDED(hr))
    {
      // Create a clone of the suggested outputtype
      hr = CreateProposedOutputType(pMixerType, &pType);
    }

    // Step 4. Check if the mixer will accept this media type.
    if (SUCCEEDED(hr))
    {
      hr = m_pMixer->SetOutputType(0, pType, MFT_SET_TYPE_TEST_ONLY);
    }

    // Step 5. Try to set the media type on ourselves.
    if (SUCCEEDED(hr))
    {
      Log("New media type successfully negotiated!");
      BOOL bHasChanged;
      hr = SetMediaType(pType, &bHasChanged);
      if (SUCCEEDED(hr))
      {
        if (bHasChanged)
        {
          ReAllocSurfaces();
        }
      }
      else
      {
        Log("ERR: Could not set media type on self: 0x%x!", hr);
      }
    }

    // Step 6. Set output media type on mixer.
    if (SUCCEEDED(hr))
    {
      Log("Setting media type on mixer");
      hr = m_pMixer->SetOutputType(0, pType, 0);

      // If something went wrong, clear the media type.
      if (FAILED(hr))
      {
        Log("Could not set output type: 0x%x", hr);
        SetMediaType(NULL, NULL);
      }
    }

    if (SUCCEEDED(hr))
    {
      SetupAudioRenderer();
      fFoundMediaType = TRUE;
    }
  }

  return hr;
}


HRESULT MPEVRCustomPresenter::GetFreeSample(IMFSample** ppSample)
{
  CAutoLock sLock(&m_lockSamples);
  //TIME_LOCK(&m_lockSamples, 50000, "GetFreeSample");
  LOG_TRACE("Trying to get free sample, size: %d", m_iFreeSamples);
  if (m_iFreeSamples == 0 || m_qScheduledSamples.IsFull())
  {
    return E_FAIL;
  }
  m_iFreeSamples--;
  *ppSample = m_vFreeSamples[m_iFreeSamples];
  m_vFreeSamples[m_iFreeSamples] = NULL;

  return S_OK;
}


void MPEVRCustomPresenter::Flush(BOOL forced)
{
  DwmFlush(); //Just in case...

  CAutoLock sLock(&m_lockSamples);
  
  if (!m_bDVDMenu || forced)
  {
    Log("Flushing: size=%d", m_qScheduledSamples.Count());

    for (int i = 0; i < NUM_SURFACES; i++)
    {
      m_vFreeSamples[i] = m_vAllSamples[i];
    }
    m_iFreeSamples = NUM_SURFACES;
    m_pLastPresSample = NULL;
    m_qScheduledSamples.Clear();
  }
  else
  {
    Log("Not flushing: size=%d", m_qScheduledSamples.Count());
  }
  
  m_bFlush = FALSE;
}



void MPEVRCustomPresenter::ReturnSample(IMFSample* pSample, BOOL tryNotify)
{
  CAutoLock sLock(&m_lockSamples);
  //TIME_LOCK(&m_lockSamples, 50000, "ReturnSample")
  LOG_TRACE("Sample returned: now having %d samples", m_iFreeSamples+1);
  m_vFreeSamples[m_iFreeSamples] = pSample;
  m_iFreeSamples++;
  
  if (m_qScheduledSamples.IsEmpty())
  {
    LOG_TRACE("No scheduled samples, queue was empty -> todo, CheckForEndOfStream()");
    CheckForEndOfStream();
  }

  if (tryNotify && (m_iFreeSamples > 0) && !m_qScheduledSamples.IsFull())
  {
    NotifyWorker(FALSE);
  }
}

void MPEVRCustomPresenter::UpdateLastPresSample(IMFSample* pSample)
{
  CAutoLock sLock(&m_lockSamples);
  
  if (m_pLastPresSample != NULL)
  {
    ReturnSample(m_pLastPresSample, FALSE);
    m_pLastPresSample = NULL;
  }
  
  if (m_pLastPresSample == NULL)
  {
    m_pLastPresSample = pSample;
  }
}

IMFSample* MPEVRCustomPresenter::PeekLastPresSample()
{
  CAutoLock sLock(&m_lockSamples);
  return m_pLastPresSample;
}


void MPEVRCustomPresenter::ReturnTempSample(IMFSample* pSample)
{
  CAutoLock sLock(&m_lockSamples);
  LOG_TRACE("Clean sample returned: now having %d samples", m_iFreeSamples+1);
  m_vFreeSamples[m_iFreeSamples] = pSample;
  m_iFreeSamples++;
}


HRESULT MPEVRCustomPresenter::PresentSample(IMFSample* pSample, LONGLONG frameTime, bool renderStats)
{
  HRESULT hr = S_OK;
  IMFMediaBuffer* pBuffer = NULL;
  IDirect3DSurface9* pSurface = NULL;
  IMFMediaBuffer* pTempBuffer = NULL;
  IDirect3DSurface9* pTempSurface = NULL;
  IMFSample* pTempSample = NULL;
  LONGLONG then = 0;
  LOG_TRACE("Presenting sample");
  // Get the buffer from the sample.
  CHECK_HR(hr = pSample->GetBufferByIndex(0, &pBuffer), "failed: GetBufferByIndex");

  CHECK_HR(hr = MyGetService(
    pBuffer, 
    MR_BUFFER_SERVICE, 
    __uuidof(IDirect3DSurface9), 
    (void**)&pSurface),
    "failed: MyGetService");

  //Experimental copying from real surface into temp surface for 'repeat render' mode
  if ((m_RepeatRender || (GetQueueCount()==0)) && renderStats)
  {
    if (!FAILED(GetFreeSample(&pTempSample)))
    {
      // Get the buffer from the sample.
      CHECK_HR(hr = pTempSample->GetBufferByIndex(0, &pTempBuffer), "failed: GetBufferByIndex");
    
      CHECK_HR(hr = MyGetService(
        pTempBuffer, 
        MR_BUFFER_SERVICE, 
        __uuidof(IDirect3DSurface9), 
        (void**)&pTempSurface),
        "failed: MyGetService");
    
      if (pTempSurface && pSurface)
      {
        DWORD alphaBlend;
        m_pD3DDev->GetRenderState(D3DRS_ALPHABLENDENABLE, &alphaBlend);
        m_pD3DDev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        CHECK_HR(hr = m_pD3DDev->StretchRect(pSurface, NULL, pTempSurface, NULL, D3DTEXF_NONE),"PresentSample: StretchRect failed")
        m_pD3DDev->SetRenderState(D3DRS_ALPHABLENDENABLE, alphaBlend);
      }      
    }
  }
  
  if (!m_RepeatRender)
  {
    m_iFramesDrawn++;
  }

  if (pTempSurface && !FAILED(hr)) //Special repeat rendering mode when queue is empty
  {
    // Calculate offset to scheduled time for subtitle renderer
    if (m_pClock != NULL)
    {
      LONGLONG hnsTimeNow, hnsSystemTime;
      m_pClock->GetCorrelatedTime(0, &hnsTimeNow, &hnsSystemTime);
      hnsTimeNow = hnsTimeNow + (GetCurrentTimestamp() - hnsSystemTime) + (frameTime * PS_FRAME_ADVANCE); //correct the value
      
      if (hnsTimeNow > 0)
      {
        m_pCallback->SetSampleTime(hnsTimeNow);
        pTempSample->SetSampleTime(hnsTimeNow); 
        pTempSample->SetSampleDuration((frameTime * 9)/8);
      }
    }

    // Present the swap surface
    LOG_TRACE("Painting");
    if (LOG_DELAYS)
      then = GetCurrentTimestamp();
      
    CHECK_HR(hr = Paint(pTempSurface, renderStats), "failed: Paint");
    
    if (LOG_DELAYS)
    {
      LONGLONG diff = GetCurrentTimestamp() - then;
      if (diff > 500000)
      {
        Log("High Paint() latency: %.2f ms", (double)diff/10000);
      }
    }
  }
  else if (pSurface) //Normal rendering
  {
    // Calculate offset to scheduled time for subtitle renderer
    if (m_pClock != NULL)
    {
      LONGLONG hnsTimeNow, hnsSystemTime;
      m_pClock->GetCorrelatedTime(0, &hnsTimeNow, &hnsSystemTime);
      hnsTimeNow = hnsTimeNow + (GetCurrentTimestamp() - hnsSystemTime) + (frameTime * PS_FRAME_ADVANCE); //correct the value
      
      if (hnsTimeNow > 0)
      {
        m_pCallback->SetSampleTime(hnsTimeNow);
        pSample->SetSampleTime(hnsTimeNow); 
        pSample->SetSampleDuration((frameTime * 9)/8);
      }
    }

    // Present the swap surface
    LOG_TRACE("Painting");
    if (LOG_DELAYS)
      then = GetCurrentTimestamp();
      
    // CHECK_HR(hr = Paint(pSurface, (m_bDrawStats && GetQueueCount()) ), "failed: Paint");
    CHECK_HR(hr = Paint(pSurface, renderStats), "failed: Paint");
    
    if (LOG_DELAYS)
    {
      LONGLONG diff = GetCurrentTimestamp() - then;
      if (diff > 500000)
      {
        Log("High Paint() latency: %.2f ms", (double)diff/10000);
      }
    }
  }

  SAFE_RELEASE(pBuffer);
  SAFE_RELEASE(pSurface);
  
  SAFE_RELEASE(pTempBuffer);
  SAFE_RELEASE(pTempSurface);
  
  if (pTempSample)
  {
    ReturnTempSample(pTempSample); 
  }
  
  if (hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET)
  {
    // Failed because the device was lost.
    Log("D3DDevice was lost!");
  }
  
  return hr;
}


BOOL MPEVRCustomPresenter::CheckForInput(bool setInAvail)
{
  int counter;
  ProcessInputNotify(&counter, setInAvail);
  return counter != 0;
}


HRESULT MPEVRCustomPresenter::CheckForScheduledSample(LONGLONG *pTargetTime, LONGLONG lastSleepTime, BOOL *pIdleWait)
{
  HRESULT hr = S_OK;
  LOG_TRACE("Checking for scheduled sample (size: %d)", m_qScheduledSamples.Count());
  LONGLONG displayTime = (LONGLONG)(GetDisplayCycle() * 10000); // display cycle in hns
  LONGLONG hystersisTime = min(50000, displayTime/4) ;
  LONGLONG nextSampleTime = 0;
  LONGLONG realSampleTime = 0;
  LONGLONG systemTime = 0;
  LONGLONG lateLimit = hystersisTime;
  LONGLONG delErrLimit = displayTime;
  bool b_RepeatPaint = false;
  IMFSample* pSample;

  LONGLONG frameTime = m_rtTimePerFrame;
  if (m_DetectedFrameTime > DFT_THRESH)
  {
    frameTime = (LONGLONG)(m_DetectedFrameTime * 10000000.0);
  }

  // Allow 'hystersisTime' late or early frames to avoid synchronised judder problems. 

  if (m_bFlush)
  {
    PauseThread(m_hWorker, &m_workerParams);
    Flush(FALSE); // do not force in case we are in DVD menus 
    WakeThread(m_hWorker, &m_workerParams);
    m_iLateFrames = 0;
    *pTargetTime = 0;
    m_earliestPresentTime = 0;
    *pIdleWait = true;
    return hr;
  }

  //Bail out after presenting first frame in skip-step FFWD/RWD mode
  if (m_bZeroScrub && (m_iFramesProcessed > 0))
    return hr;


  // Unless multiple samples/frames need to be dropped
  // this loop is only traversed once each time
  while (true)
  {        
    b_RepeatPaint = (GetQueueCount() == 0) && ENABLE_EMPTY_RENDER && (PeekLastPresSample() != NULL);
    
    if (
        ((GetQueueCount() == 0) && !b_RepeatPaint) ||  //there are no samples available so we go idle
        (m_state == MP_RENDER_STATE_STOPPED) ||
        (m_state == MP_RENDER_STATE_PAUSED && !m_bDVDMenu)  //don't process samples in paused mode during normal playback
        )
    {
      m_earliestPresentTime = 0;
      m_iLateFrames = 0;
      *pTargetTime = 0;
      *pIdleWait = true;
      if (GetQueueCount() == 0)
      {
        NotifyWorker(FALSE);
      }     
      hr = S_OK;
      break;
    }

    *pIdleWait = false;

    // Check that we are not too early after the last 'present' time
    if (!m_bZeroScrub)
    {   
      systemTime = GetCurrentTimestamp();
      
      if ((m_earliestPresentTime - systemTime) >= MIN_VSC_DELAY )
      {
        *pTargetTime = systemTime + MIN_VSC_DELAY;
        break;
      }
    }


    if (b_RepeatPaint) //Repeat render of last sample
    {
      pSample = PeekLastPresSample();
      if (pSample == NULL)
      {
        *pTargetTime = 0;
        break;
      }
      realSampleTime = 0;       
      nextSampleTime = 0;
      systemTime = GetCurrentTimestamp();
      m_RepeatRender = true;
    }
    else
    {
      pSample = PeekSample();
      if (pSample == NULL)
      {
        *pTargetTime = 0;
        break;
      }
    
      // get scheduled time, if none is available the sample will be presented immediately
      CHECK_HR(hr = GetTimeToSchedule(pSample, &realSampleTime, &systemTime, (frameTime * DWM_DELAY_COMP)), "Couldn't get time to schedule!");
      if (FAILED(hr))
      {
        realSampleTime = 0; 
      }
      nextSampleTime = realSampleTime;
      m_RepeatRender = false;
    }
    
    LOG_TRACE("Time to schedule: %I64d", nextSampleTime);  
        
    if (*pTargetTime > 0)
    {  
      m_lastDelayErr = -(systemTime - *pTargetTime);
    }   
    *pTargetTime = 0;      

    lateLimit = hystersisTime;


    if ((m_frameRateRatio > 0) && !m_bDVDMenu && !m_bScrubbing && m_NSTinitDone)
    {
      //Centralise nextSampleTime timing window when in normal play mode and MP Audio Renderer is inactive
      if (!m_pAVSyncClock && (realSampleTime != 0))
      {
        nextSampleTime = (realSampleTime + (frameTime/2)) - m_hnsNSToffset;
      }

      //De-sensitise frame dropping to avoid occasional delay glitches triggering frame drops
      if ((m_iLateFrames == 0) && !m_NSToffsUpdate)
      {
        if ((nextSampleTime < -hystersisTime) && (nextSampleTime >= -delErrLimit)) //Very late sample
        {
          m_iLateFrames = LF_THRESH_HIGH;
          m_iFramesHeld++;
          //lateLimit = delErrLimit; // Allow this late frame
          Log("Late frame, NST %.2f ms, AveRNST %.2f ms, last sleep %.2f ms, paint %.2f ms, last pres %.2f ms, EPT %.2f ms, late %d, Q %d", 
              (double)nextSampleTime/10000, 
              m_fCFPMean/10000.0, 
              (double)lastSleepTime/10000, 
              (double)m_PaintTime/10000, 
              (double)((m_lastPresentTime - systemTime)/10000), 
              (m_earliestPresentTime ? (double)((m_earliestPresentTime - systemTime)/10000) : 0), 
              m_iFramesHeld,
              GetQueueCount());
              
          m_earliestPresentTime = 0;
          nextSampleTime = 0; //Force this sample to be presented
        }
        else if (((systemTime - m_earliestPresentTime) > (displayTime/2)) && m_earliestPresentTime) //Too long since the last 'present'
        {
          m_iLateFrames = (LF_THRESH_HIGH - 1);
          m_iFramesDelayed++;
          if (LOG_DEL_FRAMES)
          {
            Log("Delayed frame, NST %.2f ms, AveRNST %.2f ms, last sleep %.2f ms, paint %.2f ms, last pres %.2f ms, EPT %.2f ms, late %d, Q %d", 
                (double)nextSampleTime/10000, 
                m_fCFPMean/10000.0, 
                (double)lastSleepTime/10000, 
                (double)m_PaintTime/10000, 
                (double)((m_lastPresentTime - systemTime)/10000), 
                (m_earliestPresentTime ? (double)((m_earliestPresentTime - systemTime)/10000) : 0), 
                m_iFramesDelayed,
                GetQueueCount());
          }
        }
      }
    }
    else
    {
      m_iLateFrames = 0;
    }
        
    // nextSampleTime == 0 means there is no valid presentation time, so we present it immediately without vsync correction
    // When scrubbing always display at least every eighth frame - even if it's late
    if ( (nextSampleTime >= -lateLimit) || m_bDVDMenu || !m_bFrameSkipping || (m_bScrubbing && !(m_iFramesProcessed % 8)) || m_bZeroScrub )
    {   
      // Within the time window to 'present' a sample, or it's a special play mode
      if (!m_bZeroScrub && (m_iLateFrames < LF_THRESH_HIGH))
      {   
        // Apply display vsync correction - check if we are inside the allowed raster target window.
        LONGLONG offsetTime = (m_lastDelayErr < 0) ? -m_lastDelayErr : 0;
        if (!GetDelayToRasterTarget(&offsetTime))
        {
          // Not at the correct point in the display raster, so sleep for a while         
          *pTargetTime = systemTime + MIN_VSC_DELAY;

          //m_earliestPresentTime = 0;
          break;
        }
        
        // We're within the raster timing limits, so present the sample or delay because it's too early...

        // Calculate minimum delay to next possible PresentSample() time
        if ((m_frameRateRatio <= 1 && !m_bScrubbing) || (m_rawFRRatio <= 1 && m_bScrubbing))
        {
          m_earliestPresentTime = systemTime + offsetTime;
        }
        else
        {
          m_earliestPresentTime = systemTime + (displayTime * (m_rawFRRatio - 1)) + offsetTime;
        }    
        
        m_stallTime = m_earliestPresentTime - systemTime;        

        if (nextSampleTime > (frameTime + hystersisTime))
        {                
          if ((m_frameRateRatio > 0) && !m_bDVDMenu && !m_bScrubbing)
          {
            //Count the early/stalled frames
            m_iEarlyFrCnt++;
          }
          // It's too early to present the sample, so delay for a while
          *pTargetTime = systemTime + MIN_VSC_DELAY; //delay in smaller chunks          
          break;
        }    
               
      }
      else
      {
        m_earliestPresentTime = 0;
      }
      
      *pTargetTime = 0;

      if (b_RepeatPaint) //Repeat render of last sample
      {        
        m_lastPresentTime = systemTime;
        CHECK_HR(PresentSample(pSample, frameTime, m_bDrawStats), "PresentSample failed");
      }
      else
      {
        if (PeekSample() != pSample)
        {
          m_earliestPresentTime = 0;
          break;
        }
        
        m_lastPresentTime = systemTime;
        PopSample();        
        CHECK_HR(PresentSample(pSample, frameTime, m_bDrawStats), "PresentSample failed");
        if ((m_iFramesDrawn < NUM_DWM_BUFFERS) && m_bDwmCompEnabled) //Push extra samples into the pipeline at start of play
        {
          CHECK_HR(PresentSample(pSample, frameTime, m_bDrawStats), "PresentSample failed");
          DwmFlush();
        }     
        UpdateLastPresSample(pSample);
      }
      
      NotifyWorker(FALSE);
      m_iFramesProcessed++;
      
      if (m_iLateFrames > 0)
      {
        m_iLateFrames--;
      }
            
      if (m_pAVSyncClock) //Update phase deviation data for MP Audio Renderer
      {
        m_nstPhaseDiffUpd = !m_nstPhaseDiffUpd; //Only update every other frame
        
        //Target (0.5 * frameTime) for nextSampleTime
        double nstPhaseDiff = -(((double)realSampleTime / (double)frameTime) - 0.5);

        //Clamp within limits - because of hystersis, the range of realSampleTime
        //is greater than frameTime, so it's possible for nstPhaseDiff to exceed
        //the -0.5 to +0.5 allowable range 
        if (
             m_bDVDMenu || m_bScrubbing || (m_iFramesDrawn <= FRAME_PROC_THRSH2) 
             || (m_frameRateRatX2 == 0 && m_dBias == 1.0) || !m_nstPhaseDiffUpd
           )
        {
          nstPhaseDiff = 0.0;
        }
        else if (nstPhaseDiff < -0.499)
        {
          nstPhaseDiff = -0.499;
        }
        else if (nstPhaseDiff > 0.499)
        {
          nstPhaseDiff = 0.499;
        }
          
        AdjustAVSync(nstPhaseDiff);
      }
  
      m_llLastCFPts = nextSampleTime;
      CalculateNSTStats(realSampleTime, frameTime); // update NextSampleTime average
      
      // Notify EVR of sample latency
      if( m_pEventSink )
      {
        // LONGLONG sampleLatency = -m_fCFPMean;
        LONGLONG sampleLatency = -realSampleTime;
        m_pEventSink->Notify(EC_SAMPLE_LATENCY, (LONG_PTR)&sampleLatency, 0);
        LOG_TRACE("Sample Latency: %I64d", sampleLatency);
      }
      
      break;
    } 
    else // Drop late frames when frame skipping is enabled during normal playback
    {         
      m_earliestPresentTime = 0;
      
      //UpdateLastPresSample(NULL);
      if (!PopSample())
      {
        break;
      }
      ReturnSample(pSample, FALSE);
      NotifyWorker(FALSE);
      
      // Notify EVR of late sample
      if( m_pEventSink )
      {
        // LONGLONG sampleLatency = -m_fCFPMean;
        LONGLONG sampleLatency = -realSampleTime;
        m_pEventSink->Notify(EC_SAMPLE_LATENCY, (LONG_PTR)&sampleLatency, 0);
        LOG_TRACE("Sample Latency: %I64d", sampleLatency);
      }
      m_iFramesDropped++;
      m_iFramesProcessed++;
                  
      // If video frame rate is higher than display refresh then we'll get lots of dropped frames
      // so it's better to not report them in the log normally.          
      if ((m_frameRateRatio > 0) && !m_bScrubbing && !m_bDVDMenu)
      {
        Log("Dropping frame, NST %.2f ms, AveRNST %.2f ms, last sleep %.2f ms, last pres %.2f ms, paint %.2f ms, queue count %d, SOP %d, EOP %d, RawFRRatio %d, dropped %d, drawn %d, late %d",
             (double)nextSampleTime/10000, 
             m_fCFPMean/10000.0,
             (double)lastSleepTime/10000, 
             (double)((m_lastPresentTime - GetCurrentTimestamp())/10000),
             (double)m_PaintTime/10000, 
             GetQueueCount(),
             m_LastStartOfPaintScanline,
             m_LastEndOfPaintScanline,
             m_rawFRRatio,
             m_iFramesDropped,
             m_iFramesDrawn,
             m_iFramesHeld
             );
      }
      
      if (m_iLateFrames > 0)
      {
        m_iLateFrames--;
      }  
               
      WaitForSingleObject(m_dummyEvent, 1); //Sleep for a short time to be friendly to other threads
    }
    
  } // end of while loop
  
  return hr;
}


void MPEVRCustomPresenter::StartWorkers()
{
  CAutoLock lock(this);
  if (m_bSchedulerRunning)
  {
    return;
  }

  StartThread(&m_hTimer, &m_timerParams, TimerThread, &m_uTimerThreadId, THREAD_PRIORITY_NORMAL);
  StartThread(&m_hWorker, &m_workerParams, WorkerThread, &m_uWorkerThreadId, THREAD_PRIORITY_ABOVE_NORMAL);
  StartThread(&m_hScheduler, &m_schedulerParams, SchedulerThread, &m_uSchedulerThreadId, THREAD_PRIORITY_TIME_CRITICAL);
  m_bSchedulerRunning = TRUE;

}

void MPEVRCustomPresenter::DwmEnableMMCSSOnOff(bool enable)
{
  // Do not use this as it causes: 0002675: Micro stutters after Refresh Rate changes 
  // Either MS bug, or we should be recreating the DirectX device on every refresh rate change
  if (m_pDwmEnableMMCSS)
  {
    HRESULT hr = m_pDwmEnableMMCSS(enable);
    if (enable)
    {
      if (SUCCEEDED(hr)) 
      {
        Log("Enabling MCSS for DWM succeeded");
      }
      else
      {
        Log("Enabling MCSS for DWM failed");
      }
    }
    else
    {   
      if (SUCCEEDED(hr)) 
      {
        Log("Disabling MCSS for DWM succeeded");
      }
      else
      {
        Log("Disabling MCSS for DWM failed");
      }
    }
  }
}

void MPEVRCustomPresenter::DwmFlush()
{
  if (m_pDwmFlush && m_bDwmCompEnabled)
  {
    m_pDwmFlush();
  }
}

void MPEVRCustomPresenter::DwmGetState()
{
  DWORD wProcessId;
  DWORD cProcessId;
  HWND fhWindow = NULL;
  m_hDwmWinHandle = NULL;

  // Find the foreground window handle
  fhWindow = GetForegroundWindow();
  // Get it's process ID
  GetWindowThreadProcessId(fhWindow, &wProcessId);
  cProcessId = GetCurrentProcessId();
  
  // Check that it's the MP window by comparing process ID's    
  if (fhWindow && (wProcessId == cProcessId))
  {
    m_hDwmWinHandle = fhWindow;
  }

  Log("DwmGetState(), hDwmWinHandle = 0x%x, wProcessId = 0x%x, cProcessId = 0x%x", fhWindow, wProcessId, cProcessId);

  if (m_pDwmIsCompositionEnabled)
  { 
    HRESULT hr = m_pDwmIsCompositionEnabled(&m_bDwmCompEnabled);
    if (SUCCEEDED(hr)) 
    {
      m_dwmBuffers = 2;
      Log("DWM composition is enabled");
    }
    else
    {
      m_dwmBuffers = 0;
      Log("DWM composition is disabled");
    }
  }
  else
  {
    m_dwmBuffers = 0;
    m_bDwmCompEnabled = false;
    Log("DWM composition check failed");
  }
}


void MPEVRCustomPresenter::DwmSetParameters(BOOL useSourceRate, UINT buffers, UINT rfshPerFrame)
{  
  HRESULT hr = E_FAIL;

  DWM_FRAME_COUNT cRefresh = 0;
  if (false && m_pDwmGetCompositionTimingInfo && m_bDwmCompEnabled)
  {
    hr = E_FAIL;
    
    DWM_TIMING_INFO presentationStatus;
    presentationStatus.cbSize = sizeof(presentationStatus);
    if (m_hDwmWinHandle)
    {
      hr = m_pDwmGetCompositionTimingInfo(m_hDwmWinHandle, &presentationStatus);
    }

    //if (SUCCEEDED(hr)) 
    if (hr==E_PENDING || hr==S_OK) 
    {
      cRefresh = presentationStatus.cRefresh;
      Log("DwmGetCompositionTimingInfo succeeded, hr = 0x%x, cRefresh = %d", hr, cRefresh);
    }
    else
    {
      Log("DwmGetCompositionTimingInfo failed, hr = 0x%x", hr);
    }
  }
  
  if (m_pDwmSetPresentParameters && m_bDwmCompEnabled)
  {
    hr = E_FAIL;

    //Create and initialise the structure
    DWM_PRESENT_PARAMETERS presentationParams;
    presentationParams.cbSize = sizeof(presentationParams);
    presentationParams.fQueue = TRUE;
    presentationParams.cRefreshStart = 0;
    presentationParams.cBuffer = buffers;
    presentationParams.fUseSourceRate = useSourceRate;
    presentationParams.rateSource.uiNumerator = (UINT)(250000000.0/GetDisplayCycle()); // Actual display rate
    presentationParams.rateSource.uiDenominator = 100000;
    presentationParams.cRefreshesPerFrame = rfshPerFrame;
    presentationParams.eSampling = DWM_SOURCE_FRAME_SAMPLING_POINT;
    
    // Set up the DWM presentation parameters    
    if (m_hDwmWinHandle)
    {
      hr = m_pDwmSetPresentParameters(m_hDwmWinHandle, &presentationParams);
    }

    if (SUCCEEDED(hr)) 
    {
      m_dwmBuffers = buffers;
      Log("DwmSetPresentParameters succeeded, DWM buffers = %d, useSourceRate = %d", m_dwmBuffers, useSourceRate);
    }
    else
    {
      Log("DwmSetPresentParameters failed, hr = 0x%x, DWM buffers = %d", hr, m_dwmBuffers);
    }
    
  }  
  
  if (m_pDwmSetPresentParameters && m_bDwmCompEnabled)
  {
    hr = E_FAIL;
    if (m_hDwmWinHandle)
    {
      hr = m_pDwmSetDxFrameDuration(m_hDwmWinHandle, (INT)rfshPerFrame);
    }
    if (SUCCEEDED(hr)) 
    {
      Log("DwmSetDxFrameDuration succeeded, rfshPerFrame = %d", rfshPerFrame);
    }
    else
    {
      Log("DwmSetDxFrameDuration failed, hr = 0x%x", hr);
    }
  }

}


void MPEVRCustomPresenter::StopWorkers()
{
  Log("Stopping workers...");
  CAutoLock lock(this);
  Log("Threads running : %s", m_bSchedulerRunning?"TRUE":"FALSE");
  if (!m_bSchedulerRunning)
  {
    return;
  }
  EndThread(m_hScheduler, &m_schedulerParams);
  EndThread(m_hWorker, &m_workerParams);
  EndThread(m_hTimer, &m_timerParams);
  m_bSchedulerRunning = FALSE;
}


void MPEVRCustomPresenter::StartThread(PHANDLE handle, SchedulerParams* pParams, UINT(CALLBACK *ThreadProc)(void*), UINT* threadId, int priority)
{
  Log("Starting thread!");
  pParams->pPresenter = this;
  pParams->bDone = FALSE;
  pParams->iPause = 0;
  pParams->bPauseAck = FALSE;
  pParams->llTime = 0;

  *handle = (HANDLE)_beginthreadex(NULL, 0, ThreadProc, pParams, 0, threadId);
  Log("Started thread. id: 0x%x (%d), handle: 0x%x", *threadId, *threadId, *handle);
  SetThreadPriority(*handle, priority);
}


void MPEVRCustomPresenter::EndThread(HANDLE hThread, SchedulerParams* params)
{
  Log("Ending thread 0x%x, 0x%x", hThread, params);
  params->csLock.Lock();
  Log("Got lock.");
  params->iPause = 0;
  params->bDone = TRUE;
  Log("Notifying thread...");
  params->eHasWork.Set();
  Log("Set done.");
  params->csLock.Unlock();
  Log("Waiting for thread to end...");
  WaitForSingleObject(hThread, INFINITE);
  Log("Waiting done");
  CloseHandle(hThread);
}

void MPEVRCustomPresenter::PauseThread(HANDLE hThread, SchedulerParams* params)
{
  if (!m_bSchedulerRunning)
  {
    return;
  }
  LOG_THR_PAUSE("Pausing thread 0x%x, 0x%x, %d", hThread, params, params->iPause);

  InterlockedIncrement(&params->iPause);
  
  int i = 0;
  for (i = 0; i < 1000; i++)
  {
    params->eHasWork.Set(); //Wake thread (in case it's sleeping)
    Sleep(1); //Sleep for 1 ms to be friendly to other threads
    if (params->bPauseAck)
    {
      break;
    }
  }
  
  if (i >= 1000)
  {
    Log("Thread pause timeout 0x%x, 0x%x, %d", hThread, params, params->iPause);
  }
  else
  {
    LOG_THR_PAUSE("Thread paused, 0x%x, 0x%x, %d", hThread, params, params->iPause);
  }

}

void MPEVRCustomPresenter::WakeThread(HANDLE hThread, SchedulerParams* params)
{
  if (!m_bSchedulerRunning)
  {
    return;
  }

  InterlockedDecrement(&params->iPause);

  params->eHasWork.Set();

  LOG_THR_PAUSE("Waking thread 0x%x, 0x%x, %d", hThread, params, params->iPause);  
}


void MPEVRCustomPresenter::NotifyThread(SchedulerParams* params, bool setWork, bool setWorkLP, LONGLONG llTime)
{
  if (m_bSchedulerRunning)
  {
    if (setWork)
    {
      params->llTime = llTime;
      params->eHasWork.Set();
    }
    if (setWorkLP)
    {
      params->llTime = llTime;
      params->eHasWorkLP.Set();
    }
  }
  else 
  {
    Log("Scheduler is already shut down");
  }
}


void MPEVRCustomPresenter::NotifyScheduler(bool forceWake)
{
  LOG_TRACE("NotifyScheduler()");
  if (forceWake)
  {
    NotifyThread(&m_schedulerParams, true, false, 0);
  }
  else
  {
    NotifyThread(&m_schedulerParams, false, true, 0);
  }
}



void MPEVRCustomPresenter::NotifySchedulerTimer()
{
  if (m_bSchedulerRunning)
  {
    m_schedulerParams.eTimerEnd.Set();
  }
  else 
  {
    Log("Scheduler is already shut down");
  }
}



void MPEVRCustomPresenter::NotifyWorker(bool setInAvail)
{
  LOG_TRACE("NotifyWorker()");
  m_llLastWorkerNotification = GetCurrentTimestamp();
  if (setInAvail)
  {
    NotifyThread(&m_workerParams, true, false, 0);
  }
  else
  {
    NotifyThread(&m_workerParams, false, true, 0);
  }
}

void MPEVRCustomPresenter::NotifyTimer(LONGLONG targetTime)
{
  LOG_TRACE("NotifyTimer()");
  if (targetTime > 0)
  {
    NotifyThread(&m_timerParams, false, true, targetTime);
  }
  else
  {
    NotifyThread(&m_timerParams, false, false, targetTime);
  }   
}


BOOL MPEVRCustomPresenter::PopSample()
{
  CAutoLock sLock(&m_lockSamples);
  LOG_TRACE("Removing scheduled sample, size: %d", m_qScheduledSamples.Count());
  if (!m_qScheduledSamples.IsEmpty())
  {
    m_qScheduledSamples.Get();
    m_qGoodPopCnt++;
    return TRUE;
  }
  m_qBadPopCnt++;
  return FALSE;
}

int MPEVRCustomPresenter::GetQueueCount()
{
  CAutoLock sLock(&m_lockSamples);
  return m_qScheduledSamples.Count();
}


IMFSample* MPEVRCustomPresenter::PeekSample()
{
  CAutoLock sLock(&m_lockSamples);
  if (m_qScheduledSamples.IsEmpty())
  {
    Log("ERR: PeekSample: empty queue!");
    return NULL;
  }
  return m_qScheduledSamples.Peek();
}


BOOL MPEVRCustomPresenter::ScheduleSample(IMFSample* pSample)
{
  LOG_TRACE("Scheduling Sample, size: %d", m_qScheduledSamples.Count());

  BOOL onTimeSample = true;
  
  DWORD hr;
  LONGLONG nextSampleTime;
  LONGLONG systemTime;
  CHECK_HR(hr = GetTimeToSchedule(pSample, &nextSampleTime, &systemTime, 0), "Couldn't get time to schedule!");
  if (SUCCEEDED(hr))
  {
    if ((nextSampleTime < -50000) && !m_bDVDMenu && !m_bScrubbing)
    {
      // consider 5 ms "just-in-time" for log-length's sake
      onTimeSample = false; //Allow sample to be dropped
      Log("Dropping sample from the past (%.2f ms, last call to NotifyWorker: %.2f ms, Queue: %d, Dropped: %d)", 
        (double)-nextSampleTime/10000, (GetCurrentTimestamp()-(double)m_llLastWorkerNotification)/10000, m_qScheduledSamples.Count(), m_iFramesDropped);
    }
  }

  if (onTimeSample) //Use samples if they are on-time
  {
    onTimeSample = PutSample(pSample);
  }
  
  return onTimeSample;
}

BOOL MPEVRCustomPresenter::PutSample(IMFSample* pSample)
{
  m_lockSamples.Lock();
  LOG_TRACE("Adding scheduled sample, q size: %d", m_qScheduledSamples.Count());

  if (m_qScheduledSamples.Put(pSample))
  {
    if (m_qScheduledSamples.Count() <= 1 || m_bEmptyQueue)
    {
      m_lockSamples.Unlock();
      NotifyScheduler(false);
    }
    else
    {
      m_lockSamples.Unlock();
    }
    return TRUE;
  }
  
  m_lockSamples.Unlock();
  return FALSE;
}

BOOL MPEVRCustomPresenter::CheckForEndOfStream()
{
  if (!m_bEndStreaming)
  {
    return FALSE;
  }
  // samples pending
  if (GetQueueCount() > 0)
  {
    return FALSE;
  }
  if (m_pEventSink)
  {
    Log("Sending completion message");
    m_pEventSink->Notify(EC_COMPLETE, (LONG_PTR)S_OK, 0);
  }
  m_bEndStreaming = FALSE;
  return TRUE;
}


HRESULT MPEVRCustomPresenter::ProcessInputNotify(int* samplesProcessed, bool setInAvail)
{
  LOG_TRACE("ProcessInputNotify");
  HRESULT hr = S_OK;
  *samplesProcessed = 0;
  
  if (!m_bFirstInputNotify || (m_state == MP_RENDER_STATE_STOPPED))
  {
    m_bInputAvailable = FALSE;
    return S_OK;
  }
  
  if (setInAvail) 
  {
    m_bInputAvailable = true;
  }
    
  if (m_pClock != NULL)
  {
    MFCLOCK_STATE state;
    m_pClock->GetState(0, &state);
    if (state == MFCLOCK_STATE_PAUSED)
    {
      // Log("Should not be processing data in pause mode");
      m_bInputAvailable = FALSE;
      return S_OK;
    }
  }
  else 
  {
    return S_OK;
  }

  // try to process as many samples as possible:
  BOOL bhasMoreSamples = true;
  do {
    IMFSample* pSample;
    hr = GetFreeSample(&pSample);
    if (FAILED(hr))
    {
      return S_OK;
      //      // double-checked locking, in case someone freed a sample between the above 2 steps and we would miss notification
      //      hr = GetFreeSample(&pSample);
      //      if (FAILED(hr))
      //      {
      //        LOG_TRACE("Still more input available");
      //        return S_OK;
      //      }
    }


    LONGLONG timeBeforeMixer;
    LONGLONG systemTime;
    m_pClock->GetCorrelatedTime(0, &timeBeforeMixer, &systemTime);

    if (m_pMixer == NULL)
    {
      m_bInputAvailable = FALSE;
      return E_POINTER;
    }
    DWORD dwStatus;
    MFT_OUTPUT_DATA_BUFFER outputSamples[1];
    outputSamples[0].dwStreamID = 0;
    outputSamples[0].dwStatus = 0;
    outputSamples[0].pSample = pSample;
    outputSamples[0].pEvents = NULL;
    hr = m_pMixer->ProcessOutput(0, 1, outputSamples, &dwStatus);
    SAFE_RELEASE(outputSamples[0].pEvents);
    if (SUCCEEDED(hr))
    {
      LONGLONG sampleTime;
      LONGLONG timeAfterMixer;
      pSample->GetSampleTime(&sampleTime);

      *samplesProcessed++;

      m_pClock->GetCorrelatedTime(0, &timeAfterMixer, &systemTime);
      CalculatePresClockDelta(timeAfterMixer, systemTime);

      LONGLONG mixerLatency = timeAfterMixer - timeBeforeMixer;
      if (m_pEventSink)
      {
        m_pEventSink->Notify(EC_PROCESSING_LATENCY, (LONG_PTR)&mixerLatency, 0);
        LOG_TRACE("Mixer Latency: %I64d", mixerLatency);
      }
      if (ScheduleSample(pSample))
      {
        CorrectSampleTime(pSample);
        m_qGoodPutCnt++;
      }
      else //sample has been dropped
      {
        ReturnSample(pSample, FALSE);
      }
    }
    else 
    {
      ReturnSample(pSample, FALSE);
      m_qBadPutCnt++;
      switch (hr)
      {
      case MF_E_TRANSFORM_NEED_MORE_INPUT:
        // we are done for now
        hr = S_OK;
        bhasMoreSamples = false;
        LOG_TRACE("Need more input...");
        CheckForEndOfStream();
      break;

      case MF_E_TRANSFORM_STREAM_CHANGE:
        Log("Unhandled: transform_stream_change");
      break;

      case MF_E_TRANSFORM_TYPE_NOT_SET:
        // no errors, just infos why it didn't succeed
        Log("ProcessOutput: change of type");
        bhasMoreSamples = FALSE;
        m_bFirstInputNotify = FALSE;
        PauseThread(m_hTimer, &m_timerParams);
        PauseThread(m_hScheduler, &m_schedulerParams);
        //LogOutputTypes();
        Flush(FALSE);
        hr = RenegotiateMediaOutputType();
        WakeThread(m_hScheduler, &m_schedulerParams);
        WakeThread(m_hTimer, &m_timerParams);
      break;

      default:
        Log("ProcessOutput failed: 0x%x", hr);
        break;
      }
      return hr;
    }
    
    WaitForSingleObject(m_dummyEvent, 1); //Sleep for a short time to be friendly to other threads
    
  } while (bhasMoreSamples);
  
  m_bInputAvailable = FALSE;
  return hr;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::ProcessMessage(MFVP_MESSAGE_TYPE eMessage, ULONG_PTR ulParam)
{
  HRESULT hr = S_OK;
  LOG_TRACE("Processmessage: %d, %p", eMessage, ulParam);

  switch (eMessage)
  {
    case MFVP_MESSAGE_FLUSH:
      // The presenter should discard any pending samples.
      m_bFirstInputNotify = FALSE;
      Log("ProcessMessage MFVP_MESSAGE_FLUSH");
      // Delegate to avoid a weird deadlock with application-idle handler Flush();
      m_bFlush = TRUE;
      NotifyScheduler(true);
    break;

    case MFVP_MESSAGE_INVALIDATEMEDIATYPE:
      // The mixer's output format has changed. The EVR will initiate format negotiation.
      m_bFirstInputNotify = FALSE;
      Log("ProcessMessage MFVP_MESSAGE_INVALIDATEMEDIATYPE");
      PauseThread(m_hTimer, &m_timerParams);
      PauseThread(m_hWorker, &m_workerParams);
      PauseThread(m_hScheduler, &m_schedulerParams);
      //LogOutputTypes();
      Flush(FALSE);
      hr = RenegotiateMediaOutputType();
      WakeThread(m_hScheduler, &m_schedulerParams);
      WakeThread(m_hWorker, &m_workerParams);
      WakeThread(m_hTimer, &m_timerParams);
    break;

    case MFVP_MESSAGE_PROCESSINPUTNOTIFY:
      // One input stream on the mixer has received a new sample.
      LOG_TRACE("ProcessMessage MFVP_MESSAGE_PROCESSINPUTNOTIFY");
      // ImmediateCheckForInput();
      m_bFirstInputNotify = TRUE;
      NotifyWorker(true);
    break;

    case MFVP_MESSAGE_BEGINSTREAMING:
      // The EVR switched from stopped to paused. The presenter should allocate resources.
      Log("ProcessMessage MFVP_MESSAGE_BEGINSTREAMING");
      PauseThread(m_hTimer, &m_timerParams);
      PauseThread(m_hWorker, &m_workerParams);
      PauseThread(m_hScheduler, &m_schedulerParams);
      
      ResetTraceStats();
      ResetFrameStats();
      if (!m_bSchedulerRunning)
      {
        GetFilterNames();
      }
      //Setup the Desktop Window Manager (DWM)
      DwmInit(NUM_DWM_BUFFERS, NUM_DWM_FRAMES);
      m_bEndStreaming = FALSE;
      m_bInputAvailable = FALSE;
      m_bFirstInputNotify = FALSE;
      m_state = MP_RENDER_STATE_STARTED; 
        
      WakeThread(m_hScheduler, &m_schedulerParams);
      WakeThread(m_hWorker, &m_workerParams);
      WakeThread(m_hTimer, &m_timerParams);
      StartWorkers();      
      // TODO add 2nd monitor support
      
    break;

    case MFVP_MESSAGE_ENDSTREAMING:
      // The EVR switched from running or paused to stopped. The presenter should free resources.
      m_bFirstInputNotify = FALSE;
      Log("ProcessMessage MFVP_MESSAGE_ENDSTREAMING");
      PauseThread(m_hTimer, &m_timerParams);
      PauseThread(m_hWorker, &m_workerParams);
      PauseThread(m_hScheduler, &m_schedulerParams);
      m_state = MP_RENDER_STATE_STOPPED;
      WakeThread(m_hScheduler, &m_schedulerParams);
      WakeThread(m_hWorker, &m_workerParams);
      WakeThread(m_hTimer, &m_timerParams);
    break;

    case MFVP_MESSAGE_ENDOFSTREAM:
      // All streams have ended. The ulParam parameter is not used and should be zero.
      Log("ProcessMessage MFVP_MESSAGE_ENDOFSTREAM");
      m_bEndStreaming = TRUE;
      CheckForEndOfStream();
    break;

    case MFVP_MESSAGE_STEP:
      // Requests a frame step. The lower DWORD of the ulParam parameter contains the number of frames to step. 
      // If the value is N, the presenter should skip N �1 frames and display the N th frame. When that frame 
      // has been displayed, the presenter should send an EC_STEP_COMPLETE event to the EVR. If the presenter 
      // is not paused when it receives this message, it should return MF_E_INVALIDREQUEST.
      Log("ProcessMessage MFVP_MESSAGE_STEP");
    break;

    case MFVP_MESSAGE_CANCELSTEP:
      // Cancels a frame step.
      Log("ProcessMessage MFVP_MESSAGE_CANCELSTEP");
    break;

    default:
      Log("ProcessMessage Unknown: %d", eMessage);
    break;
  }

  if (FAILED(hr))
  {
    Log("ProcessMessage failed with 0x%x", hr);
  }

  LOG_TRACE("ProcessMessage done");
  return hr;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::OnClockStart(MFTIME hnsSystemTime, LONGLONG llClockStartOffset)
{
  Log("OnClockStart");
  PauseThread(m_hTimer, &m_timerParams);
  PauseThread(m_hWorker, &m_workerParams);
  PauseThread(m_hScheduler, &m_schedulerParams);
  m_state = MP_RENDER_STATE_STARTED;
  ResetTraceStats();
  ResetFrameStats();
  Flush(FALSE);
  WakeThread(m_hScheduler, &m_schedulerParams);
  WakeThread(m_hWorker, &m_workerParams);
  WakeThread(m_hTimer, &m_timerParams);
  
  NotifyWorker(true);
  NotifyScheduler(true);

  GetAVSyncClockInterface();

  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::OnClockStop(MFTIME hnsSystemTime)
{
  m_bFirstInputNotify = FALSE;
  Log("OnClockStop");
  PauseThread(m_hTimer, &m_timerParams);
  PauseThread(m_hWorker, &m_workerParams);
  PauseThread(m_hScheduler, &m_schedulerParams);
  m_state = MP_RENDER_STATE_STOPPED;
  Flush(FALSE);
  WakeThread(m_hScheduler, &m_schedulerParams);
  WakeThread(m_hWorker, &m_workerParams);
  WakeThread(m_hTimer, &m_timerParams);
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::OnClockPause(MFTIME hnsSystemTime)
{
  Log("OnClockPause");
  PauseThread(m_hTimer, &m_timerParams);
  PauseThread(m_hWorker, &m_workerParams);
  PauseThread(m_hScheduler, &m_schedulerParams);
  m_state = MP_RENDER_STATE_PAUSED;
  WakeThread(m_hScheduler, &m_schedulerParams);
  WakeThread(m_hWorker, &m_workerParams);
  WakeThread(m_hTimer, &m_timerParams);
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::OnClockRestart(MFTIME hnsSystemTime)
{
  Log("OnClockRestart");
  PauseThread(m_hTimer, &m_timerParams);
  PauseThread(m_hWorker, &m_workerParams);
  PauseThread(m_hScheduler, &m_schedulerParams);
  m_state = MP_RENDER_STATE_STARTED;
  ResetFrameStats();
  WakeThread(m_hScheduler, &m_schedulerParams);
  WakeThread(m_hWorker, &m_workerParams);
  WakeThread(m_hTimer, &m_timerParams);
  
  NotifyWorker(true);
  NotifyScheduler(true);
  
  GetAVSyncClockInterface();
  SetupAudioRenderer();

  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::OnClockSetRate(MFTIME hnsSystemTime, float flRate)
{
  Log("OnClockSetRate: %f", flRate);
  m_fRate = flRate;
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::GetService(REFGUID guidService, REFIID riid, LPVOID *ppvObject)
{
  Log("GetService");
  LogGUID(guidService);
  LogIID(riid);
  HRESULT hr = MF_E_UNSUPPORTED_SERVICE;
  if (ppvObject == NULL)
  {
    hr = E_POINTER;
  }
  else if (riid == __uuidof(IDirect3DDeviceManager9))
  {
    hr = m_pDeviceManager->QueryInterface(riid, (void**)ppvObject);
  }
  else if (riid == IID_IMFVideoDeviceID)
  {
    *ppvObject = static_cast<IMFVideoDeviceID*>(this);
    AddRef();
    hr = S_OK;
  }
  else if (riid == IID_IMFClockStateSink)
  {
    *ppvObject = static_cast<IMFClockStateSink*>(this);
    AddRef();
    hr = S_OK;
  }
  else if (riid == IID_IMFTopologyServiceLookupClient)
  {
    *ppvObject = static_cast<IMFTopologyServiceLookupClient*>(this);
    AddRef();
    hr = S_OK;
  }
  else if (riid == IID_IMFVideoPresenter)
  {
    *ppvObject = static_cast<IMFVideoPresenter*>(this);
    AddRef();
    hr = S_OK;
  }
  else if (riid == IID_IMFGetService)
  {
    *ppvObject = static_cast<IMFGetService*>(this);
    AddRef();
    hr = S_OK;
  }
  else if (riid == IID_IMFRateSupport)
  {
    *ppvObject = static_cast<IMFRateSupport*>(this);
    AddRef();
    hr = S_OK;
  }
  else if (riid == IID_IMFVideoDisplayControl)
  {
    *ppvObject = static_cast<IMFVideoDisplayControl*>(this);
    AddRef();
    hr = S_OK;
  }
  else if (riid == IID_IEVRTrustedVideoPlugin)
  {
    *ppvObject = static_cast<IEVRTrustedVideoPlugin*>(this);
    AddRef();
    hr = S_OK;
  }
  else if (riid == IID_IMFVideoPositionMapper)
  {
    *ppvObject = static_cast<IMFVideoPositionMapper*>(this);
    AddRef();
    hr = S_OK;
  }
  else
  {
    LogGUID(guidService);
    LogIID(riid);
    *ppvObject=NULL;
    hr = E_NOINTERFACE;
  }
  if (FAILED(hr) || (*ppvObject)==NULL)
  {
    Log("GetService failed");
  }
  return hr;
}


void MPEVRCustomPresenter::ReleaseSurfaces()
{
  Log("ReleaseSurfaces()");
  CAutoLock lock(this);
  HANDLE hDevice;
  CHECK_HR(m_pDeviceManager->OpenDeviceHandle(&hDevice), "failed opendevicehandle");
  IDirect3DDevice9* pDevice;
  CHECK_HR(m_pDeviceManager->LockDevice(hDevice, &pDevice, TRUE), "failed: lockdevice");
  // make sure that the surface is not in use anymore before we delete it.
  if (m_pCallback != NULL)
  {
    m_pCallback->PresentImage(0, 0, 0, 0, 0, 0);
  }
  Flush(TRUE);
  m_iFreeSamples = 0;
  m_pLastPresSample = NULL;
  for (int i = 0; i < NUM_SURFACES; i++)
  {
    samples[i] = NULL;
    surfaces[i] = NULL;
    textures[i] = NULL;
    m_vFreeSamples[i] = NULL;
  }

  m_pDeviceManager->UnlockDevice(hDevice, FALSE);
  Log("Releasing device");
  pDevice->Release();
  m_pDeviceManager->CloseDeviceHandle(hDevice);
  Log("ReleaseSurfaces() done");
}


HRESULT MPEVRCustomPresenter::Paint(CComPtr<IDirect3DSurface9> pSurface, bool renderStats)
{
  CAutoLock cLock(&m_lockCallback);

  // Old current surface is saved in case the device is lost
  // and we need to restore it 
  IDirect3DSurface9* pOldSurface = NULL;

  try
  {
    if (m_pCallback == NULL || pSurface == NULL)
    {
      return E_FAIL;
    }

    // Presenter is flushing samples, do not render unless in DVD menus
    if (m_bFlush)
    {
      if (!m_bDVDMenu)
      {
        return S_OK;
      }
      else
      {
        m_bFlush = FALSE;
      }
    }

    HRESULT hr;

    if (FAILED(hr = m_pD3DDev->GetRenderTarget(0, &pOldSurface)))
    {
      Log("EVR:Paint: Failed to get current render target: %u\n", hr);
    }

    D3DRASTER_STATUS rasterStatus;

    m_pD3DDev->GetRasterStatus(0, &rasterStatus);
    
    LONGLONG startPaint = GetCurrentTimestamp();
    m_LastStartOfPaintScanline = rasterStatus.ScanLine;

    double currentDispCycle = GetDisplayCycle();
    m_rasterSyncOffset = ((m_maxScanLine + 1) - m_LastStartOfPaintScanline) * m_dDetectedScanlineTime; // in milliseconds    
    if ( (m_rasterSyncOffset > (currentDispCycle * 1.1) ) || (m_LastStartOfPaintScanline > m_maxScanLine) )
    {
      // Correct invalid values, scanline can be bigger than screen resolution  
      m_rasterSyncOffset = m_dDetectedScanlineTime * m_maxScanLine;
    }
    
    if (renderStats)
    {
      m_pD3DDev->SetRenderTarget(0, pSurface);
      m_pStatsRenderer->DrawStats();
      m_pStatsRenderer->DrawTearingTest(pSurface);
    }

    m_pD3DDev->SetRenderTarget(0, pOldSurface);

    CComPtr<IDirect3DTexture9> pTexture = NULL;
    pSurface->GetContainer(IID_IDirect3DTexture9, (void**)&pTexture);

    hr = m_pCallback->PresentImage(m_iVideoWidth, m_iVideoHeight, m_iARX,m_iARY, (DWORD)(IDirect3DTexture9*)pTexture, (DWORD)(IDirect3DSurface9*)pSurface);

    m_PaintTime = GetCurrentTimestamp() - startPaint;
      
    m_pD3DDev->GetRasterStatus(0, &rasterStatus);
    m_LastEndOfPaintScanline = rasterStatus.ScanLine;
        
    if (m_bDrawStats) // no point in wasting CPU time if we aren't displaying the stats
    {
      //update the video and display timing values
      CalculateRealFramePeriod(startPaint); // update real frame rate average
  
      m_PaintTimeMin = min(m_PaintTimeMin, m_PaintTime);
      m_PaintTimeMax = max(m_PaintTimeMax, m_PaintTime);
  
      OnVBlankFinished(true, startPaint, GetCurrentTimestamp());
  
      CalculateJitter(startPaint);
    }

    if (m_bResetStats)
    {
      ResetTraceStats();
    }

    return hr;
  }
  catch(...)
  {
    if (pOldSurface)
    {
      Log("Paint() exception - restoring old render target");
      m_pD3DDev->SetRenderTarget(0, pOldSurface);
    }
    else
    {
      Log("Paint() exception");
    }
  }
  return E_FAIL;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::get_FramesDroppedInRenderer(int *pcFrames)
{
  if (pcFrames == NULL)
  {
    return E_POINTER;
  }
  *pcFrames = m_iFramesDropped;
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::get_FramesDrawn(int *pcFramesDrawn)
{
  if (pcFramesDrawn == NULL)
  {
    return E_POINTER;
  }
  *pcFramesDrawn = m_iFramesDrawn;
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::get_AvgFrameRate(int *piAvgFrameRate)
{
  *piAvgFrameRate = (int)(m_fAvrFps*100);
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::get_Jitter(int *iJitter)
{
  *iJitter = (int)((m_fJitterStdDev/10000.0) + 0.5);
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::get_AvgSyncOffset(int *piAvg)
{
  *piAvg = (int)((m_fSyncOffsetAvr/10000.0) + 0.5);
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::get_DevSyncOffset(int *piDev)
{
  *piDev = (int)((m_fSyncOffsetStdDev/10000.0) + 0.5);
  return S_OK;
}


STDMETHODIMP MPEVRCustomPresenter::GetNativeVideoSize(SIZE *pszVideo, SIZE *pszARVideo)
{
  Log("IMFVideoDisplayControl.GetNativeVideoSize()");
  pszVideo->cx   = m_iVideoWidth;
  pszVideo->cy   = m_iVideoHeight;
  pszARVideo->cx = m_iARX;
  pszARVideo->cy = m_iARY;

  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::GetIdealVideoSize(SIZE *pszMin, SIZE *pszMax)
{
  Log("IMFVideoDisplayControl.GetIdealVideoSize()");
  pszMin->cx = m_iVideoWidth;
  pszMin->cy = m_iVideoHeight;
  pszMax->cx = m_iVideoWidth;
  pszMax->cy = m_iVideoHeight;
  return E_NOTIMPL;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::SetVideoPosition(const MFVideoNormalizedRect *pnrcSource, const LPRECT prcDest)
{
  Log("IMFVideoDisplayControl.SetVideoPosition()");
  return E_NOTIMPL;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::GetVideoPosition(MFVideoNormalizedRect *pnrcSource, LPRECT prcDest)
{
  pnrcSource->left = 0;
  pnrcSource->top = 0;
  pnrcSource->right = (float)m_iVideoWidth;
  pnrcSource->bottom = (float)m_iVideoHeight;

  prcDest->left = 0;
  prcDest->top = 0;
  prcDest->right = m_iVideoWidth;
  prcDest->bottom = m_iVideoHeight;
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::SetAspectRatioMode(DWORD dwAspectRatioMode)
{
  Log("IMFVideoDisplayControl.SetAspectRatioMode()");
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::GetAspectRatioMode(DWORD *pdwAspectRatioMode)
{
  Log("IMFVideoDisplayControl.GetAspectRatioMode()");
  *pdwAspectRatioMode = VMR_ARMODE_NONE;
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::SetVideoWindow(HWND hwndVideo)
{
  Log("IMFVideoDisplayControl.SetVideoWindow()");
  return E_NOTIMPL;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::GetVideoWindow(HWND *phwndVideo)
{
  Log("IMFVideoDisplayControl.GetVideoWindow()");
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::RepaintVideo(void)
{
  Log("IMFVideoDisplayControl.RepaintVideo()");
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::GetCurrentImage(BITMAPINFOHEADER *pBih, BYTE **pDib, DWORD *pcbDib, LONGLONG *pTimeStamp)
{
  Log("IMFVideoDisplayControl.GetCurrentImage()");
  return E_NOTIMPL;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::SetBorderColor(COLORREF Clr)
{
  Log("IMFVideoDisplayControl.SetBorderColor()");
  return E_NOTIMPL;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::GetBorderColor(COLORREF *pClr)
{
  Log("IMFVideoDisplayControl.GetBorderColor()");
  if (pClr)
  {
    *pClr = 0;
  }
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::SetRenderingPrefs(DWORD dwRenderFlags)
{
  Log("IMFVideoDisplayControl.SetRenderingPrefs()");
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::GetRenderingPrefs(DWORD *pdwRenderFlags)
{
  Log("IMFVideoDisplayControl.GetRenderingPrefs()");
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::SetFullscreen(BOOL fFullscreen)
{
  Log("IMFVideoDisplayControl.SetFullscreen()");
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::GetFullscreen(BOOL *pfFullscreen)
{
  Log("GetFullscreen()");
  *pfFullscreen=NULL;
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::IsInTrustedVideoMode (BOOL *pYes)
{
  Log("IEVRTrustedVideoPlugin.IsInTrustedVideoMode()");
  *pYes=TRUE;
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::CanConstrict (BOOL *pYes)
{
  *pYes=TRUE;
  Log("IEVRTrustedVideoPlugin.CanConstrict()");
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::SetConstriction(DWORD dwKPix)
{
  Log("IEVRTrustedVideoPlugin.SetConstriction(%d)",dwKPix);
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::DisableImageExport(BOOL bDisable)
{
  Log("IEVRTrustedVideoPlugin.DisableImageExport(%d)",bDisable);
  return S_OK;
}


HRESULT STDMETHODCALLTYPE MPEVRCustomPresenter::MapOutputCoordinateToInputStream(float xOut,float yOut,DWORD dwOutputStreamIndex,DWORD dwInputStreamIndex,float* pxIn,float* pyIn)
{
  *pxIn = xOut;
  *pyIn = yOut;
  return S_OK;
}

double LinearRegression(double *x, double *y, int n, double *pSlope, double *pIntercept)
{
  int i;
  double sigmaXY = 0;
  double sigmaX2 = 0;
  double sigmaX = 0;
  double sigmaY = 0;
  double sigmaY2 = 0;

  for(i = 0; i < n; i++)
  {
    sigmaXY += (*x) * (*y);
    sigmaX2 += (*x) * (*x);
    sigmaY2 += (*y) * (*y);
    sigmaX += *x++;
    sigmaY += *y++;
  }

  *pSlope = (n * sigmaXY - sigmaX * sigmaY) / (n * sigmaX2 - sigmaX * sigmaX);
  *pIntercept = (sigmaY - *pSlope * sigmaX) / n;
  return (n * sigmaXY - sigmaX * sigmaY) / sqrt((n * sigmaX2 - sigmaX * sigmaX) * (n * sigmaY2 - sigmaY * sigmaY));
}

int MPEVRCustomPresenter::MeasureScanLines(LONGLONG startTime, double *times, double *scanLines, int n, UINT *maxScanLine)
{
  D3DRASTER_STATUS rasterStatus;
  int line = -1;
  for (int i = 0; i < n; i++)
  {
    do
    {
      times[i] = (double)(GetCurrentTimestamp() - startTime);
      m_pD3DDev->GetRasterStatus(0, &rasterStatus);
      scanLines[i] = (double)rasterStatus.ScanLine;
    } while (line == rasterStatus.ScanLine);

    if (line > (int)*maxScanLine) 
      *maxScanLine = (UINT)line;

    if ((int)rasterStatus.ScanLine < line)
      return i;
      
    line = rasterStatus.ScanLine;
  }

  //Looping wait until next vsync
  Log("MeasureScanLines: wait for vsync: scanline: %d", line);
  while ((int)rasterStatus.ScanLine >= line) 
  {
    line = rasterStatus.ScanLine;
    
    if (line > (int)*maxScanLine) 
      *maxScanLine = (UINT)line;
      
    m_pD3DDev->GetRasterStatus(0, &rasterStatus);
  }
    
  return n;
}

BOOL MPEVRCustomPresenter::EstimateRefreshTimings(int numFrames, int thrdPrio)
{

  struct {
    UINT     maxScanLine;
    UINT     minVisScanLine;
    UINT     maxVisScanLine;    
    double   dDetectedScanlineTime;
    double   dEstRefreshCycle; 
    bool     estRefreshLock;
  }   dParams;

  dParams.maxScanLine           = m_maxScanLine;
  dParams.minVisScanLine        = m_minVisScanLine;
  dParams.maxVisScanLine        = m_maxVisScanLine;
  dParams.dDetectedScanlineTime = m_dDetectedScanlineTime;
  dParams.dEstRefreshCycle      = m_dEstRefreshCycle;  
  dParams.estRefreshLock        = false;
  
  if (m_pD3DDev)
  {
    Log("Starting to estimate display refresh timings");

    m_pD3DDev->GetDisplayMode(0, &m_displayMode); //update this just in case anything has changed...
    
    D3DRASTER_STATUS rasterStatus;

    LONGLONG startTime = 0;
    LONGLONG startTimeLR = 0;
    LONGLONG endTime = 0;
    UINT line = 0;
    UINT startLine = 0;
    UINT endLine = 0;
    double AllowedError = 0.0;
    double currError = 0.0;

    dParams.maxScanLine    = 0;
    dParams.minVisScanLine = m_displayMode.Height;
    dParams.maxVisScanLine = 0;   
    
    // Estimate the display refresh rate from the vsyncs
    
    int priority = GetThreadPriority(GetCurrentThread());
    if (priority != THREAD_PRIORITY_ERROR_RETURN)
    {
      SetThreadPriority(GetCurrentThread(), thrdPrio);
    }

     
    const int maxScanLineSamples = 1000;
    const int maxFrameSamples = 8;
    double times[maxScanLineSamples*2];
    double scanLines[maxScanLineSamples*2];
    struct {
      double slope;
      double intercept;
      double fit;
    }   coeff[maxFrameSamples];
    int sampleCount;

    double estRefreshCyc [maxFrameSamples];
    double sumRefCyc = 0.0;
    double aveRefCyc = 0.0;
    
    int reqFrameSamples = min(maxFrameSamples, max(3, numFrames));

    //Wait for vsync
    line = 0;
    m_pD3DDev->GetRasterStatus(0, &rasterStatus);
    while (rasterStatus.ScanLine >= line) 
    {
      line = rasterStatus.ScanLine;
      m_pD3DDev->GetRasterStatus(0, &rasterStatus);
      if (rasterStatus.ScanLine > dParams.maxScanLine) 
      {
        dParams.maxScanLine = rasterStatus.ScanLine;
      }
    }

    Log("Starting frame loops: start scanline: %d", rasterStatus.ScanLine);

    endTime = GetCurrentTimestamp();
    startTimeLR = endTime;
  
    // Now we're at the start of a vsync
    for (int i = 0; i < reqFrameSamples; i++)
    {      
      startTime = endTime;
      //Skip over vertical blanking period
      m_pD3DDev->GetRasterStatus(0, &rasterStatus);
      while (rasterStatus.ScanLine < 2)
      {
        m_pD3DDev->GetRasterStatus(0, &rasterStatus);
      } 
      startLine = rasterStatus.ScanLine;
      
      if (startLine < dParams.minVisScanLine)
        dParams.minVisScanLine = startLine;

      // make a few measurements
      Log("Starting Frame: %d, start scanline: %d", i, startLine);
      sampleCount = MeasureScanLines(startTimeLR, times, scanLines, maxScanLineSamples, &dParams.maxScanLine);
      // Now we're at the next vsync
      m_pD3DDev->GetRasterStatus(0, &rasterStatus);
      endLine = rasterStatus.ScanLine;
      endTime = GetCurrentTimestamp();

      Log("Ending Frame: %d, start scanline: %d, end scanline: %d, maxScanline: %d", i, startLine, endLine, dParams.maxScanLine);

      estRefreshCyc[i] = (double)(endTime - startTime); // in hns units
      sumRefCyc += estRefreshCyc[i];
      
      coeff[i].fit = LinearRegression(scanLines, times, sampleCount, &coeff[i].slope, &coeff[i].intercept);
      Log("  samples = %d, slope = %.6f, intercept = %.6f, fit = %.6f", sampleCount, coeff[i].slope, coeff[i].intercept, coeff[i].fit);
    }    

    dParams.maxVisScanLine = dParams.maxScanLine;

    // Restore thread priority
    if (priority != THREAD_PRIORITY_ERROR_RETURN)
    {
      SetThreadPriority(GetCurrentThread(), priority);
    }

    //-----------------------------------------------------
    // Calculate the simplistic refresh rate estimate
    //-----------------------------------------------------

    aveRefCyc = sumRefCyc / (double)reqFrameSamples;
    
    AllowedError = 0.0;
    currError = 0.0;
    int BadIdx0 = 0;
    // Find worst match with average refresh period so it can be removed 
    for (int i = 0; i < reqFrameSamples; ++i)
    {
      currError = fabs(1.0 - (aveRefCyc / estRefreshCyc[i]) );
      if (currError > AllowedError)
      {
        AllowedError = currError;
        BadIdx0 = i;
      }
    }
    
    sumRefCyc -= estRefreshCyc[BadIdx0];

    aveRefCyc = sumRefCyc / (double)(reqFrameSamples - 1);
    
    AllowedError = 0.0;
    currError = 0.0;
    int BadIdx1 = 0;
    // Find next worst match with new average refresh period so it can be removed 
    for (int i = 0; i < reqFrameSamples; ++i)
    {
      currError = fabs(1.0 - (aveRefCyc / estRefreshCyc[i]) );
      if ((currError > AllowedError) && (i != BadIdx0))
      {
        AllowedError = currError;
        BadIdx1 = i;
      }
    }
    sumRefCyc -= estRefreshCyc[BadIdx1];

    double simpleFrameTime = sumRefCyc / (double)(reqFrameSamples - 2); // in hns units

    //--------------------------------------------------------------
    // Calculate the linear regression refresh rate estimate
    //--------------------------------------------------------------
    
    // Find the best matching measurement and the minimum frame time
    int bestFitIdx = 0;
    double minFrameTime = DBL_MAX;
    int frameCount = 0;

    for (int i = 1; i < reqFrameSamples; i++)
    {
      if (coeff[i].fit > coeff[bestFitIdx].fit)
        bestFitIdx = i;
      if (minFrameTime > coeff[i].intercept - coeff[i-1].intercept)
        minFrameTime = coeff[i].intercept - coeff[i-1].intercept;
    }

    // Find the number of frames measured
    for (int i = 1; i < reqFrameSamples; i++)
    {
      frameCount += (int)floor((coeff[i].intercept - coeff[i-1].intercept)/minFrameTime + 0.5);
    }
    
    Log("  frame count = %d", frameCount);
    double scanLineTime = coeff[bestFitIdx].slope;
    double frameTime = (coeff[reqFrameSamples-1].intercept - coeff[0].intercept)/frameCount;

    //--------------------------------------------------------------
    // Compare the two methods
    //--------------------------------------------------------------

    AllowedError = 0.05; //Allow 5.0% error

    currError = fabs(1.0 - (simpleFrameTime / frameTime));
    if (currError < AllowedError)
    {
      dParams.estRefreshLock = true;
    }
    m_dEstRefCycDiff = currError;

    dParams.maxScanLine = ((UINT)(frameTime / scanLineTime)) - 1;    
    dParams.dEstRefreshCycle = frameTime / 10000.0; // in milliseconds
    dParams.dDetectedScanlineTime = scanLineTime / 10000.0; 

    m_pD3DDev->GetDisplayMode(0, &m_displayMode); //update this just in case anything has changed...
    GetRealRefreshRate(); // update m_dD3DRefreshCycle and m_dD3DRefreshRate values
    
    if ((dParams.dEstRefreshCycle < 5.0) || (dParams.dEstRefreshCycle > 100.0)) // just in case it's gone badly wrong...
    {
      Log("Display refresh estimation failed, measured display cycle: %.6f ms", dParams.dEstRefreshCycle);
      dParams.dEstRefreshCycle = m_dD3DRefreshCycle;
      dParams.dDetectedScanlineTime = m_dD3DRefreshCycle/(double)(m_displayMode.Height); // in milliseconds
      dParams.maxScanLine = m_displayMode.Height;
      dParams.maxVisScanLine = m_displayMode.Height;
      dParams.minVisScanLine = 5;
      dParams.estRefreshLock = false;
    }

    Log("Raw est display cycle, linReg: %.6f ms, simple: %.6f ms, diff: %.6f ", frameTime/10000.0, simpleFrameTime/10000.0, currError);
    Log("Measured display cycle: %.6f ms, locked: %d ", dParams.dEstRefreshCycle, dParams.estRefreshLock);
    Log("Measured scanline time: %.6f us", (dParams.dDetectedScanlineTime * 1000.0));
    Log("Display (from windows): %d x %d @ %.6f Hz | Measured refresh rate: %.6f Hz", m_displayMode.Width, m_displayMode.Height, m_dD3DRefreshRate, 1000.0/dParams.dEstRefreshCycle);
    Log("Max total scanline: %d, Max visible scanline: %d, Min visible scanline: %d", dParams.maxScanLine, dParams.maxVisScanLine, dParams.minVisScanLine);
    
  }
  
  CAutoLock rLock(&m_lockRasterData); //Get lock before raster parameters are updated
  
  //Update raster and vsync correction control values
  m_maxScanLine           = dParams.maxScanLine;
  m_minVisScanLine        = dParams.minVisScanLine;
  m_maxVisScanLine        = dParams.maxVisScanLine;
  m_dDetectedScanlineTime = dParams.dDetectedScanlineTime;
  m_dEstRefreshCycle      = dParams.dEstRefreshCycle;    
  
  m_rasterLimitLow        = (int)((((dParams.maxVisScanLine - dParams.minVisScanLine) * 2)/16) + dParams.minVisScanLine); 
  m_rasterTargetPosn      = m_rasterLimitLow;
  m_rasterLimitHigh       = (int)((((dParams.maxVisScanLine - dParams.minVisScanLine) * 9)/16) + dParams.minVisScanLine);
  m_rasterLimitNP         = (int)dParams.maxVisScanLine; 
  m_hnsScanlineTime       = (LONGLONG) (dParams.dDetectedScanlineTime * 10000.0);
  
  m_estRefreshLock        = dParams.estRefreshLock;
  
  Log("Vsync correction : rasterLimitHigh: %d, rasterLimitLow: %d, rasterTargetPosn: %d", m_rasterLimitHigh, m_rasterLimitLow, m_rasterTargetPosn);

  return dParams.estRefreshLock;
}


// Update the array m_pllJitter with a new vsync period. Calculate min, max and stddev.
void MPEVRCustomPresenter::CalculateJitter(LONGLONG PerfCounter)
{
  m_nNextJitter = (m_nNextJitter+1) % NB_JITTER;
  m_pllJitter[m_nNextJitter] = PerfCounter - m_llLastPerf;
  
  m_pllRasterSyncOffset[m_nNextJitter] = m_rasterSyncOffset;

  double syncDeviation = ((double)m_pllJitter[m_nNextJitter] - m_fJitterMean) / 10000.0;
  
  if (abs(syncDeviation) > (GetDisplayCycle() / 2))
  {
    // ignore glitches until enough data has been collected
    if (m_iFramesDrawn > NB_JITTER)
    {
      m_uSyncGlitches++;
    }
  }

  LONGLONG llJitterSum = 0;
  LONGLONG llJitterSumAvg = 0;
  for (int i = 0; i < NB_JITTER; i++)
  {
    LONGLONG Jitter = m_pllJitter[i];
    llJitterSum += Jitter;
    llJitterSumAvg += Jitter;
  }
  m_fJitterMean = double(llJitterSumAvg) / NB_JITTER;
  double DeviationSum = 0;

  for (int i = 0; i < NB_JITTER; i++)
  {
    LONGLONG DevInt = m_pllJitter[i] - (LONGLONG)m_fJitterMean;
    double Deviation = (double)DevInt;

    DeviationSum += Deviation*Deviation;
    
    if (m_iFramesDrawn > NB_JITTER)
    {
      m_MaxJitter = max(m_MaxJitter, DevInt);
      m_MinJitter = min(m_MinJitter, DevInt);
    }
  }

  m_fJitterStdDev = sqrt(DeviationSum/NB_JITTER);
  m_fAvrFps = 10000000.0/(double(llJitterSum)/NB_JITTER);
  m_llLastPerf = PerfCounter;
}


// Collect the difference between periodEnd and periodStart in an array, calculate mean and stddev.
void MPEVRCustomPresenter::OnVBlankFinished(bool fAll, LONGLONG periodStart, LONGLONG periodEnd)
{
  m_nNextSyncOffset = (m_nNextSyncOffset+1) % NB_JITTER;
  m_pllSyncOffset[m_nNextSyncOffset] = periodEnd - periodStart;

  LONGLONG AvrageSum = 0;
  for (int i = 0; i < NB_JITTER; i++)
  {
    LONGLONG Offset = m_pllSyncOffset[i];
    AvrageSum += Offset;
    m_MaxSyncOffset = max(m_MaxSyncOffset, Offset);
    m_MinSyncOffset = min(m_MinSyncOffset, Offset);
  }
  double MeanOffset = double(AvrageSum)/NB_JITTER;
  double DeviationSum = 0;
  for (int i = 0; i < NB_JITTER; i++)
  {
    double Deviation = double(m_pllSyncOffset[i]) - MeanOffset;
    DeviationSum += Deviation*Deviation;
  }
  double StdDev = sqrt(DeviationSum/NB_JITTER);

  m_fSyncOffsetAvr = MeanOffset;
  m_fSyncOffsetStdDev = StdDev;
}

// Update the array m_pllRFP with a new frame time stamp. Calculate mean and stddev.
void MPEVRCustomPresenter::CalculateRealFramePeriod(LONGLONG timeStamp)
{
  LONGLONG rfpDiff = timeStamp - m_llLastRFPts;
  if (rfpDiff < 0) rfpDiff = -rfpDiff;
  m_llLastRFPts = timeStamp;
  
  if ( (rfpDiff <= (m_DetectedFrameTime * 11000000)) && 
       (rfpDiff >= (m_DetectedFrameTime *  9000000)) && 
       (m_DetectedFrameTime > DFT_THRESH) )   //ignore out-of-usable-range values
  {
    m_pllRFP[(m_nNextRFP % NB_RFPSIZE)] = rfpDiff;
    m_nNextRFP++;
  }
  
  LONGLONG llRFPSumAvg = 0;
  int rfpFrames = NB_RFPSIZE;
  if ((m_nNextRFP >= 10) && (m_nNextRFP < NB_RFPSIZE))
  {
    rfpFrames = m_nNextRFP;
  }
  
  if (m_nNextRFP >= rfpFrames)
  {
    for (int i = 0; i < rfpFrames; i++)
    {
      llRFPSumAvg += m_pllRFP[i];
    }
  }
  else
  {
    m_fRFPMean = (m_DetectedFrameTime * 10000000);
    m_fRFPStdDev = 0.0;
    return;
  }
  m_fRFPMean = double(llRFPSumAvg) / rfpFrames;

  if (m_bDrawStats)
  {
    double DeviationSum = 0;
    double Deviation    = 0;
    for (int i = 0; i < rfpFrames; i++)
    {
      Deviation = (double) (m_pllRFP[i] - (LONGLONG)m_fRFPMean);
      DeviationSum += Deviation*Deviation;  
    }
    m_fRFPStdDev = sqrt(DeviationSum/rfpFrames);
  }
  
}


void MPEVRCustomPresenter::ResetTraceStats()
{
  m_uSyncGlitches   = 0;
  m_PaintTimeMin    = MAXLONG64;
  m_PaintTimeMax    = 0;
  m_MinJitter       = MAXLONG64;
  m_MaxJitter       = MINLONG64;
  m_MinSyncOffset   = MAXLONG64;
  m_MaxSyncOffset   = MINLONG64;
  m_bResetStats     = false;
}

void MPEVRCustomPresenter::ResetFrameStats()
{
  m_iFramesDrawn    = 0;
  m_iFramesDropped  = 0;
  m_iEarlyFrCnt     = 0;
  m_iFramesProcessed = 0;
  m_iFramesHeld     = 0;
  m_iFramesDelayed  = 0;
  m_iLateFrames     = 0;
  
  m_nNextCFP = 0;
  m_fCFPMean = 0;
  m_llCFPSumAvg = 0;
  ZeroMemory((void*)&m_pllCFP, sizeof(LONGLONG) * NB_CFPSIZE);
  
  m_nNextPCD = 0;
  m_fPCDMean = 1.0;
  m_fPCDSumAvg = 0.0;
  ZeroMemory((void*)&m_pllPCD, sizeof(double) * NB_PCDSIZE);
  
  m_DetectedFrameTimePos  = 0;
  m_DetectedLock          = false;
  m_DetectedFrameTime     = -1.0;  
  m_DectedSum             = 0;
  ZeroMemory((void*)&m_DetectedFrameTimeHistory, sizeof(LONGLONG) * NB_DFTHSIZE);
  
  m_LastScheduledUncorrectedSampleTime = -1;
  m_frameRateRatio = 0;
  m_frameRateRatX2 = 0;
  m_rawFRRatio = 0;
  m_nstPhaseDiffUpd = false;

  m_stallTime = 0;
  m_earliestPresentTime = 0;
  m_lastPresentTime = 0;
  m_hnsNSToffset = 0;
  m_NSTinitDone = false;
  m_NSToffsUpdate = true;
  
  m_nNextRFP = 0;
    
  m_PaintTime = 0;
  m_lastDelayErr = 0;

  m_qGoodPopCnt     = 0;
  m_qBadPopCnt      = 0; 
  m_qGoodPutCnt     = 0;
  m_qBadPutCnt      = 0; 
  m_qBadSampTimCnt  = 0; 
  m_qCorrSampTimCnt = 0; 

  m_iClockAdjustmentsDone = 0;
  m_RepeatRender          = false;
}


REFERENCE_TIME MPEVRCustomPresenter::GetFrameDuration()
{
  // TODO find a better place for this? Multi monitor support?
  if(m_dCycleDifference == 0.0 && m_rtTimePerFrame)
  {
    m_dCycleDifference = GetCycleDifference();
  }

  if (m_DetectedFrameTime > DFT_THRESH) 
  {
    return (REFERENCE_TIME)(m_DetectedFrameTime * 10000000.0);
  }
  else
  {
    return m_rtTimePerFrame;
  }

}


// Get the best estimate of the display refresh rate in Hz
double MPEVRCustomPresenter::GetRefreshRate()
{
  return m_dD3DRefreshRate;
}


// Get the best estimate of the display cycle time in milliseconds
double MPEVRCustomPresenter::GetDisplayCycle()
{
  return m_dEstRefreshCycle;
}

// Get detected frame duration in seconds
double MPEVRCustomPresenter::GetDetectedFrameTime()
{
  return m_DetectedFrameTime;
}

// Get best estimate of actual frame duration in seconds
double MPEVRCustomPresenter::GetRealFramePeriod(bool getReported)
{
  double rtimePerFrame ;
  
  if ((m_DetectedFrameTime > DFT_THRESH) && !getReported) 
  {
    if (m_DetectedFrameTimePos < NB_DFTHSIZE) //Estimate is not accurate
    {
      rtimePerFrame = 0.0;
    }
    else
    {
      rtimePerFrame = m_DetectedFrameTime; // in seconds
    }
  }
  else
  {
    rtimePerFrame = ((double) m_rtTimePerFrame)/10000000.0; // in seconds
  }
  
  return rtimePerFrame;
}

void MPEVRCustomPresenter::GetFrameRateRatio()
{
  double rtimePerFrameMs; // in ms
  double currentDispCycle = GetDisplayCycle(); // in ms
  
  if (m_DetectedFrameTime > DFT_THRESH) 
  {
    rtimePerFrameMs = m_DetectedFrameTime * 1000.0; // in ms
  }
  else
  {
    rtimePerFrameMs = ((double) m_rtTimePerFrame)/10000.0; // in ms
  }
  
  // Compensate to get actual time per frame after ReClock speed up/down
  rtimePerFrameMs = rtimePerFrameMs/m_fPCDMean;
  
  int F2DRatioP6 = (int)((rtimePerFrameMs * 1.015)/currentDispCycle); // Allow +1.5% tolerance
  int F2DRatioN6 = (int)((rtimePerFrameMs * 0.985)/currentDispCycle); // Allow -1.5% tolerance

  m_rawFRRatio = F2DRatioP6;

  if (F2DRatioP6 == 0 || (F2DRatioP6 == F2DRatioN6)) 
  {
    m_frameRateRatio = 0;
  }
  else
  {
    m_frameRateRatio = F2DRatioP6;
  }

  int F4DRatX2P6 = (int)((rtimePerFrameMs * 2.03)/currentDispCycle); // Allow +1.5% tolerance
  int F4DRatX2N6 = (int)((rtimePerFrameMs * 1.97)/currentDispCycle); // Allow -1.5% tolerance

  if (F4DRatX2P6 == 0 || (F4DRatX2P6 == F4DRatX2N6)) 
  {
    m_frameRateRatX2 = 0;
  }
  else
  {
    m_frameRateRatX2 = F4DRatX2P6;
  }
 
  if (!(m_DetectedFrameTime > DFT_THRESH) || (m_iFramesDrawn < FRAME_PROC_THRESH) )
  {
    m_frameRateRatio = 0;
    m_frameRateRatX2 = 0;
  }
  else if (m_iFramesDrawn < FRAME_PROC_THRSH2)
  {
    m_frameRateRatio = F2DRatioP6;
    m_frameRateRatX2 = F4DRatX2P6;
  }

}

// Get the difference in video and display cycle times.
double MPEVRCustomPresenter::GetCycleDifference()
{
	double dBaseDisplayCycle = GetDisplayCycle();
	UINT i, j;
	double minDiff = 1.0;
	
	if (dBaseDisplayCycle == 0.0 || m_dFrameCycle == 0.0)
  {
    return 1.0;
  }
  else
	{
    for (j = 1; j <= 2; j++) 
		{
  	  double dFrameCycle = j * m_dFrameCycle;
      for (i = 1; i <= 5; i++) 
  		{
  			double dDisplayCycle = i * dBaseDisplayCycle;
  			double diff = (dDisplayCycle - dFrameCycle) / dFrameCycle;
  			if (abs(diff) < abs(minDiff))
  			{
  				minDiff = diff;
  				m_dOptimumDisplayCycle = dDisplayCycle;
  			}
  		}
  	}
	}
	
	return minDiff;
}


void MPEVRCustomPresenter::NotifyRateChange(double pRate)
{
  if (pRate != m_fSeekRate)
  {
    Log("NotifyRateChange: %f", pRate);
    m_fSeekRate = pRate;
    if (m_fSeekRate != 1.0)
    {
      if (m_fSeekRate == 0.0) //Special case for skip-step FFWD/RWD mode
      {
        m_bScrubbing = true;
        m_bZeroScrub = true;
      }
      else
      {
        m_bScrubbing = true;
        m_bZeroScrub = false;
      }
    }
    else
    {
      m_bScrubbing = false;
      m_bZeroScrub = false;
    }
  }
}

void MPEVRCustomPresenter::NotifyDVDMenuState(bool pIsInMenu)
{
  if (pIsInMenu != m_bDVDMenu)
  {
    Log("NotifyDVDMenu: %d", pIsInMenu);
    m_bDVDMenu = pIsInMenu;
  }
}

void MPEVRCustomPresenter::UpdateDisplayFPS()
{
  for (int i = 0; i < 2; i++)
  {
    if (EstimateRefreshTimings(8, THREAD_PRIORITY_ABOVE_NORMAL))
    {
      break; //only go round the loop again if we don't get a good result
    }
  }
  
  SetupAudioRenderer(); //Bias value needs to be updated
}

void MPEVRCustomPresenter::CorrectSampleTime(IMFSample* pSample)
{
  LONGLONG PrevTime = m_LastScheduledUncorrectedSampleTime;
  LONGLONG Time;
  LONGLONG SetDuration;
  pSample->GetSampleDuration(&SetDuration);
  pSample->GetSampleTime(&Time);
  m_LastScheduledUncorrectedSampleTime = Time;
  
  LONGLONG Diff = Time - PrevTime;

  if (PrevTime == -1)
    Diff = 0;
  if (Diff < 0)
    Diff = -Diff;
    
  m_SampDuration = SetDuration;
  
  if ( ((SetDuration - Diff) > 50000)  || (-(SetDuration - Diff) > 50000) ) //more than 5ms difference
  {
    m_qBadSampTimCnt++;
  }

  if ((Diff < m_rtTimePerFrame*8 && m_rtTimePerFrame && 
      m_fRate == 1.0f && !m_bDVDMenu) || m_bScrubbing)
  {
    int iPos = (m_DetectedFrameTimePos % NB_DFTHSIZE);
    m_DectedSum -= m_DetectedFrameTimeHistory[iPos];
    m_DetectedFrameTimeHistory[iPos] = Diff;
    m_DectedSum += Diff;
    m_DetectedFrameTimePos++;
    
    double Average = (double)Diff;
    
    if (m_DetectedFrameTimePos >= NB_DFTHSIZE)
    {
      Average = (double)m_DectedSum / (double)NB_DFTHSIZE;
    }
    else if (m_DetectedFrameTimePos >= 4)
    {
      Average = (double)m_DectedSum / (double)m_DetectedFrameTimePos;
    }

    if (m_DetectedFrameTimePos >= 4)
    {     
      if (m_bDrawStats)
      {
        int nFrames = min(m_DetectedFrameTimePos, NB_DFTHSIZE);
        double DeviationSum = 0.0;
        for (int i = 0; i < nFrames; ++i)
        {
          double Deviation = m_DetectedFrameTimeHistory[i] - Average;
          DeviationSum += Deviation*Deviation;
        }
  
        double StdDev = sqrt(DeviationSum/double(nFrames));
  
        m_DetectedFrameTimeStdDev = StdDev;
      }

      double DetectedTime = Average / 10000000.0;
      
      m_DetFrameTimeAve = DetectedTime;
      
      bool bFTdiff = false;      
      if (m_DetectedFrameTime && DetectedTime)
      {
        bFTdiff = fabs(1.0 - (DetectedTime / m_DetectedFrameTime)) > 0.01; //allow 1% drift before re-calculating
      }
      
      if (bFTdiff || (m_DetectedFrameTimePos < NB_DFTHSIZE))
			{
	      double AllowedError = 0.025; //Allow 2.5% error to cover (ReClock ?) sample timing jitter
	      static double AllowedValues[] = {1000.5/30000.0, 1000.0/25000.0, 1000.5/24000.0};  //30Hz and 24Hz are compromise values
	      static double AllowedDivs[] = {4.0, 2.0, 1.0, 0.5};
	
	      double BestVal = 0.0;
	      double currError = AllowedError;
	      int nAllowed = sizeof(AllowedValues) / sizeof(AllowedValues[0]);
	      int nAllDivs = sizeof(AllowedDivs) / sizeof(AllowedDivs[0]);
	      
	      // Find best match with allowed frame periods
	      for (int i = 0; i < nAllowed; ++i)
	      {
	        for (int j = 1; j < nAllDivs; j++)
	        {
	          currError = fabs(1.0 - (DetectedTime / (AllowedValues[i] / AllowedDivs[j]) ));
	          if (currError < AllowedError)
	          {
	            AllowedError = currError;
	            BestVal = (AllowedValues[i] / AllowedDivs[j]);
	          }
	        }
	      }
	
	      if (BestVal != 0.0)
	      {
	        m_DetectedLock = true;
	        m_DetectedFrameTime = BestVal;
	      }
	      else
	      {
	        m_DetectedLock = false;
	        m_DetectedFrameTime = DetectedTime;
	      }
	    }
    }

  }
  else if ((Diff >= m_rtTimePerFrame*8) && m_rtTimePerFrame)
  {
    // Seek, so reset the averaging logic
    m_DetectedFrameTimePos = 0;
    m_DetectedLock = false;
    m_DectedSum = 0;
    ZeroMemory((void*)&m_DetectedFrameTimeHistory, sizeof(LONGLONG) * NB_DFTHSIZE);
  }
  
  GetFrameRateRatio(); // update video to display FPS ratio data
    
  LOG_TRACE("EVR: Time: %f %f %f\n", Time / 10000000.0, SetDuration / 10000000.0, m_DetectedFrameTime);
}


// get driver refresh rate
void MPEVRCustomPresenter::GetRealRefreshRate()
{
  // Win7
  if (m_bIsWin7 && m_pW7GetRefreshRate)
  {
    m_dD3DRefreshRate = m_pW7GetRefreshRate();

    if (m_dD3DRefreshRate == -1)
    {
      m_dD3DRefreshRate = (double)m_displayMode.RefreshRate;
    }
  }
  else // XP or Vista
  {
    m_dD3DRefreshRate = (double)m_displayMode.RefreshRate;
  }
  m_dD3DRefreshCycle = 1000.0 / m_dD3DRefreshRate; // in ms
}



// returns true if 'now' is inside the limitLow/limitHigh window
bool MPEVRCustomPresenter::GetDelayToRasterTarget(LONGLONG *offsetTime)
{
  D3DRASTER_STATUS rasterStatus;
  bool inWindow = false;
  
  // Calculate raster offset
  if (SUCCEEDED(m_pD3DDev->GetRasterStatus(0, &rasterStatus)))
  {
    CAutoLock rLock(&m_lockRasterData); //Get lock to stop raster parameters being updated
    
    int currScanline = (int)rasterStatus.ScanLine;
    
    if (
        (currScanline >= m_rasterLimitLow) && 
        (
          ( currScanline <= (m_rasterLimitHigh + (*offsetTime/m_hnsScanlineTime))) ||    //Normal term
          ((currScanline <= m_rasterLimitNP) && (m_iLateFrames == (LF_THRESH_HIGH - 1))) //Delayed sample term (wider window)
        )
       )
    {        
      inWindow = true ; //It's within the allowed range for presentation
    }
              
    *offsetTime = 0;
    if ((currScanline < (int)m_maxScanLine) && inWindow)
    {
      // calculate time delay (in hns) to end of raster
      *offsetTime = (LONGLONG)((int)m_maxScanLine - currScanline) * m_hnsScanlineTime ;
    }    
  }
  else
  {
    *offsetTime = 0;
  }
  
  return inWindow;
}



// Update the array m_pllCFP with a new time stamp. Calculate mean.
void MPEVRCustomPresenter::CalculateNSTStats(LONGLONG timeStamp, LONGLONG frameTime)
{
  LONGLONG cfpDiff = timeStamp;
        
  int tempNextCFP = (m_nNextCFP % NB_CFPSIZE);
  m_llCFPSumAvg -= m_pllCFP[tempNextCFP];
  m_pllCFP[tempNextCFP] = cfpDiff;
  m_llCFPSumAvg += cfpDiff;
  m_nNextCFP++;
  
  if (m_nNextCFP >= NB_CFPSIZE)
  {
    m_fCFPMean = m_llCFPSumAvg / (LONGLONG)NB_CFPSIZE;
  }
  else if (m_nNextCFP > 0)
  {
    m_fCFPMean = m_llCFPSumAvg / (LONGLONG)m_nNextCFP;
  }
  else
  {
    m_fCFPMean = cfpDiff;
  }

  //Calculate 'next sample time' correction offset
  //This is used to centre the timing window for the frame drop/stall logic
  //It is updated every NB_CFPSIZE frames
  if (tempNextCFP == (NB_CFPSIZE-1))
  {
    if (m_fCFPMean < 0)
    {
      m_hnsNSToffset = -( -m_fCFPMean % frameTime);
    }
    else
    {
      m_hnsNSToffset = m_fCFPMean % frameTime;
    }
    m_NSTinitDone = true;
    m_NSToffsUpdate = true;
  }
  else
  {
    m_NSToffsUpdate = !m_NSTinitDone; //force 'true' initially
  }
      
}


  // This function calculates the (average) ratio between the presentation clock and
  // system clock - Audio renderer can modify the presentation clock when performing speed up/down.
  // It must be called with the values returned from GetCorrelatedTime() as input
void MPEVRCustomPresenter::CalculatePresClockDelta(LONGLONG presTime, LONGLONG sysTime)
{
  LONGLONG prsDiff = presTime - m_llLastPCDprsTs;
  if (prsDiff < 0) prsDiff = -prsDiff;

  LONGLONG sysDiff = sysTime - m_llLastPCDsysTs;
  if (sysDiff < 0) sysDiff = -sysDiff;
  
  if ((prsDiff < 10000) || (sysDiff < 10000)) //ignore short intervals
  {
    return;
  }

  m_llLastPCDprsTs = presTime;
  m_llLastPCDsysTs = sysTime;  
  
  double sysPrsRatio = (double)prsDiff/(double)sysDiff;
  
  // Clamp large differences to within sensible audio renderer speed up/down limits
  if (sysPrsRatio > m_dMaxBias) 
  {
    sysPrsRatio = m_dMaxBias;
  }
  else if (sysPrsRatio < m_dMinBias)
  {
    sysPrsRatio = m_dMinBias;
  }

  int tempNextPCD = (m_nNextPCD % NB_PCDSIZE);
  m_fPCDSumAvg -= m_pllPCD[tempNextPCD];
  m_pllPCD[tempNextPCD] = sysPrsRatio;
  m_fPCDSumAvg += sysPrsRatio;
  m_nNextPCD++;
  
  if (m_nNextPCD >= NB_PCDSIZE)
  {
    m_fPCDMean = m_fPCDSumAvg / (double)NB_PCDSIZE;
  }
  else if (m_nNextPCD >= 10)
  {
    m_fPCDMean = m_fPCDSumAvg / (double)m_nNextPCD;
  }
  else
  {
    m_fPCDMean = 1.0;
  }
}

bool MPEVRCustomPresenter::QueryFpsFromVideoMSDecoder()
{
  FILTER_INFO filterInfo;
  ZeroMemory(&filterInfo, sizeof(filterInfo));
  m_EVRFilter->QueryFilterInfo(&filterInfo); // This addref's the pGraph member

  CComPtr<IBaseFilter> pBaseFilter;

  HRESULT hr = filterInfo.pGraph->FindFilterByName(L"Microsoft DTV-DVD Video Decoder", &pBaseFilter);
  filterInfo.pGraph->Release();
  if (hr == S_OK)
  {
    IPin* pin;
    HRESULT rr = pBaseFilter->FindPin(L"Video Input", &pin);
    CMediaType mt; 
    pin->ConnectionMediaType(&mt);

    REFERENCE_TIME rtAvgTimePerFrame = 0;   
    bool goodFPS = ExtractAvgTimePerFrame(&mt, rtAvgTimePerFrame);
    if (goodFPS && rtAvgTimePerFrame)
    {
      m_rtTimePerFrame = rtAvgTimePerFrame;
      Log("Found Microsoft DTV-DVD Video Decoder - FPS from Video Input pin: %.3f", 10000000.0/m_rtTimePerFrame);
    }
    else
    {
      // if fps information is not provided leave m_rtTimePerFrame unchanged
      Log("Found Microsoft DTV-DVD Video Decoder - no FPS from Video Input pin available");
    }
    
    return true;
  }

  return false;
}


bool MPEVRCustomPresenter::ExtractAvgTimePerFrame(const AM_MEDIA_TYPE* pmt, REFERENCE_TIME& rtAvgTimePerFrame)
{  
	if (pmt->formattype==FORMAT_VideoInfo)
		rtAvgTimePerFrame = ((VIDEOINFOHEADER*)pmt->pbFormat)->AvgTimePerFrame;
	else if (pmt->formattype==FORMAT_VideoInfo2)
		rtAvgTimePerFrame = ((VIDEOINFOHEADER2*)pmt->pbFormat)->AvgTimePerFrame;
	else if (pmt->formattype==FORMAT_MPEGVideo)
		rtAvgTimePerFrame = ((MPEG1VIDEOINFO*)pmt->pbFormat)->hdr.AvgTimePerFrame;
	else if (pmt->formattype==FORMAT_MPEG2Video)
		rtAvgTimePerFrame = ((MPEG2VIDEOINFO*)pmt->pbFormat)->hdr.AvgTimePerFrame;
	else
		return false;

	return true;
}


//=============== Audio Renderer interface functions =================

void MPEVRCustomPresenter::GetAVSyncClockInterface()
{
  if (m_pAVSyncClock || NO_MP_AUD_REND)
  {
    return;
  }

  m_bMsVideoCodec = QueryFpsFromVideoMSDecoder();
  SetupAudioRenderer();

  FILTER_INFO filterInfo;
  ZeroMemory(&filterInfo, sizeof(filterInfo));
  m_EVRFilter->QueryFilterInfo(&filterInfo); // This addref's the pGraph member

  CComPtr<IBaseFilter> pBaseFilter;

  HRESULT hr = filterInfo.pGraph->FindFilterByName(L"MediaPortal - Audio Renderer", &pBaseFilter);
  filterInfo.pGraph->Release();
  if (hr != S_OK)
  {
    Log("failed to find MediaPortal - Audio Renderer filter!");
    return;
  }

  hr = pBaseFilter->QueryInterface(IID_IAVSyncClock, (void**)&m_pAVSyncClock);
  
  if (hr != S_OK)
  {
    Log("Could not get IAVSyncClock interface");
    return;
  }

  Log("Found MediaPortal - Audio Renderer filter");

  if (m_pAVSyncClock)
  {
    m_pAVSyncClock->GetMaxBias(&m_dMaxBias);
    m_pAVSyncClock->GetMinBias(&m_dMinBias);
    
    Log("   Allowed bias values between %1.10f and %1.10f", m_dMinBias, m_dMaxBias);

    if (S_OK == m_pAVSyncClock->SetBias(m_dBias))
    {
      m_bBiasAdjustmentDone = true;
      Log("  Adjusting bias to: %1.10f", m_dBias);
    }
    else
    {
      m_bBiasAdjustmentDone = false;
      Log("  Failed to adjust bias to : %1.10f", m_dBias);
    }
    double audioDelayRequired = (double) m_dwmBuffers * m_dFrameCycle;
    if (S_OK == m_pAVSyncClock->SetEVRPresentationDelay(audioDelayRequired))
    {
      Log("SetupAudioRenderer: Delayed Audio by : %1.10f", audioDelayRequired);
    }
    else
    {
      Log("SetupAudioRenderer: failed to set audio delay of: %1.10f", audioDelayRequired);
    }
  }
}

void MPEVRCustomPresenter::SetupAudioRenderer()
{
  if (NO_MP_AUD_REND)
  {
    return;
  }

  m_dFrameCycle = m_rtTimePerFrame / 10000.0;

  double cycleDiff = GetCycleDifference();

  double calculatedBias = 1.0 / (1 + cycleDiff);

  if (m_dMaxBias < calculatedBias || m_dMinBias > calculatedBias)
    return;

  // try to filter out the incorrect frame rates that MS Video decoder can produce 
  // on big CPU / GPU load
  if (fabs(m_dBias - 1.0) < fabs(calculatedBias - 1.0) && 
    m_bBiasAdjustmentDone && m_dBias != calculatedBias)
    return;

  m_dBias = calculatedBias;

  Log("SetupAudioRenderer: cycleDiff: %1.10f", cycleDiff);

  if (m_pAVSyncClock)
  {
    if (S_OK == m_pAVSyncClock->SetBias(m_dBias))
    {
      m_bBiasAdjustmentDone = true;
      Log("SetupAudioRenderer: adjust bias to : %1.10f", m_dBias);
    }
    else
    {
      m_bBiasAdjustmentDone = false;
      Log("SetupAudioRenderer: failed to adjust bias to : %1.10f", m_dBias);
    }
    double audioDelayRequired = (double) m_dwmBuffers * m_dFrameCycle;
    if (S_OK == m_pAVSyncClock->SetEVRPresentationDelay(audioDelayRequired))
    {
      Log("SetupAudioRenderer: Delayed Audio by : %1.10f", audioDelayRequired);
    }
    else
    {
      Log("SetupAudioRenderer: failed to set audio delay of: %1.10f", audioDelayRequired);
    }
  }
  else
  {
    Log("SetupAudioRenderer: adjust bias to : %1.10f - wait audio renderer to be available", m_dBias);
  }
}

void MPEVRCustomPresenter::AdjustAVSync(double currentPhaseDiff)
{
  // Keep a rolling average of last X deviations from target phase. 
  // These numbers have values between -0.5 and 0.5
  int tempNextPhDev = (m_nNextPhDev % NUM_PHASE_DEVIATIONS);
  m_sumPhaseDiff -= m_dPhaseDeviations[tempNextPhDev];
  m_dPhaseDeviations[tempNextPhDev] = currentPhaseDiff;
  m_sumPhaseDiff += currentPhaseDiff;
  m_nNextPhDev++;

  double averagePhaseDifference = m_sumPhaseDiff / NUM_PHASE_DEVIATIONS;
  
  m_avPhaseDiff = averagePhaseDifference;

  // If we are getting close to target then stop correcting.
  // Since it is a rolling average we will overshoot the target, so we plan to stop early.
  // If we are speeding up, we should stop when above the "green" limit
  if (m_dVariableFreq > 1.0)
  {
    if (averagePhaseDifference > -0.05 )
    {
      m_dVariableFreq = 1.0;
    }
  }
  // If we are slowing down, we should stop when below the "green" limit
  if (m_dVariableFreq < 1.0)
  {
    if (averagePhaseDifference < 0.05 )
    {
      m_dVariableFreq = 1.0;
    }
  }

  // If we have drifted significantly away from target, let us speed up or slow down until we are within above limits again
  if (averagePhaseDifference > 0.1)
  {
    m_dVariableFreq = 1.003;
  }
  if (averagePhaseDifference < -0.1)
  {
    m_dVariableFreq = 0.997;
  }

  //Log("VF: %f averagePhaseDif: %f CP: %f ", m_dVariableFreq, averagePhaseDifference, currentPhase);

  if (m_pAVSyncClock && m_dVariableFreq != m_dPreviousVariableFreq)
  {
    HRESULT hr = m_pAVSyncClock->AdjustClock(1.0/m_dVariableFreq);
    if (hr == S_OK && m_dPreviousVariableFreq == 1.0)
    {
      m_iClockAdjustmentsDone++;
    }
  }

  m_dPreviousVariableFreq = m_dVariableFreq;
}


//=============== Filter Graph interface functions =================

bool MPEVRCustomPresenter::GetFilterNames()
{
  FILTER_INFO filterInfo;
  ZeroMemory(&filterInfo, sizeof(filterInfo));
  HRESULT hr = m_EVRFilter->QueryFilterInfo(&filterInfo); // This addref's the pGraph member

  if (hr == S_OK)
  {
    EnumFilters(filterInfo.pGraph);
    filterInfo.pGraph->Release();

    return true;
  }
  
  filterInfo.pGraph->Release();
  return false;
}


HRESULT MPEVRCustomPresenter::EnumFilters(IFilterGraph *pGraph) 
{
    IEnumFilters *pEnum = NULL;
    IBaseFilter *pFilter;
    ULONG cFetched;
    m_numFilters = 0;

    HRESULT hr = pGraph->EnumFilters(&pEnum);
    if (FAILED(hr)) return hr;

    while(pEnum->Next(1, &pFilter, &cFetched) == S_OK)
    {
        FILTER_INFO FilterInfo;
        hr = pFilter->QueryFilterInfo(&FilterInfo);
        if (FAILED(hr))
        {
            Log("Could not get the filter info");
            continue;  // Maybe the next one will work.
        }

        char szName[MAX_FILTER_NAME];
        int cch = WideCharToMultiByte(CP_ACP, 0, FilterInfo.achName, MAX_FILTER_NAME, szName, MAX_FILTER_NAME, 0, 0);
        
        if (cch > 0 && m_numFilters < FILTER_LIST_SIZE) 
        {
          strcpy_s(m_filterNames[m_numFilters],szName);
          Log("Filter: %s", m_filterNames[m_numFilters]);
          m_numFilters++;
        }
        
        // The FILTER_INFO structure holds a pointer to the Filter Graph
        // Manager, with a reference count that must be released.
        if (FilterInfo.pGraph != NULL)
        {
            FilterInfo.pGraph->Release();
        }
        pFilter->Release();
    }

    pEnum->Release();
    return S_OK;
}



