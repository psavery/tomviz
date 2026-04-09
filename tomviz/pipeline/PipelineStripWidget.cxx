/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "PipelineStripWidget.h"

#include "InputPort.h"
#include "Link.h"
#include "Node.h"
#include "NodeExecState.h"
#include "OutputPort.h"
#include "Pipeline.h"
#include "PortType.h"
#include "SinkNode.h"
#include "SourceNode.h"
#include "TransformNode.h"
#include "sinks/LegacyModuleSink.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QStyle>
#include <QStyleOption>

#include <cmath>

namespace tomviz {
namespace pipeline {

// Render an icon tinted to the given color, preserving alpha/opacity.
static void paintTintedIcon(QPainter& painter, const QIcon& icon,
                            const QRect& rect, const QColor& color)
{
  qreal dpr = painter.device()->devicePixelRatioF();
  QSize pxSize(qRound(rect.width() * dpr), qRound(rect.height() * dpr));
  QPixmap pix = icon.pixmap(pxSize);
  pix.setDevicePixelRatio(dpr);
  QPainter p(&pix);
  p.setCompositionMode(QPainter::CompositionMode_SourceIn);
  p.fillRect(pix.rect(), color);
  p.end();
  painter.drawPixmap(rect, pix);
}

// Build a QPainterPath through a sequence of points, rounding each interior
// corner with a quadratic B��zier curve.  The radius is clamped so it never
// exceeds half the length of an adjacent segment.
static QPainterPath roundedPolyline(const QList<QPointF>& pts, qreal radius)
{
  QPainterPath path;
  if (pts.size() < 2) {
    return path;
  }
  path.moveTo(pts.first());
  for (int i = 1; i < pts.size() - 1; ++i) {
    QPointF d1 = pts[i] - pts[i - 1];
    QPointF d2 = pts[i + 1] - pts[i];
    qreal len1 = std::sqrt(d1.x() * d1.x() + d1.y() * d1.y());
    qreal len2 = std::sqrt(d2.x() * d2.x() + d2.y() * d2.y());
    qreal r = qMin(radius, qMin(len1, len2) / 2.0);
    if (r <= 0) {
      path.lineTo(pts[i]);
      continue;
    }
    QPointF before = pts[i] - d1 / len1 * r;
    QPointF after = pts[i] + d2 / len2 * r;
    path.lineTo(before);
    path.quadTo(pts[i], after);
  }
  path.lineTo(pts.last());
  return path;
}

PipelineStripWidget::PipelineStripWidget(QWidget* parent) : QWidget(parent)
{
  setFocusPolicy(Qt::StrongFocus);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  setMouseTracking(true);

  m_spinnerTimer.setInterval(50);
  connect(&m_spinnerTimer, &QTimer::timeout, this, [this]() {
    m_spinnerAngle = (m_spinnerAngle + 30) % 360;
    update();
  });
}

void PipelineStripWidget::setPipeline(Pipeline* pipeline)
{
  if (m_pipeline == pipeline) {
    return;
  }
  disconnectPipeline();
  m_pipeline = pipeline;
  m_selectedIndex = -1;
  m_selectedPort = nullptr;
  m_selectedLink = nullptr;
  m_hoveredLink = nullptr;
  m_expandedNodes.clear();
  connectPipeline();
  rebuildLayout();
  update();
}

Pipeline* PipelineStripWidget::pipeline() const
{
  return m_pipeline;
}

void PipelineStripWidget::setSortOrder(SortOrder order)
{
  if (m_sortOrder != order) {
    m_sortOrder = order;
    rebuildLayout();
    update();
  }
}

SortOrder PipelineStripWidget::sortOrder() const
{
  return m_sortOrder;
}

void PipelineStripWidget::setDimLevel(qreal level)
{
  m_dimLevel = qBound(0.0, level, 1.0);
  update();
}

qreal PipelineStripWidget::dimLevel() const
{
  return m_dimLevel;
}

void PipelineStripWidget::setNodeDimmed(Node* node, bool dim)
{
  if (dim) {
    m_dimmedNodes.insert(node);
  } else {
    m_dimmedNodes.remove(node);
  }
  update();
}

void PipelineStripWidget::setPortDimmed(OutputPort* port, bool dim)
{
  if (dim) {
    m_dimmedPorts.insert(port);
  } else {
    m_dimmedPorts.remove(port);
  }
  update();
}

void PipelineStripWidget::setLinkDimmed(Link* link, bool dim)
{
  if (dim) {
    m_dimmedLinks.insert(link);
  } else {
    m_dimmedLinks.remove(link);
  }
  update();
}

bool PipelineStripWidget::isNodeDimmed(Node* node) const
{
  return m_dimmedNodes.contains(node);
}

bool PipelineStripWidget::isPortDimmed(OutputPort* port) const
{
  return m_dimmedPorts.contains(port);
}

bool PipelineStripWidget::isLinkDimmed(Link* link) const
{
  return m_dimmedLinks.contains(link);
}

void PipelineStripWidget::clearDimming()
{
  m_dimmedNodes.clear();
  m_dimmedPorts.clear();
  m_dimmedLinks.clear();
  update();
}

void PipelineStripWidget::updateDimming()
{
  if (!m_pipeline) {
    clearDimming();
    return;
  }

  // Collect all nodes, output ports, and links in the pipeline
  QSet<Node*> allNodes;
  QSet<OutputPort*> allPorts;
  for (auto& item : m_layout) {
    if (item.type == LayoutItem::NodeCard) {
      allNodes.insert(item.node);
      for (auto* port : item.node->outputPorts()) {
        allPorts.insert(port);
      }
    }
  }
  QSet<Link*> allLinks;
  for (auto* link : m_pipeline->links()) {
    allLinks.insert(link);
  }

  // Determine which elements should NOT be dimmed
  QSet<Node*> brightNodes;
  QSet<OutputPort*> brightPorts;
  QSet<Link*> brightLinks;

  // Helper: mark a link and both endpoints as bright
  auto brightenLink = [&](Link* link) {
    brightLinks.insert(link);
    auto* outPort = link->from();
    brightPorts.insert(outPort);
    brightNodes.insert(outPort->node());
    brightNodes.insert(link->to()->node());
  };

  bool hasSelection = false;

  // Output port selected (collapsed dot or expanded port card)
  OutputPort* selPort = m_selectedPort;
  if (!selPort && m_selectedIndex >= 0 && m_selectedIndex < m_layout.size() &&
      m_layout[m_selectedIndex].type == LayoutItem::PortCard) {
    selPort = m_layout[m_selectedIndex].port;
  }

  if (selPort) {
    hasSelection = true;
    brightPorts.insert(selPort);
    brightNodes.insert(selPort->node());
    for (auto* link : selPort->links()) {
      brightenLink(link);
    }
  } else if (m_selectedLink) {
    hasSelection = true;
    brightenLink(m_selectedLink);
  } else if (m_selectedIndex >= 0 && m_selectedIndex < m_layout.size() &&
             m_layout[m_selectedIndex].type == LayoutItem::NodeCard) {
    // Node selected
    hasSelection = true;
    auto* node = m_layout[m_selectedIndex].node;
    brightNodes.insert(node);
    // All output ports of this node
    for (auto* port : node->outputPorts()) {
      brightPorts.insert(port);
      for (auto* link : port->links()) {
        brightenLink(link);
      }
    }
    // All input ports of this node
    for (auto* inPort : node->inputPorts()) {
      if (inPort->link()) {
        brightenLink(inPort->link());
      }
    }
  }

  if (!hasSelection) {
    clearDimming();
    return;
  }

  // Dim everything that isn't bright
  m_dimmedNodes.clear();
  m_dimmedPorts.clear();
  m_dimmedLinks.clear();

  for (auto* node : allNodes) {
    if (!brightNodes.contains(node)) {
      m_dimmedNodes.insert(node);
    }
  }
  for (auto* port : allPorts) {
    if (!brightPorts.contains(port)) {
      m_dimmedPorts.insert(port);
    }
  }
  for (auto* link : allLinks) {
    if (!brightLinks.contains(link)) {
      m_dimmedLinks.insert(link);
    }
  }
}

Node* PipelineStripWidget::selectedNode() const
{
  if (m_selectedIndex >= 0 && m_selectedIndex < m_layout.size()) {
    return m_layout[m_selectedIndex].node;
  }
  return nullptr;
}

OutputPort* PipelineStripWidget::selectedPort() const
{
  if (m_selectedPort) {
    return m_selectedPort;
  }
  if (m_selectedIndex >= 0 && m_selectedIndex < m_layout.size()) {
    auto& item = m_layout[m_selectedIndex];
    if (item.type == LayoutItem::PortCard) {
      return item.port;
    }
  }
  return nullptr;
}

Link* PipelineStripWidget::selectedLink() const
{
  return m_selectedLink;
}

void PipelineStripWidget::setSelectedNode(Node* node)
{
  int index = -1;
  if (node) {
    for (int i = 0; i < m_layout.size(); ++i) {
      if (m_layout[i].node == node &&
          m_layout[i].type == LayoutItem::NodeCard) {
        index = i;
        break;
      }
    }
    if (index < 0) {
      return; // node not visible in layout
    }
  }
  if (index == m_selectedIndex && !m_selectedPort && !m_selectedLink) {
    return;
  }
  m_selectedIndex = index;
  m_selectedPort = nullptr;
  m_selectedLink = nullptr;
  updateDimming();
  update();
}

void PipelineStripWidget::setSelectedPort(OutputPort* port)
{
  if (!port) {
    // Clear port-dot selection only; preserve node card selection.
    if (m_selectedPort) {
      m_selectedPort = nullptr;
      updateDimming();
      update();
    }
    return;
  }
  // Check for a visible port card first.
  int index = -1;
  for (int i = 0; i < m_layout.size(); ++i) {
    if (m_layout[i].type == LayoutItem::PortCard &&
        m_layout[i].port == port) {
      index = i;
      break;
    }
  }
  if (index >= 0) {
    if (index == m_selectedIndex && !m_selectedPort && !m_selectedLink) {
      return;
    }
    m_selectedIndex = index;
    m_selectedPort = nullptr;
    m_selectedLink = nullptr;
  } else {
    // No port card — select via output-dot highlight.
    if (m_selectedPort == port && m_selectedIndex < 0 && !m_selectedLink) {
      return;
    }
    m_selectedIndex = -1;
    m_selectedPort = port;
    m_selectedLink = nullptr;
  }
  updateDimming();
  update();
}

void PipelineStripWidget::setSelectedLink(Link* link)
{
  if (!link) {
    // Clear link selection only; preserve node/port selection.
    if (m_selectedLink) {
      m_selectedLink = nullptr;
      updateDimming();
      update();
    }
    return;
  }
  if (link == m_selectedLink && m_selectedIndex < 0 && !m_selectedPort) {
    return;
  }
  m_selectedIndex = -1;
  m_selectedPort = nullptr;
  m_selectedLink = link;
  updateDimming();
  update();
}

void PipelineStripWidget::setTipOutputPort(OutputPort* port)
{
  if (m_tipOutputPort != port) {
    m_tipOutputPort = port;
    update();
  }
}

OutputPort* PipelineStripWidget::tipOutputPort() const
{
  return m_tipOutputPort;
}

void PipelineStripWidget::setNodeMenuProvider(NodeMenuProvider provider)
{
  m_nodeMenuProvider = std::move(provider);
}

void PipelineStripWidget::setPortMenuProvider(PortMenuProvider provider)
{
  m_portMenuProvider = std::move(provider);
}

void PipelineStripWidget::setLinkMenuProvider(LinkMenuProvider provider)
{
  m_linkMenuProvider = std::move(provider);
}

void PipelineStripWidget::setLinkValidator(LinkValidator validator)
{
  m_linkValidator = std::move(validator);
}

bool PipelineStripWidget::isExpanded(Node* node) const
{
  return m_expandedNodes.contains(node);
}

void PipelineStripWidget::setExpanded(Node* node, bool expanded)
{
  if (expanded) {
    m_expandedNodes.insert(node);
  } else {
    m_expandedNodes.remove(node);
  }
  rebuildLayout();
  update();
}

QSize PipelineStripWidget::minimumSizeHint() const
{
  return QSize(100, 50);
}

QSize PipelineStripWidget::sizeHint() const
{
  if (m_layout.isEmpty()) {
    return QSize(200, 50);
  }
  auto& last = m_layout.last();
  int h = last.rect.bottom() + Padding + CardSpacing;
  return QSize(200, h);
}

void PipelineStripWidget::rebuildLayout()
{
  // Remember what was selected so we can restore after rebuilding
  Node* selectedNode = nullptr;
  OutputPort* selectedPort = nullptr;
  if (m_selectedIndex >= 0 && m_selectedIndex < m_layout.size()) {
    auto& sel = m_layout[m_selectedIndex];
    selectedNode = sel.node;
    selectedPort = sel.port; // non-null only for PortCard
  }
  // Also consider a selected output dot
  if (m_selectedPort) {
    selectedPort = m_selectedPort;
  }

  m_layout.clear();
  m_linkGeometries.clear();
  m_selectedIndex = -1;
  m_selectedPort = nullptr;

  if (!m_pipeline) {
    updateGeometry();
    return;
  }

  auto sorted = m_pipeline->topologicalSort({}, m_sortOrder);
  int y = Padding;
  int cardWidth = qMax(width() - GutterWidth - Padding * 2, 80);

  bool anyExecuting = false;

  Node* prevNode = nullptr;
  bool prevCollapsed = false;

  for (int ni = 0; ni < sorted.size(); ++ni) {
    auto* node = sorted[ni];
    auto* nextNode = (ni + 1 < sorted.size()) ? sorted[ni + 1] : nullptr;

    // --- Input side clearance ---
    auto inputs = node->inputPorts();
    if (!inputs.isEmpty()) {
      // Physical overflow of input dots above the node rect
      y += DotRadius;

      // Link routing space
      int nGutterInputs = 0;
      int nDirectInputs = 0;
      for (auto* inp : inputs) {
        if (!inp->link()) {
          continue;
        }
        Node* src = inp->link()->from()->node();
        bool isDirect = prevCollapsed && src == prevNode;
        if (isDirect) {
          nDirectInputs++;
        } else {
          nGutterInputs++;
        }
      }
      int inputLinkSpace = 0;
      if (nGutterInputs > 0 && nDirectInputs > 0) {
        inputLinkSpace =
          qMax(nGutterInputs * LaneSpacing, DirectConnectionSpacing);
      } else if (nGutterInputs > 0) {
        inputLinkSpace = nGutterInputs * LaneSpacing;
      } else if (nDirectInputs > 0) {
        inputLinkSpace = DirectConnectionSpacing;
      }
      y += inputLinkSpace;
    }

    // Port sub-cards (shown only when node is expanded)
    auto outputs = node->outputPorts();
    bool showPorts =
      !outputs.isEmpty() && m_expandedNodes.contains(node);

    // Compute node card height: base height + port cards inside when expanded
    int nodeHeight = NodeCardHeight;
    if (showPorts) {
      nodeHeight += PortCardSpacing; // top padding below header
      nodeHeight += outputs.size() * PortCardHeight;
      nodeHeight += (outputs.size() - 1) * PortCardSpacing;
      nodeHeight += PortIndent; // bottom padding
    }

    // Compute indentation level based on node type and connections.
    // - All inputs disconnected: indent 0
    // - Source nodes: always indent 0
    // - Transform nodes with connections: indent 1
    // - Sink nodes with connections: indent 2 if connected to at least one
    //   transform, otherwise indent 1
    int indentLevel = 0;
    bool hasConnectedInput = false;
    for (auto* inp : inputs) {
      if (inp->link()) {
        hasConnectedInput = true;
        break;
      }
    }
    if (hasConnectedInput && !qobject_cast<SourceNode*>(node)) {
      if (qobject_cast<SinkNode*>(node)) {
        bool connectedToTransform = false;
        for (auto* inp : inputs) {
          if (inp->link() &&
              qobject_cast<TransformNode*>(inp->link()->from()->node())) {
            connectedToTransform = true;
            break;
          }
        }
        indentLevel = connectedToTransform ? 2 : 1;
      } else {
        // TransformNode (or any non-source, non-sink)
        indentLevel = 1;
      }
    }
    int indent = indentLevel * IndentWidth;

    // Node card
    LayoutItem nodeItem;
    nodeItem.type = LayoutItem::NodeCard;
    nodeItem.node = node;
    nodeItem.rect =
      QRect(GutterWidth + Padding + indent, y, cardWidth - indent, nodeHeight);
    m_layout.append(nodeItem);

    if (showPorts) {
      int portY = y + NodeCardHeight + PortCardSpacing;
      for (auto* port : outputs) {
        LayoutItem portItem;
        portItem.type = LayoutItem::PortCard;
        portItem.node = node;
        portItem.port = port;
        portItem.rect =
          QRect(GutterWidth + Padding + indent + PortIndent, portY,
                cardWidth - indent - 2 * PortIndent, PortCardHeight);
        m_layout.append(portItem);
        portY += PortCardHeight + PortCardSpacing;
      }
    }
    y += nodeHeight;

    // --- Output side clearance ---
    if (!showPorts && !outputs.isEmpty()) {
      // Physical overflow of output squares below the node rect
      y += OutputSquareEdge - OutputSquareOverlap;

      // Link routing space
      int nGutterOutputs = 0;
      int nDirectOutputs = 0;
      for (int i = 0; i < outputs.size(); ++i) {
        if (outputs[i]->links().isEmpty()) {
          continue;
        }
        bool allDirect = (nextNode != nullptr);
        if (allDirect) {
          for (auto* link : outputs[i]->links()) {
            if (link->to()->node() != nextNode) {
              allDirect = false;
              break;
            }
          }
        }
        if (allDirect) {
          nDirectOutputs++;
        } else {
          nGutterOutputs++;
        }
      }
      int outputLinkSpace = 0;
      if (nGutterOutputs > 0 && nDirectOutputs > 0) {
        outputLinkSpace =
          qMax(nGutterOutputs * LaneSpacing, DirectConnectionSpacing);
      } else if (nGutterOutputs > 0) {
        outputLinkSpace = nGutterOutputs * LaneSpacing;
      } else if (nDirectOutputs > 0) {
        outputLinkSpace = DirectConnectionSpacing;
      }
      y += outputLinkSpace;
    }

    // Static margin between nodes
    y += CardSpacing;

    prevNode = node;
    prevCollapsed = !showPorts && !outputs.isEmpty();
  }

  Q_UNUSED(anyExecuting);

  // Restore selection by matching the previously selected node/port
  if (selectedPort) {
    // Try to find as a port card first
    bool found = false;
    for (int i = 0; i < m_layout.size(); ++i) {
      if (m_layout[i].port == selectedPort) {
        m_selectedIndex = i;
        found = true;
        break;
      }
    }
    // If not a port card (collapsed), restore as selected dot
    if (!found) {
      m_selectedPort = selectedPort;
    }
  } else if (selectedNode) {
    for (int i = 0; i < m_layout.size(); ++i) {
      if (m_layout[i].type == LayoutItem::NodeCard &&
          m_layout[i].node == selectedNode) {
        m_selectedIndex = i;
        break;
      }
    }
  }

  computeLinkGeometries();
  updateGeometry();
}

void PipelineStripWidget::connectPipeline()
{
  if (!m_pipeline) {
    return;
  }

  auto connectNode = [this](Node* node) {
    connect(node, &Node::stateChanged, this,
            QOverload<>::of(&QWidget::update));
    connect(node, &Node::execStateChanged, this, [this](NodeExecState state) {
      if (state == NodeExecState::Running) {
        if (!m_spinnerTimer.isActive()) {
          m_spinnerTimer.start();
        }
      } else {
        // Stop the spinner only if no other node is still running
        bool anyRunning = false;
        for (const auto& item : m_layout) {
          if (item.node && item.node->execState() == NodeExecState::Running) {
            anyRunning = true;
            break;
          }
        }
        if (!anyRunning) {
          m_spinnerTimer.stop();
        }
      }
      update();
    });
    connect(node, &Node::editingChanged, this,
            QOverload<>::of(&QWidget::update));
    connect(node, &Node::labelChanged, this,
            QOverload<>::of(&QWidget::update));
    connect(node, &Node::breakpointChanged, this,
            QOverload<>::of(&QWidget::update));
    auto* sink = qobject_cast<LegacyModuleSink*>(node);
    if (sink) {
      connect(sink, &LegacyModuleSink::visibilityChanged, this,
              QOverload<>::of(&QWidget::update));
    }
  };

  connect(m_pipeline, &Pipeline::nodeAdded, this, [this, connectNode](Node* node) {
    connectNode(node);
    rebuildLayout();
    update();
  });

  connect(m_pipeline, &Pipeline::nodeRemoved, this, [this](Node* node) {
    node->disconnect(this);
    if (m_selectedIndex >= 0 && m_selectedIndex < m_layout.size() &&
        m_layout[m_selectedIndex].node == node) {
      m_selectedIndex = -1;
    }
    m_expandedNodes.remove(node);
    rebuildLayout();
    update();
  });

  connect(m_pipeline, &Pipeline::linkCreated, this, [this](Link* link) {
    connect(link, &Link::validityChanged, this, [this]() {
      rebuildLayout();
      update();
    });
    rebuildLayout();
    update();
  });

  connect(m_pipeline, &Pipeline::linkRemoved, this, [this](Link*) {
    rebuildLayout();
    update();
  });

  // Connect existing nodes
  for (auto* node : m_pipeline->nodes()) {
    connectNode(node);
  }

  // Connect existing links for validity change notifications
  for (auto* link : m_pipeline->links()) {
    connect(link, &Link::validityChanged, this, [this]() {
      rebuildLayout();
      update();
    });
  }
}

void PipelineStripWidget::disconnectPipeline()
{
  if (!m_pipeline) {
    return;
  }
  m_pipeline->disconnect(this);
  for (auto* node : m_pipeline->nodes()) {
    node->disconnect(this);
  }
  m_spinnerTimer.stop();
}

void PipelineStripWidget::selectItem(int index)
{
  if (index == m_selectedIndex && !m_selectedPort && !m_selectedLink) {
    return;
  }
  m_selectedIndex = index;
  m_selectedPort = nullptr;
  m_selectedLink = nullptr;
  updateDimming();
  update();

  if (index >= 0 && index < m_layout.size()) {
    auto& item = m_layout[index];
    if (item.type == LayoutItem::PortCard) {
      emit portSelected(item.port);
    } else {
      emit nodeSelected(item.node);
    }
  } else {
    emit nodeSelected(nullptr);
  }
}

int PipelineStripWidget::hitTest(const QPoint& pos) const
{
  // Search in reverse so port cards (which are inside their parent node card's
  // rect) are found before the enclosing node card.
  for (int i = m_layout.size() - 1; i >= 0; --i) {
    if (m_layout[i].rect.contains(pos)) {
      return i;
    }
  }
  return -1;
}

int PipelineStripWidget::selectedIndex() const
{
  return m_selectedIndex;
}

void PipelineStripWidget::selectLink(Link* link)
{
  if (link == m_selectedLink && m_selectedIndex < 0 && !m_selectedPort) {
    return;
  }
  m_selectedIndex = -1;
  m_selectedPort = nullptr;
  m_selectedLink = link;
  updateDimming();
  update();
  emit linkSelected(link);
}

Link* PipelineStripWidget::linkHitTest(const QPoint& pos) const
{
  QPainterPathStroker stroker;
  stroker.setWidth(8.0);
  for (auto& lg : m_linkGeometries) {
    QPainterPath hitPath = stroker.createStroke(lg.path);
    if (hitPath.contains(pos)) {
      return lg.link;
    }
  }
  return nullptr;
}

// --- Painting ---

void PipelineStripWidget::paintEvent(QPaintEvent* event)
{
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.fillRect(rect(), palette().window());

  // Layer 1: Node and port cards (behind links)
  for (int i = 0; i < m_layout.size(); ++i) {
    bool selected = (i == m_selectedIndex);
    bool hovered = (i == m_hoveredIndex);
    auto& item = m_layout[i];

    if (item.type == LayoutItem::NodeCard) {
      paintNodeCard(painter, item, selected, hovered);
    } else {
      paintPortCard(painter, item, selected, hovered);
    }
  }

  // Layer 2: Links (on top of cards)
  paintConnections(painter);

  // Layer 3: Port indicators (on top of links)
  for (int i = 0; i < m_layout.size(); ++i) {
    auto& item = m_layout[i];
    if (item.type == LayoutItem::NodeCard) {
      paintInputDots(painter, item);
      paintOutputDots(painter, item);
    }
  }

  // Paint pending link on top of everything during drag
  paintPendingLink(painter);
}

void PipelineStripWidget::paintNodeCard(QPainter& painter,
                                        const LayoutItem& item, bool selected,
                                        bool hovered)
{
  auto r = item.rect;
  auto* node = item.node;

  // Node-type colored border; fill only when selected
  bool isDim = m_dimmedNodes.contains(node);
  QColor fg = selected ? palette().highlightedText().color()
                       : palette().buttonText().color();
  QColor borderColor = badgeColor(node);
  if (isDim) {
    fg = dimmed(fg);
    borderColor = dimmed(borderColor);
  }

  QPainterPath path;
  path.addRoundedRect(QRectF(r), CardRadius, CardRadius);
  if (selected) {
    // Fill entire card with body color first, then header on top
    QColor bodyColor = borderColor;
    bodyColor.setAlphaF(0.3);
    painter.fillPath(path, bodyColor);
    // Header at full opacity (use a rect with no bottom rounding, clipped
    // to the card path so only the top corners are rounded)
    QRectF headerRect(r.left(), r.top(), r.width(), NodeCardHeight);
    QPainterPath headerPath;
    headerPath.addRect(headerRect);
    headerPath = headerPath.intersected(path);
    painter.fillPath(headerPath, borderColor);
  } else {
    QColor bgColor = borderColor;
    bgColor.setAlphaF(0.05);
    painter.fillPath(path, bgColor);
  }
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(borderColor, 1.5));
  painter.drawPath(path);

  int x = r.left() + 6;
  int cy = r.top() + NodeCardHeight / 2; // header row center
  int headerHeight = NodeCardHeight;

  // Node icon
  QIcon nodeIcon = node->icon();
  int iconPaintSize = 14;
  QRect iconRect(x + (BadgeSize - iconPaintSize) / 2,
                 cy - iconPaintSize / 2,
                 iconPaintSize, iconPaintSize);
  if (selected) {
    paintTintedIcon(painter, nodeIcon, iconRect, fg);
  } else if (isDim) {
    painter.setOpacity(1.0 - m_dimLevel);
    nodeIcon.paint(&painter, iconRect);
    painter.setOpacity(1.0);
  } else {
    nodeIcon.paint(&painter, iconRect);
  }

  x += BadgeSize + 6;

  // Label
  QFont labelFont = font();
  labelFont.setPixelSize(12);
  painter.setFont(labelFont);
  painter.setPen(fg);
  QFontMetrics fm(labelFont);

  // Right side layout: [breakpoint] [state] [menu] | [expand/action]
  auto outputs = node->outputPorts();
  int toggleX = r.right() - HeaderExpandWidth - HeaderRightPad;
  int sepX = toggleX - HeaderButtonGap / 2;
  int menuX = toggleX - HeaderButtonGap - HeaderIconSize;
  int stateX = menuX - HeaderButtonSpacing - HeaderIconSize;
  int bpX = stateX - HeaderButtonSpacing - HeaderIconSize;

  int labelWidth = bpX - x - 2;
  QString elidedLabel =
    fm.elidedText(node->label(), Qt::ElideRight, labelWidth);
  painter.drawText(QRect(x, r.top(), labelWidth, headerHeight),
                   Qt::AlignVCenter | Qt::AlignLeft, elidedLabel);

  // Breakpoint indicator / hover hint
  int btnY = r.top() + (headerHeight - HeaderIconSize) / 2;
  QRect bpRect = breakpointRect(r);
  QIcon bpIcon(QStringLiteral(":/pipeline/breakpoint.png"));
  if (node->hasBreakpoint()) {
    if (selected) {
      paintTintedIcon(painter, bpIcon, bpRect, fg);
    } else if (isDim) {
      painter.setOpacity(1.0 - m_dimLevel);
      bpIcon.paint(&painter, bpRect);
      painter.setOpacity(1.0);
    } else {
      bpIcon.paint(&painter, bpRect);
    }
  } else if (hovered && !isDim) {
    painter.setOpacity(0.25);
    bpIcon.paint(&painter, bpRect);
    painter.setOpacity(1.0);
  }

  // State indicator icon
  QRect stateRect(stateX, btnY, HeaderIconSize, HeaderIconSize);

  if (m_spinnerTimer.isActive() &&
      node->execState() == NodeExecState::Running) {
    // Draw rotating spinner icon
    QPixmap spinner(QStringLiteral(":/pipeline/spinner.png"));
    if (selected) {
      QPainter p(&spinner);
      p.setCompositionMode(QPainter::CompositionMode_SourceIn);
      p.fillRect(spinner.rect(), fg);
      p.end();
    }
    painter.save();
    painter.translate(stateRect.center());
    painter.rotate(m_spinnerAngle);
    if (isDim && !selected) {
      painter.setOpacity(1.0 - m_dimLevel);
    }
    painter.drawPixmap(-HeaderIconSize / 2, -HeaderIconSize / 2,
                       HeaderIconSize, HeaderIconSize, spinner);
    painter.restore();
  } else {
    QIcon icon = stateIcon(node);
    if (selected) {
      paintTintedIcon(painter, icon, stateRect, fg);
    } else if (isDim) {
      painter.setOpacity(1.0 - m_dimLevel);
      icon.paint(&painter, stateRect);
      painter.setOpacity(1.0);
    } else {
      icon.paint(&painter, stateRect);
    }
  }

  // Menu button (three vertical dots)
  QRect menuRect = menuButtonRect(r);
  qreal dotR = 1.5;
  int dotCX = menuRect.center().x();
  int dotGap = 4;
  QColor dotColor = fg;
  painter.setBrush(dotColor);
  painter.setPen(Qt::NoPen);
  painter.drawEllipse(QPointF(dotCX, cy - dotGap), dotR, dotR);
  painter.drawEllipse(QPointF(dotCX, cy), dotR, dotR);
  painter.drawEllipse(QPointF(dotCX, cy + dotGap), dotR, dotR);

  // Vertical separator between menu and expand/action
  QColor sepColor = selected ? fg : borderColor;
  sepColor.setAlphaF(0.5);
  painter.setPen(QPen(sepColor, 1.0));
  int sepTop = r.top() + 6;
  int sepBot = r.top() + headerHeight - 6;
  painter.drawLine(sepX, sepTop, sepX, sepBot);

  // Expand toggle for nodes with output ports
  if (!outputs.isEmpty()) {
    bool expanded = m_expandedNodes.contains(node);
    QColor arrowColor = fg;
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(arrowColor, 1.5, Qt::SolidLine, Qt::RoundCap,
                        Qt::RoundJoin));
    int arrowSize = 4;
    int arrowCX = toggleX + HeaderExpandWidth / 2;
    int halfW = 2 * arrowSize / 3;
    if (expanded) {
      // Down-pointing chevron (v)
      painter.drawLine(QPointF(arrowCX - arrowSize, cy - halfW),
                       QPointF(arrowCX, cy + halfW));
      painter.drawLine(QPointF(arrowCX, cy + halfW),
                       QPointF(arrowCX + arrowSize, cy - halfW));
    } else {
      // Right-pointing chevron (>)
      painter.drawLine(QPointF(arrowCX - halfW, cy - arrowSize),
                       QPointF(arrowCX + halfW, cy));
      painter.drawLine(QPointF(arrowCX + halfW, cy),
                       QPointF(arrowCX - halfW, cy + arrowSize));
    }
  } else {
    // Action button for nodes without outputs (e.g. visibility toggle)
    QIcon actIcon = node->actionIcon();
    if (!actIcon.isNull()) {
      QRect actRect = actionButtonRect(r);
      if (selected) {
        paintTintedIcon(painter, actIcon, actRect, fg);
      } else if (isDim) {
        painter.setOpacity(1.0 - m_dimLevel);
        actIcon.paint(&painter, actRect);
        painter.setOpacity(1.0);
      } else {
        actIcon.paint(&painter, actRect);
      }
    }
  }

  // Reset brush so it doesn't leak into subsequent paint calls
  painter.setBrush(Qt::NoBrush);
}

void PipelineStripWidget::paintPortCard(QPainter& painter,
                                        const LayoutItem& item, bool selected,
                                        bool hovered)
{
  Q_UNUSED(hovered);
  auto r = item.rect;
  auto* port = item.port;

  QColor color = portTypeColor(port);
  bool isTip = (m_tipOutputPort == port);
  bool isDim = m_dimmedPorts.contains(port);
  if (isDim) {
    color = dimmed(color);
  }

  // Port card background and border
  QPainterPath path;
  path.addRoundedRect(QRectF(r), CardRadius - 1, CardRadius - 1);
  painter.setBrush(selected ? color : palette().window());
  painter.setPen(QPen(color, 1.5));
  painter.drawPath(path);

  // Icon square on the left (same height as the port card, no padding)
  int sqEdge = r.height();
  QRect sqRect(r.left(), r.top(), sqEdge, sqEdge);

  if (selected) {
    // Selection outline behind the icon square
    painter.setBrush(palette().window());
    painter.setPen(QPen(palette().highlight().color(), 1.5));
    QRect selOutline = sqRect.adjusted(-3, -3, 3, 3);
    painter.drawRoundedRect(selOutline, OutputSquareRadius + 2,
                            OutputSquareRadius + 2);
  } else if (isTip) {
    // Tip outline behind the icon square
    painter.setBrush(palette().window());
    painter.setPen(QPen(Qt::red, 1.5));
    QRect tipOutline = sqRect.adjusted(-3, -3, 3, 3);
    painter.drawRoundedRect(tipOutline, OutputSquareRadius + 2,
                            OutputSquareRadius + 2);
  }

  // Draw the icon square (on top of any outline)
  painter.setBrush(selected ? color : palette().window());
  painter.setPen(QPen(color, 1.5));
  painter.drawRoundedRect(sqRect, OutputSquareRadius, OutputSquareRadius);

  // Port icon inside the square
  QIcon pIcon = portTypeIcon(port);
  int iconSize = sqEdge - 4;
  QRect pIconRect(sqRect.left() + (sqEdge - iconSize) / 2,
                  sqRect.top() + (sqEdge - iconSize) / 2,
                  iconSize, iconSize);
  QColor iconColor = selected ? Qt::white : color;
  paintTintedIcon(painter, pIcon, pIconRect, iconColor);

  int x = r.left() + sqEdge + 6; // 2px extra inset from icon square

  // Port name
  QColor fg = selected ? Qt::white : palette().text().color();
  QFont portFont = font();
  portFont.setPixelSize(11);
  painter.setFont(portFont);
  painter.setPen(fg);
  QFontMetrics fm(portFont);

  // Menu dots on the right
  int menuPad = 8;
  int menuAreaWidth = HeaderIconSize;
  int menuCX = r.right() - menuPad;
  int menuCY = r.top() + r.height() / 2;

  QColor dotColor = selected ? Qt::white : palette().buttonText().color();
  qreal dotR = 1.5;
  int dotGap = 4;
  painter.setBrush(dotColor);
  painter.setPen(Qt::NoPen);
  painter.drawEllipse(QPointF(menuCX, menuCY - dotGap), dotR, dotR);
  painter.drawEllipse(QPointF(menuCX, menuCY), dotR, dotR);
  painter.drawEllipse(QPointF(menuCX, menuCY + dotGap), dotR, dotR);

  int labelWidth = r.right() - menuPad - menuAreaWidth - x;
  QString name = fm.elidedText(port->name(), Qt::ElideRight, labelWidth);
  painter.setPen(fg);
  painter.drawText(QRect(x, r.top(), labelWidth, r.height()),
                   Qt::AlignVCenter | Qt::AlignLeft, name);
}

QPoint PipelineStripWidget::inputDotPos(Node* node, int portIndex,
                                        const QRect& nodeRect) const
{
  int startX = nodeRect.left() + PortIndent;
  int x = (node->inputPorts().size() <= 1) ? startX
                                           : startX + portIndex * OutputSquareSpacing;
  return QPoint(x, nodeRect.top());
}

QPoint PipelineStripWidget::outputDotPos(Node* node, int portIndex,
                                         const QRect& nodeRect) const
{
  int startX = nodeRect.left() + PortIndent;
  int x = (node->outputPorts().size() <= 1) ? startX
                                            : startX + portIndex * OutputSquareSpacing;
  int y = nodeRect.bottom() + (OutputSquareEdge / 2 - OutputSquareOverlap);
  return QPoint(x, y);
}

QPoint PipelineStripWidget::outputPortPos(OutputPort* port) const
{
  auto* node = port->node();
  for (auto& item : m_layout) {
    if (item.type == LayoutItem::PortCard && item.port == port) {
      return QPoint(item.rect.left(), item.rect.center().y());
    }
  }
  for (auto& item : m_layout) {
    if (item.type == LayoutItem::NodeCard && item.node == node) {
      int portIdx = node->outputPorts().indexOf(port);
      return outputDotPos(node, portIdx, item.rect);
    }
  }
  return QPoint();
}

QPoint PipelineStripWidget::inputPortPos(InputPort* port) const
{
  auto* node = port->node();
  for (auto& item : m_layout) {
    if (item.type == LayoutItem::NodeCard && item.node == node) {
      int portIdx = node->inputPorts().indexOf(port);
      return inputDotPos(node, portIdx, item.rect);
    }
  }
  return QPoint();
}

bool PipelineStripWidget::isPortCardVisible(OutputPort* port) const
{
  for (auto& item : m_layout) {
    if (item.type == LayoutItem::PortCard && item.port == port) {
      return true;
    }
  }
  return false;
}

void PipelineStripWidget::paintInputDots(QPainter& painter,
                                         const LayoutItem& item)
{
  auto* node = item.node;
  auto inputs = node->inputPorts();
  if (inputs.isEmpty()) {
    return;
  }

  bool nodeDim = m_dimmedNodes.contains(node);
  for (int i = 0; i < inputs.size(); ++i) {
    auto* inPort = inputs[i];
    QColor color;
    bool validLink = inPort->link() && inPort->link()->isValid();
    if (validLink) {
      color = portTypeColor(inPort->link()->from());
    } else {
      // Use the first accepted type's color for unconnected inputs
      PortTypes accepted = inPort->acceptedTypes();
      PortType primaryType = PortType::None;
      for (auto t : { PortType::ImageData, PortType::TiltSeries,
                      PortType::Volume, PortType::Image, PortType::Scalar,
                      PortType::Array, PortType::Table, PortType::Molecule }) {
        if (accepted.testFlag(t)) {
          primaryType = t;
          break;
        }
      }
      color = portTypeColor(primaryType);
    }
    if (nodeDim) {
      color = dimmed(color);
    }
    QPoint pos = inputDotPos(node, i, item.rect);
    bool invalidLink = inPort->link() && !inPort->link()->isValid();
    if (invalidLink) {
      // Draw an "X" for invalid links
      painter.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap));
      painter.setBrush(Qt::NoBrush);
      int r = DotRadius;
      painter.drawLine(pos.x() - r, pos.y() - r, pos.x() + r, pos.y() + r);
      painter.drawLine(pos.x() - r, pos.y() + r, pos.x() + r, pos.y() - r);
    } else {
      // Input dots filled with app background, stroke with port color
      painter.setBrush(palette().window());
      painter.setPen(QPen(color, 1.5));
      painter.drawEllipse(pos, DotRadius, DotRadius);
    }
  }
  painter.setBrush(Qt::NoBrush);
}

void PipelineStripWidget::paintOutputDots(QPainter& painter,
                                          const LayoutItem& item)
{
  auto* node = item.node;
  auto outputs = node->outputPorts();
  if (outputs.isEmpty()) {
    return;
  }

  // Don't draw output dots on the node if ports are shown as cards
  if (m_expandedNodes.contains(node)) {
    return;
  }

  int half = OutputSquareEdge / 2;
  for (int i = 0; i < outputs.size(); ++i) {
    QColor color = portTypeColor(outputs[i]);
    if (m_dimmedPorts.contains(outputs[i])) {
      color = dimmed(color);
    }
    bool isSelected = (m_selectedPort == outputs[i]);
    bool isTip = (m_tipOutputPort == outputs[i]);
    QPoint pos = outputDotPos(node, i, item.rect);

    // Outlined rounded square centered on pos
    QRect sqRect(pos.x() - half, pos.y() - half,
                 OutputSquareEdge, OutputSquareEdge);

    if (isSelected) {
      // Selection outline (drawn first, behind the port square)
      painter.setBrush(palette().window());
      painter.setPen(QPen(palette().highlight().color(), 1.5));
      QRect selRect = sqRect.adjusted(-3, -3, 3, 3);
      painter.drawRoundedRect(selRect, OutputSquareRadius + 2,
                              OutputSquareRadius + 2);
      // Selected: filled with port color
      painter.setBrush(color);
      painter.setPen(QPen(color, 1.5));
      painter.drawRoundedRect(sqRect, OutputSquareRadius, OutputSquareRadius);
    } else if (isTip) {
      // Tip outline (drawn first, behind the port square)
      painter.setBrush(palette().window());
      painter.setPen(QPen(Qt::red, 1.5));
      QRect tipRect = sqRect.adjusted(-3, -3, 3, 3);
      painter.drawRoundedRect(tipRect, OutputSquareRadius + 2,
                              OutputSquareRadius + 2);
      // Normal: outlined with port color, window background
      painter.setBrush(palette().window());
      painter.setPen(QPen(color, 1.5));
      painter.drawRoundedRect(sqRect, OutputSquareRadius, OutputSquareRadius);
    } else {
      // Normal: outlined with port color, window background
      painter.setBrush(palette().window());
      painter.setPen(QPen(color, 1.5));
      painter.drawRoundedRect(sqRect, OutputSquareRadius, OutputSquareRadius);
    }

    // Port icon inside the square
    QIcon icon = portTypeIcon(outputs[i]);
    QRect iconRect(pos.x() - OutputSquareIconSize / 2,
                   pos.y() - OutputSquareIconSize / 2,
                   OutputSquareIconSize, OutputSquareIconSize);
    QColor iconColor = isSelected ? Qt::white : color;
    paintTintedIcon(painter, icon, iconRect, iconColor);
  }
  painter.setBrush(Qt::NoBrush);
}

void PipelineStripWidget::paintPortCardDot(QPainter& painter,
                                           const LayoutItem& item)
{
  // Port card dot is now rendered as part of paintPortCard (icon square).
  Q_UNUSED(painter);
  Q_UNUSED(item);
}

void PipelineStripWidget::computeLinkGeometries()
{
  m_linkGeometries.clear();

  if (!m_pipeline) {
    return;
  }

  // Build node order from layout for adjacency checks
  QList<Node*> nodeOrder;
  for (auto& item : m_layout) {
    if (item.type == LayoutItem::NodeCard) {
      nodeOrder.append(item.node);
    }
  }

  // Classify links as direct (straight line) or gutter-routed.
  auto links = m_pipeline->links();

  auto isDirect = [&](Link* link) -> bool {
    auto* srcNode = link->from()->node();
    auto* dstNode = link->to()->node();
    int srcIdx = nodeOrder.indexOf(srcNode);
    int dstIdx = nodeOrder.indexOf(dstNode);
    if (dstIdx != srcIdx + 1) {
      return false;
    }
    return !isPortCardVisible(link->from());
  };

  // Build compact gutter lane indices: only ports with gutter-routed links
  // get a lane, so direct-only ports don't waste lane slots.
  m_outputGutterLanes.clear();
  m_inputGutterLanes.clear();

  for (int ni = 0; ni < nodeOrder.size(); ++ni) {
    auto* node = nodeOrder[ni];
    Node* nextNode = (ni + 1 < nodeOrder.size()) ? nodeOrder[ni + 1] : nullptr;
    Node* prevNode = (ni > 0) ? nodeOrder[ni - 1] : nullptr;

    // Output ports: a port needs a gutter lane if any of its links are
    // not direct (i.e. destination is not the next adjacent node, or port
    // is shown as a card).
    int outLane = 0;
    for (auto* port : node->outputPorts()) {
      bool needsGutter = false;
      for (auto* link : port->links()) {
        if (!isDirect(link)) {
          needsGutter = true;
          break;
        }
      }
      if (needsGutter) {
        m_outputGutterLanes[port] = outLane++;
      }
    }

    // Input ports: a port needs a gutter lane if its link is not direct
    // (source is not the previous adjacent collapsed node).
    int inLane = 0;
    bool prevCollapsed = false;
    if (prevNode) {
      prevCollapsed = !prevNode->outputPorts().isEmpty() &&
                      !m_expandedNodes.contains(prevNode);
    }
    for (auto* port : node->inputPorts()) {
      bool needsGutter = false;
      if (port->link()) {
        Node* src = port->link()->from()->node();
        bool direct = prevCollapsed && src == prevNode;
        if (!direct) {
          needsGutter = true;
        }
      }
      if (needsGutter) {
        m_inputGutterLanes[port] = inLane++;
      }
    }
  }

  // Compute departure/approach Y for a gutter-routed link
  auto departY = [this](OutputPort* port) -> int {
    QPoint pt = outputPortPos(port);
    if (isPortCardVisible(port)) {
      return pt.y();
    }
    int lane = m_outputGutterLanes.value(port, 0);
    return pt.y() + SquareClearance + lane * LaneSpacing;
  };

  auto approachY = [this](InputPort* port) -> int {
    QPoint pt = inputPortPos(port);
    int lane = m_inputGutterLanes.value(port, 0);
    return pt.y() - DotClearance - lane * LaneSpacing;
  };

  // Assign gutter lanes using interval coloring
  struct GutterSpan
  {
    OutputPort* port;
    int minY;
    int maxY;
  };
  QList<GutterSpan> spans;
  QMap<OutputPort*, int> spanIndex;

  for (auto* link : links) {
    if (isDirect(link)) {
      continue;
    }
    auto* outPort = link->from();
    if (outputPortPos(outPort).isNull() ||
        inputPortPos(link->to()).isNull()) {
      continue;
    }

    int dY = departY(outPort);
    int aY = approachY(link->to());
    int segMinY = qMin(dY, aY);
    int segMaxY = qMax(dY, aY);

    if (spanIndex.contains(outPort)) {
      auto& span = spans[spanIndex[outPort]];
      span.minY = qMin(span.minY, segMinY);
      span.maxY = qMax(span.maxY, segMaxY);
    } else {
      spanIndex[outPort] = spans.size();
      spans.append({ outPort, segMinY, segMaxY });
    }
  }

  std::sort(spans.begin(), spans.end(),
            [](const GutterSpan& a, const GutterSpan& b) {
              return a.minY < b.minY;
            });

  QMap<OutputPort*, int> portLaneIndex;
  QList<QList<GutterSpan>> lanes;

  for (auto& span : spans) {
    int assignedLane = -1;
    for (int lane = 0; lane < lanes.size(); ++lane) {
      bool conflict = false;
      for (auto& existing : lanes[lane]) {
        if (span.minY <= existing.maxY && span.maxY >= existing.minY) {
          conflict = true;
          break;
        }
      }
      if (!conflict) {
        assignedLane = lane;
        break;
      }
    }
    if (assignedLane < 0) {
      assignedLane = lanes.size();
      lanes.append(QList<GutterSpan>());
    }
    lanes[assignedLane].append(span);
    portLaneIndex[span.port] = assignedLane;
  }

  m_gutterLaneCount = lanes.size();

  // Build link geometries using buildLinkPath
  for (auto* link : links) {
    auto* outPort = link->from();
    int gutterX = 0;
    if (!isDirect(link) && portLaneIndex.contains(outPort)) {
      int laneIdx = portLaneIndex[outPort];
      int maxLane = m_gutterLaneCount - 1;
      gutterX = GutterWidth - LaneSpacing / 2 - (maxLane - laneIdx) * LaneSpacing;
    }
    QPainterPath path = buildLinkPath(outPort, link->to(), gutterX);
    if (!path.isEmpty()) {
      m_linkGeometries.append(
        { link, path, portTypeColor(outPort), link->isValid() });
    }
  }
}

QPainterPath PipelineStripWidget::buildLinkPath(OutputPort* fromPort,
                                                InputPort* toPort,
                                                int gutterX) const
{
  QPoint srcPt = outputPortPos(fromPort);
  QPoint dstPt = inputPortPos(toPort);
  if (srcPt.isNull() || dstPt.isNull()) {
    return QPainterPath();
  }

  bool portCard = isPortCardVisible(fromPort);

  // Check if direct (adjacent nodes, output not expanded to port cards)
  bool direct = false;
  if (!portCard) {
    auto* srcNode = fromPort->node();
    auto* dstNode = toPort->node();
    QList<Node*> nodeOrder;
    for (auto& item : m_layout) {
      if (item.type == LayoutItem::NodeCard) {
        nodeOrder.append(item.node);
      }
    }
    int srcOrderIdx = nodeOrder.indexOf(srcNode);
    int dstOrderIdx = nodeOrder.indexOf(dstNode);
    direct = (dstOrderIdx == srcOrderIdx + 1);
  }

  if (direct) {
    QPainterPath path;
    path.moveTo(srcPt);
    path.lineTo(dstPt);
    return path;
  }

  int outLane = m_outputGutterLanes.value(fromPort, 0);
  int dY = portCard ? srcPt.y()
                    : srcPt.y() + SquareClearance + outLane * LaneSpacing;
  int inLane = m_inputGutterLanes.value(toPort, 0);
  int aY = dstPt.y() - DotClearance - inLane * LaneSpacing;

  QList<QPointF> pts;
  pts.append(srcPt);
  if (dY != srcPt.y()) {
    pts.append(QPointF(srcPt.x(), dY));
  }
  pts.append(QPointF(gutterX, dY));
  pts.append(QPointF(gutterX, aY));
  pts.append(QPointF(dstPt.x(), aY));
  if (aY != dstPt.y()) {
    pts.append(QPointF(dstPt));
  }
  return roundedPolyline(pts, LinkCornerRadius);
}

void PipelineStripWidget::paintConnections(QPainter& painter)
{
  painter.setBrush(Qt::NoBrush);

  for (auto& lg : m_linkGeometries) {
    bool hovered = (lg.link == m_hoveredLink);
    bool selected = (lg.link == m_selectedLink);
    bool isDim = m_dimmedLinks.contains(lg.link);
    qreal baseWidth = 3.0;

    QColor linkColor = isDim ? dimmed(lg.color) : lg.color;

    // Selected: draw a thin outline around the link using the selection color,
    // with a 1px gap between the outline and the link segment.
    // Layers (bottom to top): selection outline, background gap, link line.
    if (selected) {
      QColor selColor = palette().highlight().color();
      // 1) Selection outline (outermost)
      painter.setPen(QPen(selColor, baseWidth + 6.0, Qt::SolidLine,
                          Qt::RoundCap, Qt::RoundJoin));
      painter.drawPath(lg.path);
      // 2) Background-colored gap to separate outline from link
      painter.setPen(QPen(palette().window().color(), baseWidth + 4.0,
                          Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.drawPath(lg.path);
    }

    // Draw the link line (hovered = +1px width)
    qreal strokeWidth = hovered ? baseWidth + 2.0 : baseWidth;
    painter.setPen(QPen(linkColor, strokeWidth, Qt::SolidLine,
                        Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(lg.path);
  }
}

OutputPort* PipelineStripWidget::outputPortHitTest(const QPoint& pos) const
{
  int half = OutputSquareEdge / 2;
  int margin = 2; // extra hit margin
  for (auto& item : m_layout) {
    if (item.type == LayoutItem::NodeCard &&
        !m_expandedNodes.contains(item.node)) {
      auto outs = item.node->outputPorts();
      for (int i = 0; i < outs.size(); ++i) {
        QPoint center = outputDotPos(item.node, i, item.rect);
        QRect hitRect(center.x() - half - margin, center.y() - half - margin,
                      OutputSquareEdge + 2 * margin,
                      OutputSquareEdge + 2 * margin);
        if (hitRect.contains(pos)) {
          return outs[i];
        }
      }
    } else if (item.type == LayoutItem::PortCard) {
      QPoint dotPos(item.rect.left(), item.rect.center().y());
      int dx = pos.x() - dotPos.x();
      int dy = pos.y() - dotPos.y();
      if (dx * dx + dy * dy <= (DotRadius + 2) * (DotRadius + 2)) {
        return item.port;
      }
    }
  }
  return nullptr;
}

InputPort* PipelineStripWidget::inputPortHitTest(const QPoint& pos) const
{
  for (auto& item : m_layout) {
    if (item.type != LayoutItem::NodeCard) {
      continue;
    }
    auto inputs = item.node->inputPorts();
    for (int i = 0; i < inputs.size(); ++i) {
      QPoint dotPos = inputDotPos(item.node, i, item.rect);
      int dx = pos.x() - dotPos.x();
      int dy = pos.y() - dotPos.y();
      if (dx * dx + dy * dy <= (DotRadius + 3) * (DotRadius + 3)) {
        return inputs[i];
      }
    }
  }
  return nullptr;
}

void PipelineStripWidget::paintPendingLink(QPainter& painter)
{
  if (!m_draggingLink || !m_dragFromPort) {
    return;
  }

  QColor lineColor = portTypeColor(m_dragFromPort);
  painter.setBrush(Qt::NoBrush);

  if (m_dragToPort) {
    // Valid target — reuse the same path logic as real links
    int gutterX = GutterWidth - LaneSpacing / 2 -
                  m_gutterLaneCount * LaneSpacing;
    gutterX = qMax(gutterX, 2);

    QPainterPath path = buildLinkPath(m_dragFromPort, m_dragToPort, gutterX);
    if (!path.isEmpty()) {
      painter.setPen(QPen(lineColor, 3.0, Qt::SolidLine,
                          Qt::RoundCap, Qt::RoundJoin));
      painter.drawPath(path);
    }
  } else {
    // No valid target — draw from output to gutter, ending at mouse Y
    QPoint srcPt = outputPortPos(m_dragFromPort);
    if (srcPt.isNull()) {
      return;
    }

    bool portCard = isPortCardVisible(m_dragFromPort);
    int outLane = m_outputGutterLanes.value(m_dragFromPort, 0);
    int departY = portCard ? srcPt.y()
                           : srcPt.y() + SquareClearance + outLane * LaneSpacing;

    int gutterX = GutterWidth - LaneSpacing / 2 -
                  m_gutterLaneCount * LaneSpacing;
    gutterX = qMax(gutterX, 2);

    QList<QPointF> pts;
    pts.append(srcPt);
    if (departY != srcPt.y()) {
      pts.append(QPointF(srcPt.x(), departY));
    }
    pts.append(QPointF(gutterX, departY));
    pts.append(QPointF(gutterX, m_dragCurrentPos.y()));

    painter.setPen(QPen(lineColor, 3.0, Qt::DashLine,
                        Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(roundedPolyline(pts, LinkCornerRadius));
  }
}

QColor PipelineStripWidget::badgeColor(Node* node) const
{
  if (qobject_cast<SourceNode*>(node)) {
    return QColor(76, 175, 80); // green
  }
  if (qobject_cast<TransformNode*>(node)) {
    return QColor(33, 150, 243); // blue
  }
  if (qobject_cast<SinkNode*>(node)) {
    return QColor(255, 152, 0); // orange
  }
  return QColor(158, 158, 158); // gray
}

QIcon PipelineStripWidget::portTypeIcon(OutputPort* port) const
{
  if (!port) {
    return QIcon(QStringLiteral(":/icons/pqInspect.png"));
  }
  switch (port->type()) {
    case PortType::TiltSeries:
      return QIcon(QStringLiteral(":/pipeline/port_tiltseries.svg"));
    case PortType::ImageData:
    case PortType::Volume:
      return QIcon(QStringLiteral(":/pipeline/port_imagedata.svg"));
    case PortType::Table:
      return QIcon(QStringLiteral(":/pipeline/port_table.svg"));
    case PortType::Molecule:
      return QIcon(QStringLiteral(":/pipeline/port_molecule.svg"));
    default:
      return QIcon(QStringLiteral(":/icons/pqInspect.png"));
  }
}

QColor PipelineStripWidget::portTypeColor(PortType type) const
{
  // Port type colors are chosen to be distinct from node badge colors
  // (Source=green, Transform=blue, Sink=orange).
  switch (type) {
    case PortType::ImageData:
      return QColor(158, 118, 47); // amber (generic volume data)
    case PortType::TiltSeries:
      return QColor(57, 73, 171);  // indigo (tilt series)
    case PortType::Volume:
      return QColor(171, 71, 188); // orchid (reconstructed volume)
    case PortType::Table:
      return QColor(0, 172, 172); // teal
    case PortType::Molecule:
      return QColor(194, 60, 108); // rose
    case PortType::Image:
      return QColor(120, 144, 56); // olive
    default:
      return QColor(158, 158, 158); // gray
  }
}

QColor PipelineStripWidget::portTypeColor(OutputPort* port) const
{
  if (!port) {
    return QColor(158, 158, 158);
  }
  return portTypeColor(port->type());
}

QColor PipelineStripWidget::dimmed(const QColor& color) const
{
  QColor bg = palette().window().color();
  qreal f = m_dimLevel;
  return QColor::fromRgbF(color.redF() * (1 - f) + bg.redF() * f,
                          color.greenF() * (1 - f) + bg.greenF() * f,
                          color.blueF() * (1 - f) + bg.blueF() * f,
                          color.alphaF() * (1 - f) + bg.alphaF() * f);
}

QIcon PipelineStripWidget::stateIcon(Node* node) const
{
  // Editing takes priority over everything else
  if (node->isEditing()) {
    return QIcon(QStringLiteral(":/pipeline/edit.png"));
  }

  // Non-idle execution state takes priority over data state
  switch (node->execState()) {
    case NodeExecState::Running:
      return QIcon(QStringLiteral(":/pipeline/spinner.png"));
    case NodeExecState::Failed:
      return QIcon(QStringLiteral(":/pipeline/error.png"));
    case NodeExecState::Canceled:
      return QIcon(QStringLiteral(":/pipeline/canceled.png"));
    case NodeExecState::Idle:
      break;
  }

  // Data state
  switch (node->state()) {
    case NodeState::Current:
      return QIcon(QStringLiteral(":/pipeline/check.png"));
    case NodeState::Stale:
      return QIcon(QStringLiteral(":/pipeline/question.png"));
    case NodeState::New:
    default:
      return QIcon();
  }
}

QRect PipelineStripWidget::breakpointRect(const QRect& cardRect) const
{
  // Layout: [breakpoint] [state] [menu] | [expand/action]
  int toggleX = cardRect.right() - HeaderExpandWidth - HeaderRightPad;
  int menuX = toggleX - HeaderButtonGap - HeaderIconSize;
  int stateX = menuX - HeaderButtonSpacing - HeaderIconSize;
  int bpX = stateX - HeaderButtonSpacing - HeaderIconSize;
  int bpY = cardRect.top() + (NodeCardHeight - HeaderIconSize) / 2;
  return QRect(bpX, bpY, HeaderIconSize, HeaderIconSize);
}

QRect PipelineStripWidget::menuButtonRect(const QRect& cardRect) const
{
  // Layout: [breakpoint] [state] [menu] | [expand/action]
  int toggleX = cardRect.right() - HeaderExpandWidth - HeaderRightPad;
  int menuX = toggleX - HeaderButtonGap - HeaderIconSize;
  int menuY = cardRect.top() + (NodeCardHeight - HeaderIconSize) / 2;
  return QRect(menuX, menuY, HeaderIconSize, HeaderIconSize);
}

QRect PipelineStripWidget::actionButtonRect(const QRect& cardRect) const
{
  // Same position as the expand toggle
  int toggleX = cardRect.right() - HeaderExpandWidth - HeaderRightPad;
  int toggleY = cardRect.top() + (NodeCardHeight - HeaderIconSize) / 2;
  return QRect(toggleX, toggleY, HeaderExpandWidth, HeaderIconSize);
}

// --- Interaction ---

void PipelineStripWidget::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton) {
    // Check for output port dot press (potential link drag start)
    auto* outPort = outputPortHitTest(event->pos());
    if (outPort) {
      m_dragFromPort = outPort;
      m_dragStartPos = event->pos();
      m_dragCurrentPos = event->pos();
      m_draggingLink = false;
      m_dragToPort = nullptr;
      return;
    }

    int idx = hitTest(event->pos());

    // Check clicks on node card action areas
    if (idx >= 0 && m_layout[idx].type == LayoutItem::NodeCard) {
      auto* node = m_layout[idx].node;
      auto& cardRect = m_layout[idx].rect;

      // Breakpoint area
      QRect bpRect = breakpointRect(cardRect);
      if (bpRect.contains(event->pos())) {
        node->setBreakpoint(!node->hasBreakpoint());
        update();
        return;
      }

      // Menu button (three dots) — open context menu
      QRect menuRect = menuButtonRect(cardRect);
      if (menuRect.contains(event->pos())) {
        selectItem(idx);
        QPoint globalPos = mapToGlobal(
          QPoint(menuRect.right(), menuRect.bottom()));
        showContextMenu(globalPos);
        return;
      }

      // Expand toggle (rightmost area) or action button
      auto outputs = node->outputPorts();
      if (!outputs.isEmpty()) {
        int toggleX = cardRect.right() - HeaderExpandWidth - HeaderRightPad;
        if (event->pos().x() >= toggleX &&
            event->pos().x() <= cardRect.right() - HeaderRightPad) {
          setExpanded(node, !isExpanded(node));
          return;
        }
      } else if (!node->actionIcon().isNull()) {
        QRect actRect = actionButtonRect(cardRect);
        if (actRect.contains(event->pos())) {
          node->triggerAction();
          update();
          return;
        }
      }
    }

    if (idx >= 0) {
      selectItem(idx);
    } else {
      // No card hit — check links
      auto* link = linkHitTest(event->pos());
      if (link) {
        selectLink(link);
      } else {
        selectItem(-1);
      }
    }
  }
  QWidget::mousePressEvent(event);
}

void PipelineStripWidget::mouseReleaseEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton && m_dragFromPort) {
    if (m_draggingLink) {
      // Complete the drag — emit linkRequested if valid target
      if (m_dragToPort) {
        emit linkRequested(m_dragFromPort, m_dragToPort);
      }
    } else {
      // Was a click on output dot (no drag) — select it
      m_selectedIndex = -1;
      m_selectedPort = m_dragFromPort;
      m_selectedLink = nullptr;
      updateDimming();
      emit portSelected(m_dragFromPort);
    }
    m_dragFromPort = nullptr;
    m_dragToPort = nullptr;
    m_draggingLink = false;
    update();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void PipelineStripWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton) {
    int idx = hitTest(event->pos());
    if (idx >= 0 && m_layout[idx].type == LayoutItem::NodeCard) {
      emit nodeDoubleClicked(m_layout[idx].node);
    }
  }
  QWidget::mouseDoubleClickEvent(event);
}

void PipelineStripWidget::keyPressEvent(QKeyEvent* event)
{
  switch (event->key()) {
    case Qt::Key_Up:
      if (m_selectedIndex > 0) {
        selectItem(m_selectedIndex - 1);
      }
      break;
    case Qt::Key_Down:
      if (m_selectedIndex < m_layout.size() - 1) {
        selectItem(m_selectedIndex + 1);
      } else if (m_selectedIndex < 0 && !m_layout.isEmpty()) {
        selectItem(0);
      }
      break;
    case Qt::Key_Escape:
      if (m_draggingLink) {
        m_draggingLink = false;
        m_dragFromPort = nullptr;
        m_dragToPort = nullptr;
        update();
      }
      break;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
      // Show context menu at widget origin for the selected element
      showContextMenu(mapToGlobal(QPoint(0, 0)));
      break;
    default:
      QWidget::keyPressEvent(event);
      return;
  }
  event->accept();
}

void PipelineStripWidget::contextMenuEvent(QContextMenuEvent* event)
{
  int idx = hitTest(event->pos());

  if (idx >= 0) {
    auto& item = m_layout[idx];
    if (item.type == LayoutItem::PortCard && m_portMenuProvider) {
      selectItem(idx);
      QMenu menu(this);
      m_portMenuProvider(item.port, menu);
      if (!menu.isEmpty()) {
        menu.exec(event->globalPos());
      }
      return;
    }
    if (item.type == LayoutItem::NodeCard && m_nodeMenuProvider) {
      selectItem(idx);
      QMenu menu(this);
      m_nodeMenuProvider(item.node, menu);
      if (!menu.isEmpty()) {
        menu.exec(event->globalPos());
      }
      return;
    }
    return;
  }

  // No card hit — check links
  auto* link = linkHitTest(event->pos());
  if (link && m_linkMenuProvider) {
    selectLink(link);
    QMenu menu(this);
    m_linkMenuProvider(link, menu);
    if (!menu.isEmpty()) {
      menu.exec(event->globalPos());
    }
  }
}

void PipelineStripWidget::showContextMenu(const QPoint& globalPos)
{
  if (m_selectedLink && m_linkMenuProvider) {
    QMenu menu(this);
    m_linkMenuProvider(m_selectedLink, menu);
    if (!menu.isEmpty()) {
      menu.exec(globalPos);
    }
    return;
  }

  if (m_selectedIndex >= 0 && m_selectedIndex < m_layout.size()) {
    auto& item = m_layout[m_selectedIndex];
    if (item.type == LayoutItem::PortCard && m_portMenuProvider) {
      QMenu menu(this);
      m_portMenuProvider(item.port, menu);
      if (!menu.isEmpty()) {
        menu.exec(globalPos);
      }
      return;
    }
    if (item.type == LayoutItem::NodeCard && m_nodeMenuProvider) {
      QMenu menu(this);
      m_nodeMenuProvider(item.node, menu);
      if (!menu.isEmpty()) {
        menu.exec(globalPos);
      }
    }
  }
}

void PipelineStripWidget::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);
  rebuildLayout();
}

void PipelineStripWidget::mouseMoveEvent(QMouseEvent* event)
{
  // Handle link creation drag
  if (m_dragFromPort) {
    if (!m_draggingLink) {
      // Check drag threshold
      if ((event->pos() - m_dragStartPos).manhattanLength() >=
          QApplication::startDragDistance()) {
        m_draggingLink = true;
      }
    }
    if (m_draggingLink) {
      m_dragCurrentPos = event->pos();
      // Check if hovering a valid input port
      auto* inPort = inputPortHitTest(event->pos());
      if (inPort) {
        bool valid = !m_linkValidator ||
                     m_linkValidator(m_dragFromPort, inPort);
        m_dragToPort = valid ? inPort : nullptr;
      } else {
        m_dragToPort = nullptr;
      }
      update();
    }
    return;
  }

  int idx = hitTest(event->pos());
  // Only track hover on node cards (for breakpoint hint)
  if (idx >= 0 && m_layout[idx].type != LayoutItem::NodeCard) {
    // Find the parent node card for port cards
    for (int i = idx - 1; i >= 0; --i) {
      if (m_layout[i].type == LayoutItem::NodeCard) {
        idx = i;
        break;
      }
    }
  }

  // Track link hover (only when not hovering a card)
  Link* hoveredLink = nullptr;
  if (idx < 0) {
    hoveredLink = linkHitTest(event->pos());
  }

  bool needsUpdate = false;
  if (idx != m_hoveredIndex) {
    m_hoveredIndex = idx;
    needsUpdate = true;
  }
  if (hoveredLink != m_hoveredLink) {
    m_hoveredLink = hoveredLink;
    needsUpdate = true;
  }
  if (needsUpdate) {
    update();
  }
  QWidget::mouseMoveEvent(event);
}

void PipelineStripWidget::leaveEvent(QEvent* event)
{
  bool needsUpdate = false;
  if (m_hoveredIndex >= 0) {
    m_hoveredIndex = -1;
    needsUpdate = true;
  }
  if (m_hoveredLink) {
    m_hoveredLink = nullptr;

    needsUpdate = true;
  }
  if (needsUpdate) {
    update();
  }
  QWidget::leaveEvent(event);
}

} // namespace pipeline
} // namespace tomviz
