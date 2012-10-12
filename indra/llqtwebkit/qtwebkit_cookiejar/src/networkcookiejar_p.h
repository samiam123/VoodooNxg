/* Copyright (c) 2006-2010, Linden Research, Inc.
 * 
 * LLQtWebKit Source Code
 * The source code in this file ("Source Code") is provided by Linden Lab
 * to you under the terms of the GNU General Public License, version 2.0
 * ("GPL"), unless you have obtained a separate licensing agreement
 * ("Other License"), formally executed by you and Linden Lab.  Terms of
 * the GPL can be found in GPL-license.txt in this distribution, or online at
 * http://secondlifegrid.net/technology-programs/license-virtual-world/viewerlicensing/gplv2
 * 
 * There are special exceptions to the terms and conditions of the GPL as
 * it is applied to this Source Code. View the full text of the exception
 * in the file FLOSS-exception.txt in this software distribution, or
 * online at
 * http://secondlifegrid.net/technology-programs/license-virtual-world/viewerlicensing/flossexception
 * 
 * By copying, modifying or distributing this software, you acknowledge
 * that you have read and understood your obligations described above,
 * and agree to abide by those obligations.
 * 
 * ALL LINDEN LAB SOURCE CODE IS PROVIDED "AS IS." LINDEN LAB MAKES NO
 * WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY,
 * COMPLETENESS OR PERFORMANCE.
 */

#ifndef NETWORKCOOKIEJARPRIVATE_H
#define NETWORKCOOKIEJARPRIVATE_H

#include "trie_p.h"

QT_BEGIN_NAMESPACE
QDataStream &operator<<(QDataStream &stream, const QNetworkCookie &cookie)
{
    stream << cookie.toRawForm();
    return stream;
}

QDataStream &operator>>(QDataStream &stream, QNetworkCookie &cookie)
{
    QByteArray value;
    stream >> value;
    QList<QNetworkCookie> newCookies = QNetworkCookie::parseCookies(value);
    if (!newCookies.isEmpty())
        cookie = newCookies.first();
    return stream;
}
QT_END_NAMESPACE

class NetworkCookieJarPrivate {
public:
    NetworkCookieJarPrivate()
        : setSecondLevelDomain(false)
    {}

    Trie<QNetworkCookie> tree;
    mutable bool setSecondLevelDomain;
    mutable QStringList secondLevelDomains;

    bool matchesBlacklist(const QString &string) const;
    bool matchingDomain(const QNetworkCookie &cookie, const QUrl &url) const;
    QString urlPath(const QUrl &url) const;
    bool matchingPath(const QNetworkCookie &cookie, const QString &urlPath) const;
};

#endif

