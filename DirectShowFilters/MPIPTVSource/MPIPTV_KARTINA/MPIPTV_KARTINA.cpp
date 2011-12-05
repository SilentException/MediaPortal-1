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

// MPIPTV_KARTINA.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"

#include "MPIPTV_KARTINA.h"
#include "Network.h"
#include "Utilities.h"

#include <stdio.h>

// protocol implementation name
#define PROTOCOL_IMPLEMENTATION_NAME                                    _T("CMPIPTV_KARTINA")

PIProtocol CreateProtocolInstance(void)
{
  return new CMPIPTV_KARTINA;
}

void DestroyProtocolInstance(PIProtocol pProtocol)
{
  if (pProtocol != NULL)
  {
    CMPIPTV_KARTINA *pClass = (CMPIPTV_KARTINA *)pProtocol;
    delete pClass;
  }
}

CMPIPTV_KARTINA::CMPIPTV_KARTINA()
{
  this->logger.Log(LOGGER_INFO, METHOD_START_FORMAT, PROTOCOL_IMPLEMENTATION_NAME, METHOD_CONSTRUCTOR_NAME);

  this->requestUrl = NULL;
  this->openConnetionMaximumAttempts = KARTINA_OPEN_CONNECTION_MAXIMUM_ATTEMPTS_DEFAULT;

  this->logger.Log(LOGGER_INFO, METHOD_END_FORMAT, PROTOCOL_IMPLEMENTATION_NAME, METHOD_CONSTRUCTOR_NAME);
}

CMPIPTV_KARTINA::~CMPIPTV_KARTINA()
{
  this->logger.Log(LOGGER_INFO, METHOD_START_FORMAT, PROTOCOL_IMPLEMENTATION_NAME, METHOD_DESTRUCTOR_NAME);

  if (this->requestUrl != NULL)
  {
    CoTaskMemFree(this->requestUrl);
    this->requestUrl = NULL;
  }

  this->logger.Log(LOGGER_INFO, METHOD_END_FORMAT, PROTOCOL_IMPLEMENTATION_NAME, METHOD_DESTRUCTOR_NAME);
}

int CMPIPTV_KARTINA::Initialize(HANDLE lockMutex, CParameterCollection *configuration)
{
  if (configuration != NULL)
  {
    CParameterCollection *httpParameters = GetConfiguration(&this->logger, PROTOCOL_IMPLEMENTATION_NAME, METHOD_INITIALIZE_NAME, CONFIGURATION_SECTION_HTTP);
    configuration->Append(httpParameters);
    delete httpParameters;
  }

  int result = this->CMPIPTV_HTTP::Initialize(lockMutex, configuration);

  this->receiveDataTimeout = this->configurationParameters->GetValueLong(CONFIGURATION_KARTINA_RECEIVE_DATA_TIMEOUT, true, KARTINA_RECEIVE_DATA_TIMEOUT_DEFAULT);
  this->openConnetionMaximumAttempts = this->configurationParameters->GetValueLong(CONFIGURATION_KARTINA_OPEN_CONNECTION_MAXIMUM_ATTEMPTS, true, KARTINA_OPEN_CONNECTION_MAXIMUM_ATTEMPTS_DEFAULT);

  this->receiveDataTimeout = (this->receiveDataTimeout < 0) ? KARTINA_RECEIVE_DATA_TIMEOUT_DEFAULT : this->receiveDataTimeout;
  this->openConnetionMaximumAttempts = (this->openConnetionMaximumAttempts < 0) ? KARTINA_OPEN_CONNECTION_MAXIMUM_ATTEMPTS_DEFAULT : this->openConnetionMaximumAttempts;

  return STATUS_OK;
}

TCHAR *CMPIPTV_KARTINA::GetProtocolName(void)
{
  return Duplicate(CONFIGURATION_SECTION_KARTINA);
}

int CMPIPTV_KARTINA::ClearSession(void)
{
  this->logger.Log(LOGGER_INFO, METHOD_START_FORMAT, PROTOCOL_IMPLEMENTATION_NAME, METHOD_CLEAR_SESSION_NAME);

  if (this->IsConnected())
  {
    this->CloseConnection();
  }

  if (this->requestUrl != NULL)
  {
    CoTaskMemFree(this->requestUrl);
  }
  this->requestUrl = NULL;
  this->CMPIPTV_HTTP::ClearSession();
  this->logger.Log(LOGGER_INFO, METHOD_END_FORMAT, PROTOCOL_IMPLEMENTATION_NAME, METHOD_CLEAR_SESSION_NAME);

  return STATUS_OK;
}

int CMPIPTV_KARTINA::ParseUrl(const TCHAR *url, const CParameterCollection *parameters)
{
  int result = STATUS_OK;
  this->logger.Log(LOGGER_INFO, METHOD_START_FORMAT, PROTOCOL_IMPLEMENTATION_NAME, METHOD_PARSE_URL_NAME);

  this->ClearSession();
  this->loadParameters->Append((CParameterCollection *)parameters);
  this->loadParameters->LogCollection(&this->logger, LOGGER_VERBOSE, PROTOCOL_IMPLEMENTATION_NAME, METHOD_PARSE_URL_NAME);

  ALLOC_MEM_DEFINE_SET(urlComponents, URL_COMPONENTS, 1, 0);
  if (urlComponents == NULL)
  {
    this->logger.Log(LOGGER_ERROR, METHOD_MESSAGE_FORMAT, PROTOCOL_IMPLEMENTATION_NAME, METHOD_PARSE_URL_NAME, _T("cannot allocate memory for 'url components'"));
    result = STATUS_ERROR;
  }

  if (result == STATUS_OK)
  {
    ZeroURL(urlComponents);
    urlComponents->dwStructSize = sizeof(URL_COMPONENTS);

    this->logger.Log(LOGGER_INFO, _T("%s: %s: url: %s"), PROTOCOL_IMPLEMENTATION_NAME, METHOD_PARSE_URL_NAME, url);

    if (!InternetCrackUrl(url, 0, 0, urlComponents))
    {
      this->logger.Log(LOGGER_ERROR, _T("%s: %s: InternetCrackUrl() error: %u"), PROTOCOL_IMPLEMENTATION_NAME, METHOD_PARSE_URL_NAME, GetLastError());
      result = STATUS_ERROR;
    }
  }

  if (result == STATUS_OK)
  {
    int length = urlComponents->dwSchemeLength + 1;
    ALLOC_MEM_DEFINE_SET(protocol, TCHAR, length, 0);
    if (protocol == NULL) 
    {
      this->logger.Log(LOGGER_ERROR, METHOD_MESSAGE_FORMAT, PROTOCOL_IMPLEMENTATION_NAME, METHOD_PARSE_URL_NAME, _T("cannot allocate memory for 'protocol'"));
      result = STATUS_ERROR;
    }

    if (result == STATUS_OK)
    {
      _tcsncat_s(protocol, length, urlComponents->lpszScheme, urlComponents->dwSchemeLength);

      if (_tcsncicmp(urlComponents->lpszScheme, _T("KARTINA"), urlComponents->dwSchemeLength) != 0)
      {
        // not supported protocol
        this->logger.Log(LOGGER_INFO, _T("%s: %s: unsupported protocol '%s'"), PROTOCOL_IMPLEMENTATION_NAME, METHOD_PARSE_URL_NAME, protocol);
        result = STATUS_ERROR;
      }
    }
    FREE_MEM(protocol);
    if (result == STATUS_OK)
    {
      length = _tcslen(url) - 2; // needed: length of url + 1 (terminating null character) - 7 (kartina) + 4 (http) = length of url - 2
      this->requestUrl = ALLOC_MEM_SET(this->requestUrl, TCHAR, length, 0);
      if (this->requestUrl == NULL) 
      {
        this->logger.Log(LOGGER_ERROR, METHOD_MESSAGE_FORMAT, PROTOCOL_IMPLEMENTATION_NAME, METHOD_PARSE_URL_NAME, _T("cannot allocate memory for 'request url'"));
        result = STATUS_ERROR;
      }
    }

    if (result == STATUS_OK)
    {
      _tcsncat_s(this->requestUrl, length, _T("http"), 4);
      _tcsncat_s(this->requestUrl, length, url + 7, _tcslen(url) - 7);

      this->logger.Log(LOGGER_INFO, _T("%s: %s: request url: %s"), PROTOCOL_IMPLEMENTATION_NAME, METHOD_PARSE_URL_NAME, this->requestUrl);
    }
  }

  FREE_MEM(urlComponents);
  this->logger.Log(LOGGER_INFO, (result == STATUS_OK) ? METHOD_END_FORMAT : METHOD_END_FAIL_FORMAT, PROTOCOL_IMPLEMENTATION_NAME, METHOD_PARSE_URL_NAME);

  return result;
}

int CMPIPTV_KARTINA::OpenConnection(void)
{
  this->logger.Log(LOGGER_INFO, METHOD_START_FORMAT, PROTOCOL_IMPLEMENTATION_NAME, METHOD_OPEN_CONNECTION_NAME);

  CParameterCollection *parameters = new CParameterCollection();
  parameters->Append(this->loadParameters);

  // we must create configuration parameters for HTTP protocol
  CParameterCollection *httpParameters = GetConfiguration(&this->logger, PROTOCOL_IMPLEMENTATION_NAME, METHOD_OPEN_CONNECTION_NAME, CONFIGURATION_SECTION_MPIPTVSOURCE);
  httpParameters->Append(GetConfiguration(&this->logger, PROTOCOL_IMPLEMENTATION_NAME, METHOD_OPEN_CONNECTION_NAME, CONFIGURATION_SECTION_HTTP));

  CMPIPTV_HTTP http;
  http.Initialize(this->lockMutex, httpParameters);
  int retval = http.ParseUrl(this->requestUrl, parameters);

  if (retval == STATUS_OK)
  {
    // url successfully parsed
    retval = http.OpenConnection();
    if (retval == STATUS_OK)
    {
      // connection succesfully opened
      // now wait for data
      unsigned int occupiedSpace = 0;
      bool shouldExit = FALSE;
      do
      {
        http.ReceiveData(&shouldExit);
        http.GetSafeBufferSizes(this->lockMutex, NULL, &occupiedSpace, NULL);
        Sleep(1);
      } while (occupiedSpace == 0);

      // data received, in buffer we get url with ticket
      // copy data from buffer
      ALLOC_MEM_DEFINE_SET(receivedUrl, char, (occupiedSpace + 1), 0);
      retval = (receivedUrl == NULL) ? STATUS_ERROR : STATUS_OK;
      if (retval == STATUS_OK)
      {
        http.GetBuffer()->CopyFromBuffer(receivedUrl, occupiedSpace, 0, 0);

        // allocate memory for url with ticket
        TCHAR *newUrl = NULL;
#ifdef _MBCS
        newUrl = ConvertToMultiByteA(receivedUrl);
#else
        newUrl = ConvertToUnicodeA(receivedUrl);
#endif
        retval = (newUrl == NULL) ? STATUS_ERROR : STATUS_OK;

        if (retval == STATUS_OK)
        {
          // if successfully converted to Unicode (if needed), try parse url
          this->logger.Log(LOGGER_INFO, _T("%s: %s: new url: %s"), PROTOCOL_IMPLEMENTATION_NAME, METHOD_OPEN_CONNECTION_NAME, newUrl);
          // ParseUrl() calls ClearSession() which set this->requestUrl to NULL !
          TCHAR *tempRequestUrl = Duplicate(this->requestUrl);
          retval = (tempRequestUrl == NULL) ? STATUS_ERROR : STATUS_OK;

          if (retval == STATUS_OK)
          {
            retval = this->CMPIPTV_HTTP::ParseUrl(newUrl, parameters);

            if (retval != STATUS_OK)
            {
              // error occured while parsing url
              // set this->requestUrl to its previous value
              this->requestUrl = Duplicate(tempRequestUrl);
            }

            CoTaskMemFree(tempRequestUrl);
            tempRequestUrl = NULL;
          }
          else
          {
            this->logger.Log(LOGGER_ERROR, METHOD_MESSAGE_FORMAT, PROTOCOL_IMPLEMENTATION_NAME, METHOD_OPEN_CONNECTION_NAME, _T("cannot duplicate request url"));
          }
        }

        if (retval == STATUS_OK)
        {
          // if successfully parsed new url, try open connection
          retval = this->CMPIPTV_HTTP::OpenConnection();
        }

        // release newUrl
        if (newUrl != NULL)
        {
          CoTaskMemFree(newUrl);
          newUrl = NULL;
        }

        CoTaskMemFree(receivedUrl);
        receivedUrl = NULL;
      }
    }
  }

  delete httpParameters;
  delete parameters;

  this->logger.Log(LOGGER_INFO, (retval == STATUS_OK) ? METHOD_END_FORMAT : METHOD_END_FAIL_FORMAT, PROTOCOL_IMPLEMENTATION_NAME, METHOD_OPEN_CONNECTION_NAME);
  return retval;
}

int CMPIPTV_KARTINA::IsConnected(void)
{
  return this->CMPIPTV_HTTP::IsConnected();
}

void CMPIPTV_KARTINA::CloseConnection(void)
{
  this->logger.Log(LOGGER_INFO, METHOD_START_FORMAT, PROTOCOL_IMPLEMENTATION_NAME, METHOD_CLOSE_CONNECTION_NAME);
  this->CMPIPTV_HTTP::CloseConnection();
  this->logger.Log(LOGGER_INFO, METHOD_END_FORMAT, PROTOCOL_IMPLEMENTATION_NAME, METHOD_CLOSE_CONNECTION_NAME);
}

void CMPIPTV_KARTINA::GetSafeBufferSizes(HANDLE lockMutex, unsigned int *freeSpace, unsigned int *occupiedSpace, unsigned int *bufferSize)
{
  this->CMPIPTV_HTTP::GetSafeBufferSizes(lockMutex, freeSpace, occupiedSpace, bufferSize);
}

void CMPIPTV_KARTINA::ReceiveData(bool *shouldExit)
{
  this->CMPIPTV_HTTP::ReceiveData(shouldExit);
}

unsigned int CMPIPTV_KARTINA::FillBuffer(IMediaSample *pSamp, char *pData, long cbData)
{
  return this->CMPIPTV_HTTP::FillBuffer(pSamp, pData, cbData);
}

unsigned  int CMPIPTV_KARTINA::GetReceiveDataTimeout(void)
{
  return this->receiveDataTimeout;
}

unsigned int CMPIPTV_KARTINA::GetOpenConnectionMaximumAttempts(void)
{
  return this->openConnetionMaximumAttempts;
}
