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

#include "StdAfx.h"

#include "ParameterCollection.h"

CParameterCollection::CParameterCollection(void)
{
  this->parameterCount = 0;
  this->parameterMaximumCount = 16;
  //this->parameters = (PCParameter *)CoTaskMemAlloc(this->parameterMaximumCount * sizeof(PCParameter));
  this->parameters = ALLOC_MEM_SET(this->parameters, PCParameter, this->parameterMaximumCount, 0);
}

CParameterCollection::~CParameterCollection(void)
{
  this->Clear();

  /*if (this->parameters != NULL)
  {
    CoTaskMemFree(this->parameters);
  }
  this->parameters = NULL;*/
  FREE_MEM(this->parameters);
}

void CParameterCollection::Clear(void)
{
  // call destructors of all parameters
  for(unsigned int i = 0; i < this->parameterCount; i++)
  {
    delete (*(this->parameters + i));
  }

  // set used parameters to 0
  this->parameterCount = 0;
}

bool CParameterCollection::Add(PCParameter parameter)
{
  if (this->parameterCount >= this->parameterMaximumCount)
  {
    // there is need to enlarge array of parameters
    // double number of allowed parameters
    this->parameterMaximumCount *= 2;
    //PCParameter *parameterArray = (PCParameter *)CoTaskMemRealloc(this->parameters, this->parameterMaximumCount * sizeof(PCParameter));
    PCParameter *parameterArray = REALLOC_MEM(this->parameters, PCParameter, this->parameterMaximumCount);

    if (parameterArray == NULL)
    {
      return FALSE;
    }

    this->parameters = parameterArray;
  }

  *(this->parameters + this->parameterCount++) = parameter;
  return TRUE;
}

void CParameterCollection::Append(CParameterCollection *collection)
{
  if (collection != NULL)
  {
    unsigned int count = collection->Count();
    for (unsigned int i = 0; i < count; i++)
    {
      this->Add(collection->GetParameter(i)->Clone());
    }
  }
}

bool CParameterCollection::Contains(const TCHAR *name, bool invariant)
{
  return (this->GetParameter(name, invariant) != NULL);
}

PCParameter CParameterCollection::GetParameter(unsigned int index)
{
  PCParameter result = NULL;
  if (index <= this->parameterCount)
  {
    result = *(this->parameters + index);
  }
  return result;
}

PCParameter CParameterCollection::GetParameter(const TCHAR *name, bool invariant)
{
  PCParameter result = NULL;
  for(unsigned int i = 0; i < this->parameterCount; i++)
  {
    PCParameter parameter = *(this->parameters + i);

    if (_tcslen(name) == parameter->GetNameLength())
    {
      if (invariant)
      {
        if (_tcsicmp(name, parameter->GetName()) == 0)
        {
          // same names
          result = parameter;
          break;
        }
      }
      else
      {
        if (_tcscmp(name, parameter->GetName()) == 0)
        {
          // same names
          result = parameter;
          break;
        }
      }
    }
  }
  return result;
}

unsigned int CParameterCollection::Count(void)
{
  return this->parameterCount;
}

void CParameterCollection::LogCollection(CLogger *logger, unsigned int loggerLevel, const TCHAR *protocolName, const TCHAR *functionName)
{
  unsigned int count = this->Count();
  if (protocolName == NULL)
  {
    logger->Log(loggerLevel, _T("%s: configuration parameters: %u"), functionName, count);
  }
  else
  {
    logger->Log(loggerLevel, _T("%s: %s: configuration parameters: %u"), protocolName, functionName, count);
  }
  for (unsigned int i = 0; i < count; i++)
  {
    PCParameter parameter = this->GetParameter(i);
    if (protocolName == NULL)
    {
      logger->Log(loggerLevel, _T("%s: parameter %u, name: '%s', value: '%s'"), functionName, i + 1, parameter->GetName(), parameter->GetValue());
    }
    else
    {
      logger->Log(loggerLevel, _T("%s: %s: parameter %u, name: '%s', value: '%s'"), protocolName, functionName, i + 1, parameter->GetName(), parameter->GetValue());
    }
  }
}

TCHAR *CParameterCollection::GetValue(const TCHAR *name, bool invariant, TCHAR *defaultValue)
{
  PCParameter parameter = this->GetParameter(name, invariant);
  if (parameter != NULL)
  {
    return parameter->GetValue();
  }
  else
  {
    return defaultValue;
  }
}

long CParameterCollection::GetValueLong(const TCHAR *name, bool invariant, long defaultValue)
{
  TCHAR *value = this->GetValue(name, invariant, _T(""));
  TCHAR *end = NULL;
  long valueLong = _tcstol(value, &end, 10);
  if ((valueLong == 0) && (value == end))
  {
    // error while converting
    valueLong = defaultValue;
  }

  return valueLong;
}

bool CParameterCollection::GetValueBool(const TCHAR *name, bool invariant, bool defaultValue)
{
  switch (this->GetValueLong(name, invariant, -1))
  {
  case 0:
    return false;
  case 1:
    return true;
  default:
    return defaultValue;
  }
}
