/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "ActiveObjects.h"
#include "Utilities.h"

#include "pipeline/Node.h"
#include "pipeline/OutputPort.h"
#include "pipeline/Pipeline.h"

#include <pqActiveObjects.h>
#include <pqApplicationCore.h>
#include <pqObjectBuilder.h>
#include <pqRenderView.h>
#include <pqServer.h>
#include <pqView.h>

#include <vtkNew.h>
#include <vtkSMProxyIterator.h>
#include <vtkSMRenderViewProxy.h>
#include <vtkSMViewProxy.h>

namespace tomviz {

ActiveObjects::ActiveObjects() : QObject()
{
  connect(&pqActiveObjects::instance(), &pqActiveObjects::viewChanged, this,
          QOverload<pqView*>::of(&ActiveObjects::viewChanged));
}

ActiveObjects::~ActiveObjects() = default;

ActiveObjects& ActiveObjects::instance()
{
  static ActiveObjects theInstance;
  return theInstance;
}

void ActiveObjects::setActiveView(vtkSMViewProxy* view)
{
  pqActiveObjects::instance().setActiveView(tomviz::convert<pqView*>(view));
}

vtkSMViewProxy* ActiveObjects::activeView() const
{
  pqView* view = activePqView();
  return view ? view->getViewProxy() : nullptr;
}

pqView* ActiveObjects::activePqView() const
{
  return pqActiveObjects::instance().activeView();
}

pqRenderView* ActiveObjects::activePqRenderView() const
{
  return qobject_cast<pqRenderView*>(activePqView());
}

void ActiveObjects::viewChanged(pqView* view)
{
  emit viewChanged(view ? view->getViewProxy() : nullptr);
}

vtkSMSessionProxyManager* ActiveObjects::proxyManager() const
{
  pqServer* server = pqActiveObjects::instance().activeServer();
  return server ? server->proxyManager() : nullptr;
}

void ActiveObjects::createRenderViewIfNeeded()
{
  vtkNew<vtkSMProxyIterator> iter;
  iter->SetSessionProxyManager(proxyManager());
  iter->SetModeToOneGroup();
  for (iter->Begin("views"); !iter->IsAtEnd(); iter->Next()) {
    vtkSMRenderViewProxy* renderView =
      vtkSMRenderViewProxy::SafeDownCast(iter->GetProxy());
    if (renderView) {
      return;
    }
  }

  // If we get here, there was no existing view, so create one.
  pqObjectBuilder* builder = pqApplicationCore::instance()->getObjectBuilder();
  pqServer* server = pqApplicationCore::instance()->getActiveServer();
  builder->createView("RenderView", server);
}

void ActiveObjects::setActiveViewToFirstRenderView()
{
  vtkNew<vtkSMProxyIterator> iter;
  iter->SetSessionProxyManager(proxyManager());
  iter->SetModeToOneGroup();
  for (iter->Begin("views"); !iter->IsAtEnd(); iter->Next()) {
    vtkSMRenderViewProxy* renderView =
      vtkSMRenderViewProxy::SafeDownCast(iter->GetProxy());
    if (renderView) {
      setActiveView(renderView);
      break;
    }
  }
}

void ActiveObjects::renderAllViews()
{
  pqApplicationCore::instance()->render();
}

pqTimeKeeper* ActiveObjects::activeTimeKeeper() const
{
  pqServer* server = pqActiveObjects::instance().activeServer();
  return server ? server->getTimeKeeper() : nullptr;
}

void ActiveObjects::setActivePipeline(pipeline::Pipeline* p)
{
  if (m_activePipeline != p) {
    if (m_activePipeline) {
      disconnect(m_activePipeline, nullptr, this, nullptr);
    }
    m_activePipeline = p;
    if (p) {
      connect(p, &pipeline::Pipeline::nodeRemoved, this,
              [this](pipeline::Node* node) {
                if (m_activeNode == node) {
                  setActiveNode(nullptr);
                }
              });
    }
    emit activePipelineChanged(p);
  }
}

pipeline::Pipeline* ActiveObjects::activePipeline() const
{
  return m_activePipeline;
}

void ActiveObjects::setActiveNode(pipeline::Node* node)
{
  if (m_activeNode != node) {
    m_activeNode = node;
    emit activeNodeChanged(node);
  }
}

pipeline::Node* ActiveObjects::activeNode() const
{
  return m_activeNode;
}

void ActiveObjects::setActivePort(pipeline::OutputPort* port)
{
  if (m_activePort != port) {
    m_activePort = port;
    emit activePortChanged(port);
  }
}

pipeline::OutputPort* ActiveObjects::activePort() const
{
  return m_activePort;
}

} // end of namespace tomviz
