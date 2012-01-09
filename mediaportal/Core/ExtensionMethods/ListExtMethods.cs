#region Copyright (C) 2005-2011 Team MediaPortal

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

#endregion

using System;
using System.Collections;
using System.Collections.Generic;
using MediaPortal.GUI.Library;
using System.Linq;
using System.Threading;

namespace MediaPortal.ExtensionMethods
{
  public static class ListExtMethods
  {
    public static void Dispose(this IList listInterface)
    {
      if (listInterface != null)
      {
        //listInterface.DisposeChildren();
        listInterface.OfType<IDisposable>().DisposeChildren();
      }
    }

    public static void DisposeAndClear(this IList listInterface)
    {
      if (listInterface != null)
      {
        listInterface.Dispose();
        listInterface.Clear();
      }
    }

    public static void DisposeAndClearCollection<T>(this ICollection<T> listInterface)
    {
      if (listInterface != null)
      {
        //((ICollection)listInterface).DisposeChildren();
        listInterface.DisposeChildren();
        listInterface.Clear();
      }
    }

    public static void DisposeAndClearList(this IList listInterface)
    {
      listInterface.DisposeAndClear();
    }

    static void DisposeChildren<T>(this IEnumerable<T> collection)
    {
      if (collection == null) return;
      IDisposable[] shallowCopy;
      try
      {
        lock (collection)
          shallowCopy = collection.OfType<IDisposable>().ToArray();
      }
      catch (InvalidOperationException ex)
      {
        Log.Error("List disposing failed: " + ex.ToString());
        return;
      }
      for (int i = 0; i < shallowCopy.Length; i++)
      {
        var o = shallowCopy[i];
        if (o != null)
        {
          o.Dispose();
        }
      }
    }
  }
}