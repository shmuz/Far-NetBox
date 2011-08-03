/**************************************************************************
 *  NetBox plugin for FAR 2.0 (http://code.google.com/p/farplugs)         *
 *  Copyright (C) 2011 by Artem Senichev <artemsen@gmail.com>             *
 *  Copyright (C) 2011 by Michael Lukashov <michael.lukashov@gmail.com>   *
 *                                                                        *
 *  This program is free software: you can redistribute it and/or modify  *
 *  it under the terms of the GNU General Public License as published by  *
 *  the Free Software Foundation, either version 3 of the License, or     *
 *  (at your option) any later version.                                   *
 *                                                                        *
 *  This program is distributed in the hope that it will be useful,       *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *  GNU General Public License for more details.                          *
 *                                                                        *
 *  You should have received a copy of the GNU General Public License     *
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 **************************************************************************/

#pragma once

#include "Protocol.h"
#include "Session.h"


/**
 * Base (common) client's protocol implementation with session support
 */
template<class T> class CProtocolBase : public IProtocol
{
public:
    explicit CProtocolBase<T>(const CSession *session) : m_Session(*static_cast<const T *>(session)), m_ProgressPercent(-1) {}

    //From IProtocol
    virtual int GetProgress()
    {
        return m_ProgressPercent;
    }

    //From IProtocol
    virtual bool ChangeDirectory(const wchar_t *name, wstring &errorInfo)
    {
        assert(name && *name);
        DEBUG_PRINTF(L"NetBox: ChangeDirectory: name = %s, m_CurrentDirectory = %s", name, m_CurrentDirectory.c_str());

        const bool moveUp = (wcscmp(L"..", name) == 0);
        wstring newPath;
        if (name && (L'/' == name[0]))
        {
            m_CurrentDirectory = name;
            newPath = m_CurrentDirectory;
        }
        else
        {
            const bool topDirectory = (m_CurrentDirectory.compare(L"/") == 0);

            assert(!moveUp || !topDirectory);   //Must be handled in CPanel (exit from session)

            newPath = m_CurrentDirectory;
            if (moveUp)
            {
                const size_t lastSlash = newPath.rfind(L'/');
                assert(lastSlash != string::npos);
                if (lastSlash != string::npos && lastSlash != 0)
                {
                    newPath.erase(lastSlash);
                }
                else
                {
                    newPath = L'/';
                }
            }
            else
            {
                if (!topDirectory)
                {
                    newPath += L'/';
                }
                newPath += name;
            }
        }

        //Check path existing
        bool dirExist = false;
        DEBUG_PRINTF(L"NetBox: ChangeDirectory: name = %s, newPath = %s", name, newPath.c_str());
        if (!CheckExisting(newPath.c_str(), ItemDirectory, dirExist, errorInfo) || !dirExist)
        {
            return false;
        }
        m_CurrentDirectory = newPath;
        return true;
    }

    //From IProtocol
    virtual const wchar_t *GetCurrentDirectory()
    {
        return m_CurrentDirectory.c_str();
    }

    //From IProtocol
    virtual void FreeList(PluginPanelItem *items, int itemsNum)
    {
        if (itemsNum)
        {
            assert(items);
            for (int i = 0; i < itemsNum; ++i)
            {
                delete[] items[i].FindData.lpwszFileName;
                delete[] reinterpret_cast<void *>(items[i].UserData);
            }
            delete[] items;
        }
    }

    //From IProtocol
    virtual wstring GetURL(const bool includeUser = false)
    {
        unsigned short port = 0;
        wstring schemeName;
        wstring hostName;
        wstring path;
        ParseURL(m_Session.GetURL(), &schemeName, &hostName, &port, &path, NULL, NULL, NULL);

        wstring ret;
        ret += schemeName;
        ret += L"://";

        if (includeUser)
        {
            const wstring userName = m_Session.GetUserName();
            const wstring password = m_Session.GetPassword();
            if (!userName.empty() || !password.empty())
            {
                ret += userName;
                ret += L':';
                ret += password;
                ret += L'@';
            }
        }

        ret += hostName;
        if (port)
        {
            ret += L':';
            ret += ::NumberToWString(port);
        }

        return ret;
    }

    virtual bool TryToResolveConnectionProblem()
    {
        return false;
    }

    virtual bool Aborted() const
    {
        return false;
    }

protected:
    /**
     * Format error description
     * \param errCode system error code
     * \param info additional info
     * \return error description
     */
    wstring FormatErrorDescription(const DWORD errCode, const wchar_t *info = NULL) const
    {
        assert(errCode || info);

        wstring errDescr;
        if (info)
        {
            errDescr = info;
        }
        if (errCode)
        {
            if (!errDescr.empty())
            {
                errDescr += L'\n';
            }
            errDescr += GetSystemErrorMessage(errCode);
        }
        return errDescr;
    }

    /**
     * Convert local (unicode) charset to ftp codepage
     * \param src source path
     * \return path in ftp codepage
     */
    string LocalToFtpCP(const wchar_t *src, bool replace = false) const
    {
        assert(src && src[0] == L'/');
        string r = CFarPlugin::W2MB(src, m_Session.GetCodePage());
        if (replace)
        {
            while (r.find(L'#') != string::npos)
            {
                r.replace(r.find(L'#'), 1, "%23");    //libcurl think that it is an URL instead of path :-/
            }
        }
        // DEBUG_PRINTF(L"NetBox: LocalToFtpCP: r = %s", CFarPlugin::MB2W(r.c_str()).c_str());
        return r;
    }

    /**
     * Convert ftp charset to local (unicode) codepage
     * \param src source path
     * \return path in local (unicode) codepage
     */
    inline wstring FtpToLocalCP(const char *src) const
    {
        return CFarPlugin::MB2W(src, m_Session.GetCodePage());
    }


protected:
    T       m_Session;           ///< Session description
    int     m_ProgressPercent;   ///< Progress percent value
    wstring m_CurrentDirectory;  ///< Current directory name
};
