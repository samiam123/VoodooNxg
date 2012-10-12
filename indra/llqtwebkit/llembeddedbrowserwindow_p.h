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

#ifndef LLEMBEDDEDBROWSERWINDOW_P_H
#define LLEMBEDDEDBROWSERWINDOW_P_H

#include "llwebpage.h"
#include "llwebpageopenshim.h"

#include <qgraphicsscene.h>
#include <qgraphicsview.h>
#include <qwebview.h>
#include <QWebInspector>
#include <list>

///////////////////////////////////////////////////////////////////////////////
// manages the process of storing and emitting events that the consumer
// of the embedding class can observe
template< class T >
class LLEmbeddedBrowserWindowEmitter
{
    public:
        LLEmbeddedBrowserWindowEmitter() { };
        ~LLEmbeddedBrowserWindowEmitter() { };

        typedef typename T::EventType EventType;
        typedef std::list< T* > ObserverContainer;
		typedef typename ObserverContainer::iterator iterator;
        typedef void(T::*observerMethod)(const EventType&);

        ///////////////////////////////////////////////////////////////////////////////
        //
        bool addObserver(T* observer)
        {
            if (! observer)
                return false;

            if (std::find(observers.begin(), observers.end(), observer) != observers.end())
                return false;

            observers.push_back(observer);

            return true;
        }

        ///////////////////////////////////////////////////////////////////////////////
        //
        bool remObserver(T* observer)
        {
            if (! observer)
                return false;

            observers.remove(observer);

            return true;
        }

        ///////////////////////////////////////////////////////////////////////////////
        //
        void update(observerMethod method, const EventType& msg)
        {
            typename std::list< T* >::iterator iter = observers.begin();

            while(iter != observers.end())
            {
                ((*iter)->*method)(msg);
                ++iter;
            }
        }

        int getObserverNumber()
        {
            return observers.size();
        }

		iterator begin()
		{
			return observers.begin();
		}

		iterator end()
		{
			return observers.end();
		}

    protected:
        ObserverContainer observers;
};

#include "llqtwebkit.h"
#include "llembeddedbrowserwindow.h"
#include <qgraphicssceneevent.h>
#include <qgraphicswebview.h>

class LLGraphicsScene : public QGraphicsScene
{
    Q_OBJECT

public:
    LLGraphicsScene();
    LLEmbeddedBrowserWindow *window;

    void mouseMoveEvent(QGraphicsSceneMouseEvent *mouseEvent) {
        QGraphicsScene::mouseMoveEvent(mouseEvent);
        mouseEvent->setAccepted(true);
        mouseEvent->setButtons(Qt::LeftButton);
    }

private slots:
    void repaintRequestedSlot(const QList<QRectF> &);
    friend class LLEmbeddedBrowserWindow;
};


class LLWebView : public QGraphicsWebView
{
    Q_OBJECT

public:
    LLWebView(QGraphicsItem *parent = 0);
    LLEmbeddedBrowserWindow *window;

    static QUrl guessUrlFromString(const QString &string);

    int width() const { return boundingRect().width(); }
    int height() const { return boundingRect().height(); }

protected:
    bool event(QEvent *event);

    Qt::CursorShape currentShape;
};

class LLEmbeddedBrowserWindowPrivate
{
    public:
    LLEmbeddedBrowserWindowPrivate()
        : mParent(0)
        , mPage(0)
        , mView(0)
        , mGraphicsScene(0)
        , mGraphicsView(0)
        , mInspector(0)
        , mCurrentMouseButtonState(Qt::NoButton)
        , mPercentComplete(0)
        , mShowLoadingOverlay(false)
        , mTimeLoadStarted(0)
        , mStatusText("")
        , mTitle("")
        , mCurrentUri("")
        , mNoFollowScheme("secondlife")
        , mWindowId(-1)
        , mEnabled(true)
        , mFlipBitmap(false)
        , mPageBuffer(NULL)
        , mDirty(false)
		, mOpeningSelf(false)
    {
    }

    ~LLEmbeddedBrowserWindowPrivate()
    {
		while(!mProxyPages.empty())
		{
			ProxyList::iterator iter = mProxyPages.begin();
			(*iter)->window = 0;
			(*iter)->deleteLater();
		}

		if(mGraphicsScene)
		{
        	mGraphicsScene->window = 0;
		}
		if(mPage)
		{
	        mPage->window = 0;
		}
		if(mView)
		{
	        mView->deleteLater();
		}
		if(mGraphicsScene)
		{
	        mGraphicsScene->deleteLater();
		}
		if(mGraphicsView)
		{
	        mGraphicsView->viewport()->setParent(mGraphicsView);
	        mGraphicsView->deleteLater();
		}
		if(mInspector)
		{
			mInspector->deleteLater();
		}
    }

	typedef LLEmbeddedBrowserWindowEmitter< LLEmbeddedBrowserWindowObserver> Emitter;
    Emitter mEventEmitter;
    QImage mImage;
    LLEmbeddedBrowser *mParent;
    LLWebPage *mPage;
	typedef std::list<LLWebPageOpenShim*> ProxyList;
	ProxyList mProxyPages;

    LLWebView *mView;
    QWebInspector* mInspector;
    LLGraphicsScene *mGraphicsScene;
    QGraphicsView *mGraphicsView;
    Qt::MouseButtons mCurrentMouseButtonState;

    int16_t mPercentComplete;
    bool mShowLoadingOverlay;
    time_t mTimeLoadStarted;
    std::string mStatusText;
    std::string mTitle;
    std::string mCurrentUri;
    QString mNoFollowScheme;
    int mWindowId;
    bool mEnabled;
    bool mFlipBitmap;
    unsigned char* mPageBuffer;
    QColor backgroundColor;
    bool mDirty;
	bool mOpeningSelf;
};


#endif

