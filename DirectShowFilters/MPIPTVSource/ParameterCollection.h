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

#ifndef __PARAMETERCOLLECTION_DEFINED
#define __PARAMETERCOLLECTION_DEFINED

#define MAX_LOG_SIZE_DEFAULT                                        10485760
#define LOG_VERBOSITY_DEFAULT                                       LOGGER_INFO
// 200 miliseconds after successfull end of Run() method will be sent first data
// because of wait for conditional access module to prepare
#define CONDITIONAL_ACCESS_WAITING_TIMEOUT_DEFAULT                  200
// maximum count of plugins
#define MAX_PLUGINS_DEFAULT                                         256
// default count of IPTV buffers
#define IPTV_BUFFER_COUNT_DEFAULT                                   16
// maximum size of DirectX sample (maximum size of data block sent to MediaPortal)
#define IPTV_BUFFER_SIZE_DEFAULT                                    32768
#define DUMP_RAW_TS_DEFAULT                                         0
#define ANALYZE_DISCONTINUITY_DEFAULT                               1

#define CONFIGURATION_SECTION_MPIPTVSOURCE                          _T("MPIPTVSource")

#define CONFIGURATION_MAX_LOG_SIZE                                  _T("MaxLogSize")
#define CONFIGURATION_LOG_VERBOSITY                                 _T("LogVerbosity")
#define CONFIGURATION_CONDITIONAL_ACCESS_WAITING_TIMEOUT            _T("ConditionalAccessWaitingTimeout")
#define CONFIGURATION_MAX_PLUGINS                                   _T("MaxPlugins")
#define CONFIGURATION_IPTV_BUFFER_COUNT                             _T("IptvBufferCount")
#define CONFIGURATION_IPTV_BUFFER_SIZE                              _T("IptvBufferSize")
#define CONFIGURATION_DUMP_RAW_TS                                   _T("DumpRawTS")
#define CONFIGURATION_ANALYZE_DISCONTINUITY                         _T("AnalyzeDiscontinuity")

#define INTERFACE_PARAMETER_NAME                                    _T("interface")

// this is temporary parameter for url
// will be removed if normal way of specifying network interface be implemented
#define URL_PARAMETER_NAME                                          _T("url")

#include "MPIPTVSourceExports.h"
#include "Parameter.h"
#include "Logger.h"

class MPIPTVSOURCE_API CParameterCollection
{
public:
  CParameterCollection(void);
  ~CParameterCollection(void);

  // add parameter to collection
  // @param parameter : the reference to parameter to add
  // @return : true if successful, false otherwise
  bool Add(PCParameter parameter);

  // append parameter collection
  // @param collection : the reference to collection to add
  void Append(CParameterCollection *collection);

  // clear collection of parameters
  void Clear(void);

  // test if parameter exists in collection
  // @param name : the name of parameter to find
  // @param invariant : specifies if parameter name shoud be find with invariant casing
  // @return : true if parameter exists, false otherwise
  bool Contains(const TCHAR *name, bool invariant);

  // get the parameter from collection with specified index
  // @param index : the index of parameter to find
  // @return : the reference to parameter or NULL if not find
  PCParameter GetParameter(unsigned int index);

  // get the parameter from collection with specified name
  // @param name : the name of parameter to find
  // @param invariant : specifies if parameter name shoud be find with invariant casing
  // @return : the reference to parameter or NULL if not find
  PCParameter GetParameter(const TCHAR *ame, bool invariant);

  // get count of parameters in collection
  // @return : count of parameters in collection
  unsigned int Count(void);

  // log all parameters to log file
  // @param logger : the logger
  // @param loggerLevel : the logger level of messages
  // @param protocolName : name of protocol calling LogCollection()
  // @param functionName : name of function calling LogCollection()
  void LogCollection(CLogger *logger, unsigned int loggerLevel, const TCHAR *protocolName, const TCHAR *functionName);

  // get the string value of parameter with specified name
  // @param name : the name of parameter to find
  // @param invariant : specifies if parameter name shoud be find with invariant casing
  // @param defaultValue : the default value to return
  // @return : the value of parameter or default value if not found
  TCHAR *GetValue(const TCHAR *name, bool invariant, TCHAR *defaultValue);

  // get the integer value of parameter with specified name
  // @param name : the name of parameter to find
  // @param invariant : specifies if parameter name shoud be find with invariant casing
  // @param defaultValue : the default value to return
  // @return : the value of parameter or default value if not found
  long GetValueLong(const TCHAR *name, bool invariant, long defaultValue);

  // get the boolean value of parameter with specified name
  // @param name : the name of parameter to find
  // @param invariant : specifies if parameter name shoud be find with invariant casing
  // @param defaultValue : the default value to return
  // @return : the value of parameter or default value if not found
  bool GetValueBool(const TCHAR *name, bool invariant, bool defaultValue);
protected:
  // count of parameters in collection
  unsigned int parameterCount;

  // maximum count of parameters to store in collection
  unsigned int parameterMaximumCount;
private:
  // pointer to array of pointers to parameters
  PCParameter *parameters;
};

#endif
