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
using System.Drawing;
using System.Drawing.Imaging;
using System.Globalization;
using System.IO;
using System.Threading;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using MediaPortal.Configuration;
using MediaPortal.ExtensionMethods;
using MediaPortal.ServiceImplementations;
using MediaPortal.Profile;
using MediaPortal.Services;

namespace MediaPortal.Util
{
  public class VideoThumbCreator
  {
    private static string Extract1App = "mtn.exe";
    private static string Extract2App = "ffmpeg.exe";
    private static string Extractor1Path = Config.GetFile(Config.Dir.Base, "MovieThumbnailer", Extract1App);
    private static string Extractor2Path = Config.GetFile(Config.Dir.Base, "FFmpeg", Extract2App);
    private static int PreviewColumns = 1;
    private static int PreviewRows = 1;
    private static bool LeaveShareThumb = false;
    private static bool NeedsConfigRefresh = true;

    #region Serialisation

    private static void LoadSettings()
    {
      using (Settings xmlreader = new MPSettings())
      {
        PreviewColumns = xmlreader.GetValueAsInt("thumbnails", "tvthumbcols", 1);
        PreviewRows = xmlreader.GetValueAsInt("thumbnails", "tvthumbrows", 1);
        LeaveShareThumb = xmlreader.GetValueAsBool("thumbnails", "tvrecordedsharepreview", false);
        Log.Debug("VideoThumbCreator: Settings loaded - using {0} columns and {1} rows. Share thumb = {2}",
                  PreviewColumns, PreviewRows, LeaveShareThumb);
        NeedsConfigRefresh = false;
      }
    }

    #endregion

    #region Public methods

    //[MethodImpl(MethodImplOptions.Synchronized)]
    //public static bool CreateVideoThumb(string aVideoPath, bool aOmitCredits)
    //{
    //  string sharethumb = Path.ChangeExtension(aVideoPath, ".jpg");
    //  if (Util.Utils.FileExistsInCache(sharethumb))
    //    return true;
    //  else
    //    return CreateVideoThumb(aVideoPath, sharethumb, false, aOmitCredits);
    //}

    [MethodImpl(MethodImplOptions.Synchronized)]
    public static bool CreateVideoThumb(string aVideoPath, string aThumbPath, bool aCacheThumb, bool aOmitCredits)
    {
      if (NeedsConfigRefresh)
      {
        LoadSettings();
      }

      if (String.IsNullOrEmpty(aVideoPath) || String.IsNullOrEmpty(aThumbPath))
      {
        Log.Warn("VideoThumbCreator: Invalid arguments to generate thumbnails of your video!");
        return false;
      }
      if (!Util.Utils.FileExistsInCache(aVideoPath))
      {
        Log.Warn("VideoThumbCreator: File {0} not found!", aVideoPath);
        return false;
      }
      if (!Util.Utils.FileExistsInCache(Extractor1Path) && !Util.Utils.FileExistsInCache(Extractor2Path))
      {
        Log.Warn("VideoThumbCreator: No {0} or {1} found to generate thumbnails of your video!", Extract1App, Extract2App);
        return false;
      }
      if (!LeaveShareThumb && !aCacheThumb)
      {
        Log.Warn(
          "VideoThumbCreator: No share thumbs wanted by config option AND no caching wanted - where should the thumb go then? Aborting..");
        return false;
      }

      IVideoThumbBlacklist blacklist = GlobalServiceProvider.Get<IVideoThumbBlacklist>();
      if (blacklist != null && blacklist.Contains(aVideoPath))
      {
        Log.Debug("Skipped creating thumbnail for {0}, it has been blacklisted because last attempt failed", aVideoPath);
        return false;
      }

      // Params for ffmpeg
      // string ExtractorArgs = string.Format(" -i \"{0}\" -vframes 1 -ss {1} -s {2}x{3} \"{4}\"", aVideoPath, @"00:08:21", (int)Thumbs.ThumbLargeResolution, (int)Thumbs.ThumbLargeResolution, aThumbPath);

      // Params for mplayer (outputs 00000001.jpg in video resolution into working dir) -vf scale=600:-3
      //string ExtractorArgs = string.Format(" -noconsolecontrols -nosound -vo jpeg:quality=90 -vf scale -frames 1 -ss {0} \"{1}\"", "501", aVideoPath);

      // Params for mtn (http://moviethumbnail.sourceforge.net/usage.en.html)
      //   -D 8         : edge detection; 0:off >0:on; higher detects more; try -D4 -D6 or -D8
      //   -B 420/E 600 : omit this seconds from the beginning / ending TODO: use pre- / postrecording values
      //   -c 2 / r 2   : # of column / # of rows
      //   -b 0.60      : skip if % blank is higher; 0:skip all 1:skip really blank >1:off
      //   -h 100       : minimum height of each shot; will reduce # of column to fit
      //   -t           : time stamp off
      //   -i           : info text off
      //   -w 0         : width of output image; 0:column * movie width
      //   -n           : run at normal priority
      //   -W           : dont overwrite existing files, i.e. update mode
      //   -P           : dont pause before exiting; override -p

      const double flblank = 0.6;
      string blank = flblank.ToString("F", CultureInfo.CurrentCulture);

      int preGapSec = 5;
      int postGapSec = 5;
      if (aOmitCredits)
      {
        preGapSec = 420;
        postGapSec = 600;
      }
      bool Success = false;

      try
      {
        // Use this for the working dir to be on the safe side
        string TempPath = Path.GetTempPath();
        //string OutputThumb = string.Format("{0}_s{1}", Path.ChangeExtension(aVideoPath, null), ".jpg");
        //string ShareThumb = OutputThumb.Replace("_s.jpg", ".jpg");
        string ShareThumb = string.Format("{0}{1}", Path.ChangeExtension(aVideoPath, null), ".jpg");

        // EXTRACTOR 1 - MTN
        string Extractor1Args = string.Format(" -o .jpg -D 6 -B {0} -E {1} -c {2} -r {3} -b {4} -t -i -w {5} -n -P \"{6}\"",
                                              preGapSec, postGapSec, PreviewColumns, PreviewRows, blank, 0, aVideoPath);
        string Extractor1FallbackArgs = string.Format(" -o .jpg -D 8 -B {0} -E {1} -c {2} -r {3} -b {4} -t -i -w {5} -n -P \"{6}\"",
                                                      0, 0, PreviewColumns, PreviewRows, blank, 0, aVideoPath);
        // Honour we are using a unix app
        Extractor1Args = Extractor1Args.Replace('\\', '/');
        Extractor1FallbackArgs = Extractor1FallbackArgs.Replace('\\', '/');

        // EXTRACTOR 2 - FFmpeg
        string Extractor2Args = string.Format(" -ss {0} -i \"{1}\" -vframes 1 \"{2}\"", preGapSec, aVideoPath, ShareThumb);
        string Extractor2FallbackArgs = string.Format(" -ss 1 -i \"{1}\" -vframes 1 \"{2}\"", preGapSec, aVideoPath, ShareThumb);

        if ((LeaveShareThumb && !Util.Utils.FileExistsInCache(ShareThumb))
            // No thumb in share although it should be there
            || (!LeaveShareThumb && !Util.Utils.FileExistsInCache(aThumbPath)))
            // No thumb cached and no chance to find it in share
        {
          //Log.Debug("VideoThumbCreator: No thumb in share {0} - trying to create one with arguments: {1}", ShareThumb, ExtractorArgs);
          Success = Utils.StartProcess(Extractor1Path, Extractor1Args, TempPath, 15000, true, GetMtnConditions());
          if (!Success)
          {
            // Maybe the pre-gap was too large or not enough sharp & light scenes could be caught
            Thread.Sleep(50);
            Success = Utils.StartProcess(Extractor1Path, Extractor1FallbackArgs, TempPath, 30000, true, GetMtnConditions());
            /*
            if (!Success)
              Log.Info("VideoThumbCreator: {0} has not been executed successfully with arguments: {1}", ExtractApp,
                       ExtractorFallbackArgs);
            */
          }
          // give the system a few IO cycles
          Thread.Sleep(50);
          // make sure there's no process hanging
          Utils.KillProcess(Path.ChangeExtension(Extract1App, null));
          /*
          try
          {
            // remove the _s which mtn appends to its files
            File.Move(OutputThumb, ShareThumb);
          }
          catch (FileNotFoundException)
          {
            Log.Debug("VideoThumbCreator: {0} did not extract a thumbnail to: {1}", ExtractApp, OutputThumb);
          }
          catch (Exception)
          {
            try
            {
              // Clean up
              File.Delete(OutputThumb);
              Thread.Sleep(50);
            }
            catch (Exception) {}
          }
          */
          bool shareThumbExists = Util.Utils.FileExistsInCache(ShareThumb);
          if (!shareThumbExists || (shareThumbExists && new FileInfo(ShareThumb).Length == 0))
          {
            Log.Debug("VideoThumbCreator: {0} did not extract a thumbnail to: {1}", Extract1App, ShareThumb);
            if (shareThumbExists)
            {
              try
              {
                File.Delete(ShareThumb);
              }
              catch { }
            }
            Success = false;
          }

          if (!Success)
          {
            Success = Utils.StartProcess(Extractor2Path, Extractor2Args, TempPath, 15000, true, GetMtnConditions());
            if (!Success)
            {
              // Maybe it takes more than 15 seconds to create the thumbnail
              // Try fallback arguments
              Thread.Sleep(50);
              Success = Utils.StartProcess(Extractor2Path, Extractor2FallbackArgs, TempPath, 30000, true, GetMtnConditions());
              /*
              if (!Success)
                Log.Info("VideoThumbCreator: {0} has not been executed successfully with arguments: {1}", ExtractApp,
                         ExtractorFallbackArgs);
              */
            }
            // give the system a few IO cycles
            Thread.Sleep(50);
            // make sure there's no process hanging
            Utils.KillProcess(Path.ChangeExtension(Extract2App, null));

            shareThumbExists = Util.Utils.FileExistsInCache(ShareThumb);
            if ((!shareThumbExists || (shareThumbExists && new FileInfo(ShareThumb).Length == 0)))
            {
              Log.Debug("VideoThumbCreator: {0} did not extract a thumbnail to: {1}", Extract2App, ShareThumb);
              if (shareThumbExists)
              {
                try
                {
                  File.Delete(ShareThumb);
                }
                catch { }
              }
              Success = false;
            }
          }
  
          if (!Success)
          {
            Image thumb = null;
            try
            {
              if (OSInfo.OSInfo.VistaOrLater())
              {
                thumb = VistaToolbelt.Shell.ThumbnailGenerator.GenerateThumbnail(aVideoPath);
              }
              else
              {
                using (ThumbnailExtractor extractor = new ThumbnailExtractor())
                {
                  thumb = extractor.GetThumbnail(aVideoPath);
                }
              }
              if (thumb != null)
              {
                thumb.Save(ShareThumb);
                Success = true;
              }
            }
            catch (COMException comex)
            {
              Success = false;
              if (comex.ErrorCode == unchecked((int)0x8004B200))
              {
                Log.Warn("VideoThumbCreator: Windows did not extract a thumbnail to: {0} - [Unknown error 0x8004B200]", ShareThumb);
              }
              else
              {
                Log.Error("VideoThumbCreator: Windows did not extract a thumbnail to: {0}", ShareThumb);
                Log.Error(comex);
              }
            }
            catch (Exception ex)
            {
              Success = false;
              Log.Error("VideoThumbCreator: Windows did not extract a thumbnail to: {0}", ShareThumb);
              Log.Error(ex);
            }
            finally
            {
              if (thumb != null)
                thumb.SafeDispose();
            }
            // give the system a few IO cycles
            Thread.Sleep(50);

            shareThumbExists = Util.Utils.FileExistsInCache(ShareThumb);
            if (Success && (!shareThumbExists || (shareThumbExists && new FileInfo(ShareThumb).Length == 0)))
            {
              Log.Debug("VideoThumbCreator: Windows did not extract a thumbnail to: {0}", ShareThumb);
              if (shareThumbExists)
              {
                try
                {
                  File.Delete(ShareThumb);
                }
                catch { }
              }
              Success = false;
            }
          }
        }
        else
        {
          // We have a thumbnail in share but the cache was wiped out - make sure it is recreated
          if (LeaveShareThumb && Util.Utils.FileExistsInCache(ShareThumb) && !Util.Utils.FileExistsInCache(aThumbPath))
            Success = true;
        }

        //Thread.Sleep(30);

        if (aCacheThumb && Success)
        {
          if (Picture.CreateThumbnail(ShareThumb, aThumbPath, (int)Thumbs.ThumbResolution, (int)Thumbs.ThumbResolution,
                                      0, false))
            Picture.CreateThumbnail(ShareThumb, Utils.ConvertToLargeCoverArt(aThumbPath),
                                    (int)Thumbs.ThumbLargeResolution, (int)Thumbs.ThumbLargeResolution, 0, false);
        }

        if (!LeaveShareThumb)
        {
          try
          {
            File.Delete(ShareThumb);
            Thread.Sleep(30);
          }
          catch (Exception) {}
        }
      }
      catch (Exception ex)
      {
        Log.Error("VideoThumbCreator: Thumbnail generation failed - {0}!", ex.ToString());
      }
      if (Util.Utils.FileExistsInCache(aThumbPath))
      {
        return true;
      }
      else
      {
        if (blacklist != null)
        {
          blacklist.Add(aVideoPath);
        }
        return false;
      }
    }

    public static string GetThumbExtractorVersion(int extractorIndex)
    {
      try
      {
        //System.Diagnostics.FileVersionInfo newVersion = System.Diagnostics.FileVersionInfo.GetVersionInfo(ExtractorPath);
        //return newVersion.FileVersion;
        // mtn.exe has no version info, so let's use "time modified" instead
        string extractor = string.Empty;
        if (extractorIndex == 1)
        {
          extractor = Extractor1Path;
        }
        else if (extractorIndex == 2)
        {
          extractor = Extractor2Path;
        }
        if (string.IsNullOrEmpty(extractor))
        {
          return string.Empty;
        }

        FileInfo fi = new FileInfo(extractor);
        return fi.LastWriteTimeUtc.ToString("s"); // use culture invariant format
      }
      catch (Exception ex)
      {
        Log.Error("GetThumbExtractorVersion failed:");
        Log.Error(ex);
        return string.Empty;
      }
    }

    #endregion

    #region Private methods

    private static Utils.ProcessFailedConditions GetMtnConditions()
    {
      Utils.ProcessFailedConditions mtnStat = new Utils.ProcessFailedConditions();
      // The input file is shorter than pre- and post-recording time
      mtnStat.AddCriticalOutString("net duration after -B & -E is negative");
      mtnStat.AddCriticalOutString("all rows're skipped?");
      mtnStat.AddCriticalOutString("step is zero; movie is too short?");
      mtnStat.AddCriticalOutString("failed: -");
      // unsupported video format by mtn.exe - maybe there's an update?
      mtnStat.AddCriticalOutString("couldn't find a decoder for codec_id");

      mtnStat.SuccessExitCode = 0;

      return mtnStat;
    }

    #endregion
  }
}