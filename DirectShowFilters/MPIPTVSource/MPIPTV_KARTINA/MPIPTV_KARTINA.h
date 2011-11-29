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

#ifndef __MPIPTV_KARTINA_DEFINE_DEFINED
#define __MPIPTV_KARTINA_DEFINE_DEFINED

#include "MPIPTV_KARTINA_Exports.h"
#include "MPIPTV_HTTP.h"

// we should get data in twenty seconds
#define KARTINA_RECEIVE_DATA_TIMEOUT_DEFAULT                      20000
#define KARTINA_OPEN_CONNECTION_MAXIMUM_ATTEMPTS_DEFAULT          3

#define CONFIGURATION_SECTION_KARTINA                             _T("KARTINA")

#define CONFIGURATION_KARTINA_RECEIVE_DATA_TIMEOUT                _T("KartinaReceiveDataTimeout")
#define CONFIGURATION_KARTINA_OPEN_CONNECTION_MAXIMUM_ATTEMPTS    _T("KartinaOpenConnectionMaximumAttempts")

// returns protocol class instance
PIProtocol CreateProtocolInstance(void);

// destroys protocol class instance
void DestroyProtocolInstance(PIProtocol pProtocol);

// This class is exported from the MPIPTV_KARTINA.dll
class MPIPTV_KARTINA_API CMPIPTV_KARTINA : public CMPIPTV_HTTP
{
public:
  // constructor
  // create instance of CMPIPTV_KARTINA class
  CMPIPTV_KARTINA(void);

  // destructor
  ~CMPIPTV_KARTINA(void);

  /* IProtocol interface */
  TCHAR *GetProtocolName(void);
  int Initialize(HANDLE lockMutex, CParameterCollection *configuration);
  int ClearSession(void);
  int ParseUrl(const TCHAR *url, const CParameterCollection *parameters);
  int OpenConnection(void);
  int IsConnected(void);
  void CloseConnection(void);
  void GetSafeBufferSizes(HANDLE lockMutex, unsigned int *freeSpace, unsigned int *occupiedSpace, unsigned int *bufferSize);
  void ReceiveData(bool *shouldExit);
  unsigned int FillBuffer(IMediaSample *pSamp, char *pData, long cbData);
  unsigned int GetReceiveDataTimeout(void);
  unsigned int GetOpenConnectionMaximumAttempts(void);

protected:
  // specify request url for kartina protocol
  TCHAR *requestUrl;
};

#endif
