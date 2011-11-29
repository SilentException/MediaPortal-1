/*
    Copyright (C) 2007-2010 Team MediaPortal
    http://www.team-mediaportal.com

    This file is part of MediaPortal 2

    MediaPortal 2 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    MediaPortal 2 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with MediaPortal 2.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#ifndef __MPIPTVSOURCESTREAM_DEFINED
#define __MPIPTVSOURCESTREAM_DEFINED

#include "LinearBuffer.h"
#include "Logger.h"
#include "ProtocolInterface.h"
#include "ParameterCollection.h"

#define     STATUS_NONE                                               0
#define     STATUS_INITIALIZE_ERROR                                   -1
#define     STATUS_NO_DATA_ERROR                                      -2
#define     STATUS_INITIALIZED                                        1
#define     STATUS_RECEIVING_DATA                                     2

#define     RUN_NO_ERROR                                              0
#define     RUN_ERROR_UNEXPECTED                                      -1
#define     RUN_ERROR_NO_DATA_AVAILABLE                               -2
#define     RUN_ERROR_INITIALIZE                                      -3

struct ProtocolImplementation
{
  TCHAR *protocol;
  HINSTANCE hLibrary;
  PIProtocol pImplementation;
  bool supported;
  DESTROYPROTOCOLINSTANCE destroyProtocolInstance;
};

class CMPIPTVSourceStream  : public CSourceStream
{
public:
  CMPIPTVSourceStream(HRESULT *phr, CSource *pFilter, CParameterCollection *configuration);
  ~CMPIPTVSourceStream(void);

  bool Load(const TCHAR *url, const CParameterCollection *parameters);
  GUID GetInstanceId(void);

private:
  CLogger logger;

  unsigned long long totalReceived;
  unsigned int dllTotal;

  // methods

  HRESULT FillBuffer(IMediaSample *pSamp);
  HRESULT GetMediaType(__inout CMediaType *pMediaType);
  HRESULT DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pRequest);
  HRESULT OnThreadCreate(void);
  HRESULT OnThreadDestroy(void);
  HRESULT OnThreadStartPlay(void);
  HRESULT DoBufferProcessingLoop(void);
  HRESULT Run(REFERENCE_TIME tStart);
  HANDLE lockMutex;

  // everything for the receive data worker thread
  DWORD   dwReceiveDataWorkerThreadId;
  HANDLE  hReceiveDataWorkerThread;
  bool threadShouldExit;
  static DWORD WINAPI ReceiveDataWorker(LPVOID lpParam);

  // status of processing
  int status;
  ProtocolImplementation *protocolImplementations;

  // loads plugins from directory
  void LoadPlugins(void);

  // stores active protocol
  PIProtocol activeProtocol;

  // remember time when Run() method was finished
  DWORD runMethodExecuted;

  // parameters from configuration file
  CParameterCollection *configuration;

  // specifies if output from IPTV filter have to be saved to file
  bool dumpRawTS;

  // specifies is discontinuity have to be analyzed
  bool analyzeDiscontinuity;

  // pointer to array of PID counters
  unsigned int *pidCounters;
};

#endif
