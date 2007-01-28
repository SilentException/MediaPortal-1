/* 
 *	Copyright (C) 2006-2007 Team MediaPortal
 *	http://www.team-mediaportal.com
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

#pragma warning( disable: 4995 4996 )

#include "DVBSub.h"
#include "SubtitleInputPin.h"
#include "SubtitleOutputPin.h"
#include "PcrInputPin.h"
#include "PMTInputPin.h"

const int subtitleSizeInBytes = 720 * 576 * 3;

extern void LogDebug(const char *fmt, ...);

// Setup data
const AMOVIESETUP_MEDIATYPE sudPinTypesSubtitle =
{
	&MEDIATYPE_MPEG2_SECTIONS, &MEDIASUBTYPE_DVB_SI 
};

const AMOVIESETUP_MEDIATYPE sudPinTypesIn =
{
	&MEDIATYPE_NULL, &MEDIASUBTYPE_NULL
};

const AMOVIESETUP_PIN sudPins[4] =
{
	{
		L"In",				        // Pin string name
		FALSE,						    // Is it rendered
		FALSE,						    // Is it an output
		FALSE,						    // Allowed none
		FALSE,						    // Likewise many
		&CLSID_NULL,				  // Connects to filter
		L"In",				        // Connects to pin
		1,							      // Number of types
		&sudPinTypesSubtitle  // Pin information
	},
	{
		L"Out",				        // Pin string name
		FALSE,						    // Is it rendered
		TRUE,						      // Is it an output
		FALSE,						    // Allowed none
		FALSE,						    // Likewise many
		&CLSID_NULL,				  // Connects to filter
		L"Out",				        // Connects to pin
		1,							      // Number of types
		&sudPinTypesSubtitle  // Pin information
	},
  {
		L"PCR",					    // Pin string name
		FALSE,						  // Is it rendered
		FALSE,						  // Is it an output
		FALSE,						  // Allowed none
		FALSE,						  // Likewise many
		&CLSID_NULL,			  // Connects to filter
		L"PCR",					    // Connects to pin
		1,							    // Number of types
		&sudPinTypesIn	    // Pin information
	},
	{
		L"PMT",					    // Pin string name
		FALSE,						  // Is it rendered
		FALSE,						  // Is it an output
		FALSE,						  // Allowed none
		FALSE,						  // Likewise many
		&CLSID_NULL,			  // Connects to filter
		L"PMT",					    // Connects to pin
		1,							    // Number of types
		&sudPinTypesIn	    // Pin information
	}

};
//
// Constructor
//
CDVBSub::CDVBSub( LPUNKNOWN pUnk, HRESULT *phr, CCritSec *pLock ) :
  CBaseFilter( NAME("MediaPortal DVBSub"), pUnk, &m_Lock, CLSID_DVBSub ),
  m_pSubtitleInputPin( NULL ),
	m_pSubDecoder( NULL ),
  m_pSubtitleObserver( NULL )
{
	// Create subtitle decoder
	m_pSubDecoder = new CDVBSubDecoder();
	
	if( m_pSubDecoder == NULL ) 
	{
    if( phr )
	  {
      *phr = E_OUTOFMEMORY;
	  }
    return;
  }

	// Create subtitle input pin
	m_pSubtitleInputPin = new CSubtitleInputPin( this,
								GetOwner(),
								this,
								&m_Lock,
								&m_ReceiveLock,
								m_pSubDecoder, 
								phr );
    
	if ( m_pSubtitleInputPin == NULL ) 
	{
    if( phr )
		{
      *phr = E_OUTOFMEMORY;
		}
    return;
  }

	// Create subtitle output pin
	m_pSubtitleOutputPin = new CSubtitleOutputPin(
                this,
								this,
								&m_ReceiveLock,
								phr );
    
	if ( m_pSubtitleOutputPin == NULL ) 
	{
    if( phr )
		{
      *phr = E_OUTOFMEMORY;
		}
    return;
  }

	// Create pcr input pin
	m_pPcrPin = new CPcrInputPin( this,
								GetOwner(),
								this,
								&m_Lock,
								&m_ReceiveLock,
								phr );

	if ( m_pPcrPin == NULL )
	{
    if( phr )
		{
      *phr = E_OUTOFMEMORY;
		}
      return;
  }

	// Create PMT input pin
	m_pPMTPin = new CPMTInputPin( this,
								GetOwner(),
								this,
								&m_Lock,
								&m_ReceiveLock,
								phr,
                this ); // MPidObserver

	if ( m_pPMTPin == NULL )
	{
    if( phr )
		{
      *phr = E_OUTOFMEMORY;
		}
      return;
  }

	m_pSubDecoder->SetObserver( this );
}

CDVBSub::~CDVBSub()
{
	m_pSubDecoder->SetObserver( NULL );
	delete m_pSubDecoder;
	delete m_pSubtitleInputPin;
  delete m_pSubtitleOutputPin;
	delete m_pPcrPin;
  delete m_pPMTPin;
}

//
// GetPin
//
CBasePin * CDVBSub::GetPin( int n )
{
	if( n == 0 )
		return m_pSubtitleInputPin;

  if( n == 1 )
    return m_pSubtitleOutputPin;

  if( n == 2 )
		return m_pPcrPin;

  if( n == 3 )
		return m_pPMTPin;

  return NULL;
}

int CDVBSub::GetPinCount()
{
	return 4; // subtitle in + out, pmt, pcr
}

HRESULT CDVBSub::CheckConnect( PIN_DIRECTION dir, IPin *pPin )
{
  AM_MEDIA_TYPE mediaType;
  int videoPid = 0;

  pPin->ConnectionMediaType( &mediaType );

  // Search for demuxer's video pin
  if(  mediaType.majortype == MEDIATYPE_Video && dir == PINDIR_INPUT )
  {
	  IMPEG2PIDMap* pMuxMapPid;
	  if( SUCCEEDED( pPin->QueryInterface( &pMuxMapPid ) ) )
    {
		  IEnumPIDMap *pIEnumPIDMap;
		  if( SUCCEEDED( pMuxMapPid->EnumPIDMap( &pIEnumPIDMap ) ) )
      {
			  ULONG count = 0;
			  PID_MAP pidMap;
			  while( pIEnumPIDMap->Next( 1, &pidMap, &count ) == S_OK )
        {
          m_VideoPid = pidMap.ulPID;
          m_pPMTPin->SetVideoPid( m_VideoPid );
          LogDebug( "  found video PID %d",  m_VideoPid );
			  }
		  }
		  pMuxMapPid->Release();
    }
  }
  return S_OK;
}

STDMETHODIMP CDVBSub::Run( REFERENCE_TIME tStart )
{
  CAutoLock cObjectLock( m_pLock );
	Reset();
	return CBaseFilter::Run( tStart );
}

STDMETHODIMP CDVBSub::Pause()
{
  CAutoLock cObjectLock( m_pLock );
	//Reset();
	return CBaseFilter::Pause();
}

STDMETHODIMP CDVBSub::Stop()
{
  CAutoLock cObjectLock( m_pLock );
	Reset();
	return CBaseFilter::Stop();
}

void CDVBSub::Reset()
{
	CAutoLock cObjectLock( m_pLock );

	m_pSubDecoder->Reset();

	m_pSubtitleInputPin->Reset();
  m_pSubtitleOutputPin->Reset();

	m_firstPTS = -1;
}

STDMETHODIMP CDVBSub::Test(int status){
	LogDebug("TEST : %i", status);
	return S_OK;
  }

void CDVBSub::Notify()
{
	LogDebug("NOTIFY");
  if( m_pSubtitleObserver )
  {
	  LogDebug("Calling callback");
    // Notify the callback function
    int retval = (*m_pSubtitleObserver)();
	  LogDebug("Callback returned");
  }
  else
  {
	  LogDebug("No callback set");
  }
}
void CDVBSub::SetPcrPid( LONG pid )
{
  m_pPcrPin->SetPcrPid( pid );
}


void CDVBSub::SetSubtitlePid( LONG pid )
{
  m_pSubtitleInputPin->SetSubtitlePid( pid );
}


//
// Interface methods
//
STDMETHODIMP CDVBSub::GetSubtitle( int place, SUBTITLE* subtitle )
{
  CSubtitle* pCSubtitle = NULL;
  
  if( m_pSubDecoder ) 
  {
    pCSubtitle = m_pSubDecoder->GetSubtitle( place );
  }

  if( pCSubtitle )
  {
	  BITMAP* bitmap = pCSubtitle->GetBitmap();
	  LogDebug("Bitmap: bpp=%i, planes=%i, dim=%i x %i",bitmap->bmBitsPixel,bitmap->bmPlanes, bitmap->bmWidth, bitmap->bmHeight);
	  subtitle->bmBits = bitmap->bmBits;
	  subtitle->bmBitsPixel = bitmap->bmBitsPixel;
    subtitle->bmHeight = bitmap->bmHeight;
	  subtitle->bmPlanes = bitmap->bmPlanes;
	  subtitle->bmType = bitmap->bmType;
	  subtitle->bmWidth = bitmap->bmWidth;
	  LogDebug("Stride: %i" , bitmap->bmWidthBytes);
	  subtitle->bmWidthBytes = bitmap->bmWidthBytes;
    subtitle->firstScanLine = pCSubtitle->FirstScanline();
    subtitle->timeOut = pCSubtitle->Timeout();
	  LogDebug("TIMEOUT: %i", pCSubtitle->Timeout());
    return S_OK;  
  }
  else
  {
    return S_FALSE;
  }
}


STDMETHODIMP CDVBSub::SetCallback( int (CALLBACK *pSubtitleObserver)() )
{
	LogDebug("SetCallback called");
  m_pSubtitleObserver = pSubtitleObserver;
  return S_OK;  
}


STDMETHODIMP CDVBSub::GetSubtitleCount( int* pcount )
{
	LogDebug("GetSubtitleCount");
  if( m_pSubDecoder )
  {
    *pcount = m_pSubDecoder->GetSubtitleCount();
	return S_OK;
  }
  return S_FALSE;  
}


STDMETHODIMP CDVBSub::DiscardOldestSubtitle()
{
	LogDebug("DiscardOldestSubtitle");
  if( m_pSubDecoder )
  {  
    m_pSubDecoder->ReleaseOldestSubtitle();
	return S_OK;
  }
  return S_FALSE;  
}


//
// CreateInstance
//
CUnknown * WINAPI CDVBSub::CreateInstance( LPUNKNOWN punk, HRESULT *phr )
{
  ASSERT( phr );

  LogDebug("CreateInstance");
  CDVBSub *pFilter = new CDVBSub( punk, phr, NULL );
  if( pFilter == NULL ) 
	{
    if (phr)
		{
      *phr = E_OUTOFMEMORY;
		}
  }
  return pFilter;
}


STDMETHODIMP CDVBSub::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{

	if (riid == IID_IDVBSubtitle){
		//LogDebug("QueryInterface in DVBSub.CPP accepting");
		return GetInterface((IDVBSubtitle *) this, ppv);
	}
	else
	{
		//LogDebug("Forwarding query interface call ... ");
		HRESULT hr = CBaseFilter::NonDelegatingQueryInterface(riid,ppv);

		if(SUCCEEDED(hr)){
			//LogDebug("QI succeeded");
		}
		else if(hr == E_NOINTERFACE){
			//LogDebug("QI -> E_NOINTERFACE");
		}
		else{
			//LogDebug("QI failed");
		}
		return hr;
	}
}