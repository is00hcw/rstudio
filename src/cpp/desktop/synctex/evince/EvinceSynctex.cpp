/*
 * EvinceSynctex.cpp
 *
 * Copyright (C) 2009-12 by RStudio, Inc.
 *
 * This program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */

#include "EvinceSynctex.hpp"

#include <boost/format.hpp>

#include <core/Log.hpp>
#include <core/Error.hpp>
#include <core/DateTime.hpp>

#include <DesktopMainWindow.hpp>
#include <DesktopUtils.hpp>

#include "EvinceDaemon.hpp"
#include "EvinceWindow.hpp"

// TODO: window activation
// TODO: call evince directly for page
// TODO: handle differnet evince versions
// TODO: dynamic binding to correct type

using namespace core;

namespace desktop {
namespace synctex {

namespace {

void logDBusError(const QDBusError& error, const ErrorLocation& location)
{
   boost::format fmt("Error %1% (%2%): %3%");
   std::string msg = boost::str(fmt % error.type() %
                                      error.name().toStdString() %
                                      error.message().toStdString());
   core::log::logErrorMessage(msg, location);
}

} // anonymous namespace

EvinceSynctex::EvinceSynctex(MainWindow* pMainWindow)
   : QObject(pMainWindow),
     pMainWindow_(pMainWindow)
{
   pEvince_ = new EvinceDaemon(this);
}

void EvinceSynctex::syncView(const QString& pdfFile,
                             const QString& srcFile,
                             const QPoint& srcLoc)
{
   if (windows_.contains(pdfFile))
   {
      syncView(windows_.value(pdfFile), srcFile, srcLoc);
   }
   else
   {
      // find the window
      QDBusPendingReply<QString> reply = pEvince_->FindDocument(
                                       QUrl::fromLocalFile(pdfFile).toString(),
                                       true);

      // wait for the results asynchronously
      QDBusPendingCallWatcher* pWatcher = new QDBusPendingCallWatcher(reply,
                                                                      this);
      SyncRequest request;
      request.pdfFile = pdfFile;
      request.srcFile = srcFile;
      request.srcLoc = srcLoc;
      pendingSyncRequests_.insert(pWatcher, request);

      QObject::connect(pWatcher,
                       SIGNAL(finished(QDBusPendingCallWatcher*)),
                       this,
                       SLOT(onFindWindowFinished(QDBusPendingCallWatcher*)));
   }
}

void EvinceSynctex::onFindWindowFinished(QDBusPendingCallWatcher* pWatcher)
{
   // get the reply and the sync request params
   QDBusPendingReply<QString> reply = *pWatcher;
   SyncRequest req = pendingSyncRequests_.value(pWatcher);
   pendingSyncRequests_.remove(pWatcher);

   if (reply.isError())
   {
      logDBusError(reply.error(), ERROR_LOCATION);
   }
   else
   {
      // initialize a connection to it
      EvinceWindow* pWindow = new EvinceWindow(reply.value());
      if (!pWindow->isValid())
      {
         logDBusError(pWindow->lastError(), ERROR_LOCATION);
         return;
      }

      // put it in our map
      windows_.insert(req.pdfFile, pWindow);

      // sign up for events
      QObject::connect(pWindow,
                       SIGNAL(Closed()),
                       this,
                       SLOT(onClosed()));
      QObject::connect(pWindow,
                       SIGNAL(SyncSource(const QString&,const QPoint&,uint)),
                       this,
                       SLOT(onSyncSource(const QString&,const QPoint&,uint)));

      // perform sync
      syncView(pWindow, req.srcFile, req.srcLoc);
   }

   // delete the watcher
   pWatcher->deleteLater();
}

void EvinceSynctex::syncView(EvinceWindow* pWindow,
                             const QString& srcFile,
                             const QPoint& srcLoc)
{
   QDBusPendingReply<> reply = pWindow->SyncView(
                                       srcFile,
                                       srcLoc,
                                       core::date_time::secondsSinceEpoch());

   // wait for the results asynchronously
   QDBusPendingCallWatcher* pWatcher = new QDBusPendingCallWatcher(reply,
                                                                   this);
   QObject::connect(pWatcher,
                    SIGNAL(finished(QDBusPendingCallWatcher*)),
                    this,
                    SLOT(onSyncViewFinished(QDBusPendingCallWatcher*)));
}

void EvinceSynctex::onSyncViewFinished(QDBusPendingCallWatcher* pWatcher)
{
   QDBusPendingReply<QString> reply = *pWatcher;
   if (reply.isError())
      logDBusError(reply.error(), ERROR_LOCATION);

   pWatcher->deleteLater();
}

void EvinceSynctex::onClosed()
{
   EvinceWindow* pWindow = static_cast<EvinceWindow*>(sender());
   windows_.remove(windows_.key(pWindow));
   pWindow->deleteLater();
}


void EvinceSynctex::onSyncSource(const QString &source_file,
                  const QPoint &source_point,
                  uint timestamp)
{
    desktop::showWarning(NULL,
                         QString::fromAscii("Event - SyncSource"),
                         source_file);
}


} // namesapce synctex
} // namespace desktop