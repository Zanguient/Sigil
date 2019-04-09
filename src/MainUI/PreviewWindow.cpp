/************************************************************************
**
**  Copyright (C) 2019 Kevin Hendricks, Doug Massay
**  Copyright (C) 2012 Dave Heiland, John Schember
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
**  Sigil is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Sigil.  If not, see <http://www.gnu.org/licenses/>.
**
*************************************************************************/

#include <QEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QStackedWidget>
#include <QVBoxLayout>
// #include <QtWebKitWidgets/QWebInspector>
#include <QDir>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QTimer>

#include "MainUI/PreviewWindow.h"
#include "Misc/SleepFunctions.h"
#include "Misc/SettingsStore.h"
#include "Misc/Utility.h"
#include "ViewEditors/ViewPreview.h"
#include "sigil_constants.h"

static const QString SETTINGS_GROUP = "previewwindow";

PreviewWindow::PreviewWindow(QWidget *parent)
    :
    QDockWidget(tr("Preview"), parent),
    m_MainWidget(new QWidget(this)),
    m_Layout(new QVBoxLayout(m_MainWidget)),
    m_Preview(new ViewPreview(this)),
    // m_Inspector(new QWebInspector(this)),
    m_Splitter(new QSplitter(this)),
    m_StackedViews(new QStackedWidget(this)),
    m_Filepath(QString()),
    m_GoToRequestPending(false)
{
    SetupView();
    LoadSettings();
    ConnectSignalsToSlots();
}

PreviewWindow::~PreviewWindow()
{
    // BookViewPreview must be deleted before QWebInspector.
    // BookViewPreview's QWebPage is linked to the QWebInspector
    // and when deleted it will send a message to the linked QWebInspector
    // to remove the association. If QWebInspector is deleted before
    // BookViewPreview, BookViewPreview will try to access the deleted
    // QWebInspector and the application will SegFault. This is an issue
    // with how QWebPages interface with QWebInspector.

#if 0
    if (m_Inspector) {
        m_Inspector->setPage(0);
        m_Inspector->close();
    }
#endif

    if (m_Preview) {
        delete m_Preview;
        m_Preview = 0;
    }

#if 0
    if (m_Inspector) {
        delete m_Inspector;
        m_Inspector = 0;
    }
#endif

    if (m_Splitter) {
        delete m_Splitter;
        m_Splitter = 0;
    }

    if (m_StackedViews) {
        delete(m_StackedViews);
        m_StackedViews= 0;
    }
}


void PreviewWindow::resizeEvent(QResizeEvent *event)
{
    // Update self normally
    QDockWidget::resizeEvent(event);
    UpdateWindowTitle();
}


void PreviewWindow::hideEvent(QHideEvent * event)
{
#if 0
    if (m_Inspector) {
        // break the link between the inspector and the page it is inspecting
        // to prevent memory corruption from Qt modified after free issue
        m_Inspector->setPage(0);
        if (m_Inspector->isVisible()) {
            m_Inspector->hide();
        }
    }
#endif
    if ((m_Preview) && m_Preview->isVisible()) {
        m_Preview->hide();
    }
    if ((m_Splitter) && m_Splitter->isVisible()) {
        m_Splitter->hide();
    }
    if ((m_StackedViews) && m_StackedViews->isVisible()) {
        m_StackedViews->hide();
    }
}

void PreviewWindow::showEvent(QShowEvent * event)
{
#if 0
    // restablish the link between the inspector and its page
    if ((m_Inspector) && (m_Preview)) {
        m_Inspector->setPage(m_Preview->page());
    }
#endif

    // perform the show for all children of this widget
    if ((m_Preview) && !m_Preview->isVisible()) {
        m_Preview->show();
    }

#if 0
    if ((m_Inspector) && !m_Inspector->isVisible()) {
        m_Inspector->show();
    }
#endif

    if ((m_Splitter) && !m_Splitter->isVisible()) {
        m_Splitter->show();
    }
    if ((m_StackedViews) && !m_StackedViews->isVisible()) {
        m_StackedViews->show();
    }
    QDockWidget::showEvent(event);
    raise();
    emit Shown();
}

bool PreviewWindow::IsVisible()
{
    return m_Preview->isVisible();
}

bool PreviewWindow::HasFocus()
{
    if (!m_Preview->isVisible()) {
        return false;
    }
    return m_Preview->hasFocus();
}

float PreviewWindow::GetZoomFactor()
{
    return m_Preview->GetZoomFactor();
}

void PreviewWindow::SetupView()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);

    // QWebEngineView events are routed to their parent
    m_Preview->installEventFilter(this);

    // m_Inspector->setPage(m_Preview->page());

    m_Layout->setContentsMargins(0, 0, 0, 0);
#ifdef Q_OS_MAC
    m_Layout->setSpacing(4);
#endif

    m_Layout->addWidget(m_StackedViews);

    m_Splitter->setOrientation(Qt::Vertical);
    m_Splitter->addWidget(m_Preview);
    // m_Splitter->addWidget(m_Inspector);
    m_Splitter->setSizes(QList<int>() << 400 << 200);
    m_StackedViews->addWidget(m_Splitter);

    m_MainWidget->setLayout(m_Layout);
    setWidget(m_MainWidget);

    m_Preview->Zoom();

    QApplication::restoreOverrideCursor();
}

void PreviewWindow::UpdatePage(QString filename, QString text, QList<ElementIndex> location)
{
    if (!m_Preview->isVisible()) {
        return;
    }

    // If this page uses the mathml, inject a polyfill
    // MathJax.js so that the mathml appears in the Preview Window
    QRegularExpression mathused("<\\s*math [^>]*>");
    QRegularExpressionMatch mo = mathused.match(text);
    if (mo.hasMatch()) {
        QString mathjaxurl;

        // The path to MathJax.js is platform dependent
#ifdef Q_OS_MAC
        // On Mac OS X QCoreApplication::applicationDirPath() points to Sigil.app/Contents/MacOS/ 
        QDir execdir(QCoreApplication::applicationDirPath());
        execdir.cdUp();
        mathjaxurl = execdir.absolutePath() + "/polyfills/MJ/MathJax.js";
#elif defined(Q_OS_WIN32)
        mathjaxurl = "/" + QCoreApplication::applicationDirPath() + "/polyfills/MJ/MathJax.js";
#else
        // all flavours of linux / unix
        // First check if system MathJax was configured to be used at compile time
        if (!mathjax_dir.isEmpty()) {
            mathjaxurl = mathjax_dir + "/MathJax.js";
        } else {
            // otherwise user supplied environment variable to 'share/sigil'
            // takes precedence over Sigil's usual share location.
            if (!sigil_extra_root.isEmpty()) {
                mathjaxurl = sigil_extra_root + "/polyfills/MJ/MathJax.js";
            } else {
                mathjaxurl = sigil_share_root + "/polyfills/MJ/MathJax.js";
            }
        }
#endif

        mathjaxurl = "file://" + Utility::URLEncodePath(mathjaxurl);
        mathjaxurl = mathjaxurl + "?config=local/SIGIL_EBOOK_MML_SVG";
        int endheadpos = text.indexOf("</head>");
        if (endheadpos > 1) {
            QString inject_mathjax = 
              "<script type=\"text/javascript\" async=\"async\" "
              "src=\"" + mathjaxurl + "\"></script>";
            text.insert(endheadpos, inject_mathjax);
        }
    }

    m_Filepath = filename;
    m_Preview->CustomSetDocument(filename, text);

    // Wait until the preview is loaded before moving cursor.
    while (!m_Preview->IsLoadingFinished()) {
        qApp->processEvents();
    }

    m_Preview->StoreCaretLocationUpdate(location);
    m_Preview->ExecuteCaretUpdate();
    // m_Preview->InspectElement();
    UpdateWindowTitle();
}

void PreviewWindow::UpdateWindowTitle()
{
    if ((m_Preview) && m_Preview->isVisible()) {
        int height = m_Preview->height();
        int width = m_Preview->width();
        setWindowTitle(tr("Preview") + " (" + QString::number(width) + "x" + QString::number(height) + ")");
    }
}

QList<ElementIndex> PreviewWindow::GetCaretLocation()
{
    QList<ElementIndex> hierarchy = m_Preview->GetCaretLocation();
    foreach(ElementIndex ei, hierarchy){
      qDebug()<< "name: " << ei.name << " index: " << ei.index;
    }
    return m_Preview->GetCaretLocation();
}

void PreviewWindow::SetZoomFactor(float factor)
{
    m_Preview->SetZoomFactor(factor);
}

void PreviewWindow::SplitterMoved(int pos, int index)
{
    Q_UNUSED(pos);
    Q_UNUSED(index);
    SettingsStore settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue("splitter", m_Splitter->saveState());
    settings.endGroup();
    UpdateWindowTitle();
}

void PreviewWindow::EmitGoToPreviewLocationRequest()
{
    if (m_GoToRequestPending) {
        m_GoToRequestPending = false;
        emit GoToPreviewLocationRequest();
    }
}

bool PreviewWindow::eventFilter(QObject *object, QEvent *event)
{
  bool return_value = QObject::eventFilter(object, event);
  switch (event->type()) {
    case QEvent::ChildAdded:
      if (object == m_Preview) {
	 qDebug() << "child add event";
	  const QChildEvent *childEvent(static_cast<QChildEvent*>(event));
	  if (childEvent->child()) {
	      childEvent->child()->installEventFilter(this);
	  }
      }
      break;
    case QEvent::MouseButtonPress:
      {
	  qDebug() << "Preview mouse button press event " << object;
	  const QMouseEvent *mouseEvent(static_cast<QMouseEvent*>(event));
	  if (mouseEvent) {
	      if (mouseEvent->button() == Qt::LeftButton) {
	          m_GoToRequestPending = true;
	          QTimer::singleShot(50, this, SLOT(EmitGoToPreviewLocationRequest()));
	      }
	  }
      }
      break;
    case QEvent::MouseButtonRelease:
      {
	  qDebug() << "Preview mouse button release event " << object;
      }
      break;
    default:
      break;
  }
  return return_value;
}



void PreviewWindow::LinkClicked(const QUrl &url)
{
    if (m_GoToRequestPending) m_GoToRequestPending = false;
    qDebug() << "in PreviewWindow LinkClicked with url :" << url.toString();

    if (url.toString().isEmpty()) {
        return;
    }

    QFileInfo finfo(m_Filepath);
    QString url_string = url.toString();

    // Convert fragments to full filename/fragments
    if (url_string.startsWith("#")) {
        url_string.prepend(finfo.fileName());
    } else if (url.scheme() == "file") {
        if (url_string.contains("/#")) {
            url_string.insert(url_string.indexOf("/#") + 1, finfo.fileName());
        }
    }

    emit OpenUrlRequest(QUrl(url_string));
}

void PreviewWindow::LoadSettings()
{
    SettingsStore settings;
    settings.beginGroup(SETTINGS_GROUP);
    m_Splitter->restoreState(settings.value("splitter").toByteArray());
    settings.endGroup();
}

void PreviewWindow::ConnectSignalsToSlots()
{
    connect(m_Splitter,  SIGNAL(splitterMoved(int, int)), this, SLOT(SplitterMoved(int, int)));
    connect(m_Preview,   SIGNAL(ZoomFactorChanged(float)), this, SIGNAL(ZoomFactorChanged(float)));
    connect(m_Preview,   SIGNAL(LinkClicked(const QUrl &)), this, SLOT(LinkClicked(const QUrl &)));
}

