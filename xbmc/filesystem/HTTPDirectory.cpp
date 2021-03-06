/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "HTTPDirectory.h"
#include "URL.h"
#include "CurlFile.h"
#include "FileItem.h"
#include "utils/RegExp.h"
#include "settings/AdvancedSettings.h"
#include "utils/StringUtils.h"
#include "utils/CharsetConverter.h"
#include "utils/log.h"
#include "utils/URIUtils.h"
#include "utils/HTMLUtil.h"
#include "climits"

using namespace XFILE;

CHTTPDirectory::CHTTPDirectory(void){}
CHTTPDirectory::~CHTTPDirectory(void){}

bool CHTTPDirectory::GetDirectory(const CStdString& strPath, CFileItemList &items)
{
  CCurlFile http;
  CURL url(strPath);

  CStdString strName, strLink;
  CStdString strBasePath = url.GetFileName();

  if(!http.Open(url))
  {
    CLog::Log(LOGERROR, "%s - Unable to get http directory", __FUNCTION__);
    return false;
  }

  CRegExp reItem(true); // HTML is case-insensitive
  reItem.RegComp("<a href=\"(.*)\">(.*)</a>");

  CRegExp reDateTime(true);
  reDateTime.RegComp("<td align=\"right\">([0-9]{2})-([A-Z]{3})-([0-9]{4}) ([0-9]{2}):([0-9]{2}) +</td>");
  
  CRegExp reDateTimeLighttp(true);
  reDateTimeLighttp.RegComp("<td class=\"m\">([0-9]{4})-([A-Z]{3})-([0-9]{2}) ([0-9]{2}):([0-9]{2}):([0-9]{2})</td>");

  CRegExp reDateTimeNginx(true);
  reDateTimeNginx.RegComp("</a> +([0-9]{2})-([A-Z]{3})-([0-9]{4}) ([0-9]{2}):([0-9]{2}) ");

  CRegExp reSize(true);
  reSize.RegComp("> *([0-9.]+)(B|K|M|G| )</td>");

  CRegExp reSizeNginx(true);
  reSizeNginx.RegComp("([0-9]+)(B|K|M|G)?$");

  /* read response from server into string buffer */
  char buffer[MAX_PATH + 1024];
  while(http.ReadString(buffer, sizeof(buffer)-1))
  {
    CStdString strBuffer = buffer;
    std::string fileCharset(http.GetServerReportedCharset());
    if (!fileCharset.empty() && fileCharset != "UTF-8")
    {
      std::string converted;
      if (g_charsetConverter.ToUtf8(fileCharset, strBuffer, converted) && !converted.empty())
        strBuffer = converted;
    }

    StringUtils::RemoveCRLF(strBuffer);

    if (reItem.RegFind(strBuffer.c_str()) >= 0)
    {
      strLink = reItem.GetMatch(1);
      strName = reItem.GetMatch(2);

      if(strLink[0] == '/')
        strLink = strLink.substr(1);

      CStdString strNameTemp = StringUtils::Trim(strName);

      CStdStringW wName, wLink, wConverted;
      if (fileCharset.empty())
        g_charsetConverter.unknownToUTF8(strNameTemp);
      g_charsetConverter.utf8ToW(strNameTemp, wName, false);
      HTML::CHTMLUtil::ConvertHTMLToW(wName, wConverted);
      g_charsetConverter.wToUTF8(wConverted, strNameTemp);
      URIUtils::RemoveSlashAtEnd(strNameTemp);

      CStdString strLinkBase = strLink;
      CStdString strLinkOptions;

      // split link with url options
      size_t pos = strLinkBase.find('?');
      if (pos != std::string::npos) {
        strLinkOptions = strLinkBase.substr(pos);
        strLinkBase.erase(pos);
      }
      CStdString strLinkTemp = strLinkBase;

      URIUtils::RemoveSlashAtEnd(strLinkTemp);
      CURL::Decode(strLinkTemp);
      if (fileCharset.empty())
        g_charsetConverter.unknownToUTF8(strLinkTemp);
      g_charsetConverter.utf8ToW(strLinkTemp, wLink, false);
      HTML::CHTMLUtil::ConvertHTMLToW(wLink, wConverted);
      g_charsetConverter.wToUTF8(wConverted, strLinkTemp);

      if (StringUtils::EndsWith(strNameTemp, "..>") &&
          StringUtils::StartsWith(strLinkTemp, strNameTemp.substr(0, strNameTemp.length() - 3)))
        strName = strNameTemp = strLinkTemp;

      // we detect http directory items by its display name and its stripped link
      // if same, we consider it as a valid item.
      if (strNameTemp == strLinkTemp && strLinkTemp != "..")
      {
        CFileItemPtr pItem(new CFileItem(strNameTemp));
        pItem->SetProperty("IsHTTPDirectory", true);
        url.SetFileName(strBasePath + strLinkBase);
        url.SetOptions(strLinkOptions);
        pItem->SetPath(url.Get());

        if(URIUtils::HasSlashAtEnd(pItem->GetPath(), true))
          pItem->m_bIsFolder = true;

        CStdString day, month, year, hour, minute;

        if (reDateTime.RegFind(strBuffer.c_str()) >= 0)
        {
          day = reDateTime.GetMatch(1);
          month = reDateTime.GetMatch(2);
          year = reDateTime.GetMatch(3);
          hour = reDateTime.GetMatch(4);
          minute = reDateTime.GetMatch(5);
        }
        else if (reDateTimeNginx.RegFind(strBuffer.c_str()) >= 0)
        {
          day = reDateTimeNginx.GetMatch(1);
          month = reDateTimeNginx.GetMatch(2);
          year = reDateTimeNginx.GetMatch(3);
          hour = reDateTimeNginx.GetMatch(4);
          minute = reDateTimeNginx.GetMatch(5);
        }
        else if (reDateTimeLighttp.RegFind(strBuffer.c_str()) >= 0)
        {
          day = reDateTimeLighttp.GetMatch(3);
          month = reDateTimeLighttp.GetMatch(2);
          year = reDateTimeLighttp.GetMatch(1);
          hour = reDateTimeLighttp.GetMatch(4);
          minute = reDateTimeLighttp.GetMatch(5);
        }

        if (day.length() > 0 && month.length() > 0 && year.length() > 0)
        {
          pItem->m_dateTime = CDateTime(atoi(year.c_str()), CDateTime::MonthStringToMonthNum(month), atoi(day.c_str()), atoi(hour.c_str()), atoi(minute.c_str()), 0);
        }

        if (!pItem->m_bIsFolder)
        {
          if (reSize.RegFind(strBuffer.c_str()) >= 0)
          {
            double Size = atof(reSize.GetMatch(1).c_str());
            std::string strUnit(reSize.GetMatch(2));

            if (strUnit == "K")
              Size = Size * 1024;
            else if (strUnit == "M")
              Size = Size * 1024 * 1024;
            else if (strUnit == "G")
              Size = Size * 1024 * 1024 * 1024;

            pItem->m_dwSize = (int64_t)Size;
          }
          else if (reSizeNginx.RegFind(strBuffer.c_str()) >= 0)
          {
            double Size = atof(reSizeNginx.GetMatch(1).c_str());
            std::string strUnit(reSize.GetMatch(2));

            if (strUnit == "K")
              Size = Size * 1024;
            else if (strUnit == "M")
              Size = Size * 1024 * 1024;
            else if (strUnit == "G")
              Size = Size * 1024 * 1024 * 1024;

            pItem->m_dwSize = (int64_t)Size;
          }
          else
          if (g_advancedSettings.m_bHTTPDirectoryStatFilesize) // As a fallback get the size by stat-ing the file (slow)
          {
            CCurlFile file;
            file.Open(url);
            pItem->m_dwSize=file.GetLength();
            file.Close();
          }
        }
        items.Add(pItem);
      }
    }
  }
  http.Close();

  items.SetProperty("IsHTTPDirectory", true);

  return true;
}

bool CHTTPDirectory::Exists(const char* strPath)
{
  CCurlFile http;
  CURL url(strPath);
  struct __stat64 buffer;

  if( http.Stat(url, &buffer) != 0 )
  {
    return false;
  }

  if (buffer.st_mode == _S_IFDIR)
	  return true;

  return false;
}
