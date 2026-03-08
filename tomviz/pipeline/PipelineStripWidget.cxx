/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "PipelineStripWidget.h"

#include "InputPort.h"
#include "Link.h"
#include "Node.h"
#include "OutputPort.h"
#include "Pipeline.h"
#include "PortType.h"
#include "SinkNode.h"
#include "SourceNode.h"
#include "TransformNode.h"

#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QStyleOption>

namespace tomviz {
namespace pipeline {

PipelineStripWidget::PipelineStripWidget(QWidget* parent) : QWidget(parent)
{
  setFocusPolicy(Qt::StrongFocus);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

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
  m_expandedNodes.clear();
  connectPipeline();
  rebuildLayout();
  update();
}

Pipeline* PipelineStripWidget::pipeline() const
{
  return m_pipeline;
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
  m_selectedIndex = -1;
  m_selectedPort = nullptr;

  if (!m_pipeline) {
    updateGeometry();
    return;
  }

  auto sorted = m_pipeline->topologicalSort();
  int y = Padding;
  int cardWidth = qMax(width() - GutterWidth - Padding * 2, 80);

  bool anyExecuting = false;

  Node* prevNode = nullptr;
  bool prevCollapsed = false;

  for (int ni = 0; ni < sorted.size(); ++ni) {
    auto* node = sorted[ni];
    auto* nextNode = (ni + 1 < sorted.size()) ? sorted[ni + 1] : nullptr;

    // Extra top margin for input ports that are NOT direct connections.
    // A "direct" input is one connected from the previous adjacent node
    // whose output ports are collapsed — those get straight lines.
    int maxNonDirectInputIdx = -1;
    auto inputs = node->inputPorts();
    for (int i = 0; i < inputs.size(); ++i) {
      if (!inputs[i]->link()) {
        continue;
      }
      Node* src = inputs[i]->link()->from()->node();
      bool isDirect = prevCollapsed && src == prevNode;
      if (!isDirect) {
        maxNonDirectInputIdx = i;
      }
    }
    if (maxNonDirectInputIdx >= 0) {
      y += DotClearance + maxNonDirectInputIdx * LaneSpacing;
    }

    // Port sub-cards (shown only when node is expanded)
    auto outputs = node->outputPorts();
    bool showPorts =
      !outputs.isEmpty() && m_expandedNodes.contains(node);

    // Compute node card height: base height + port cards inside when expanded
    int nodeHeight = NodeCardHeight;
    if (showPorts) {
      nodeHeight += CardSpacing; // top padding below header
      nodeHeight += outputs.size() * PortCardHeight;
      nodeHeight += (outputs.size() - 1) * CardSpacing;
      nodeHeight += PortIndent; // bottom padding
    }

    // Node card
    LayoutItem nodeItem;
    nodeItem.type = LayoutItem::NodeCard;
    nodeItem.node = node;
    nodeItem.rect = QRect(GutterWidth + Padding, y, cardWidth, nodeHeight);
    m_layout.append(nodeItem);

    if (showPorts) {
      int portY = y + NodeCardHeight + CardSpacing;
      for (auto* port : outputs) {
        LayoutItem portItem;
        portItem.type = LayoutItem::PortCard;
        portItem.node = node;
        portItem.port = port;
        portItem.rect = QRect(GutterWidth + Padding + PortIndent, portY,
                              cardWidth - 2 * PortIndent, PortCardHeight);
        m_layout.append(portItem);
        portY += PortCardHeight + CardSpacing;
      }
    }
    y += nodeHeight + CardSpacing;

    // Extra bottom margin for collapsed outputs with non-direct links.
    // Direct outputs (all links go to the next adjacent node) get
    // straight lines and don't need gutter clearance.
    if (!showPorts) {
      int maxNonDirectOutputIdx = -1;
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
        if (!allDirect) {
          maxNonDirectOutputIdx = i;
        }
      }
      if (maxNonDirectOutputIdx >= 0) {
        y += DotClearance + maxNonDirectOutputIdx * LaneSpacing;
      } else if (!outputs.isEmpty()) {
        // All outputs are direct — use wider spacing for the straight
        // lines, but only if the next node also has no non-direct inputs
        // (otherwise the approach margin already provides enough space).
        bool nextHasNonDirect = false;
        if (nextNode) {
          auto nextInputs = nextNode->inputPorts();
          for (auto* inp : nextInputs) {
            if (inp->link() && inp->link()->from()->node() != node) {
              nextHasNonDirect = true;
              break;
            }
          }
        }
        if (!nextHasNonDirect) {
          y += DirectConnectionSpacing - ConnectionSpacing;
        }
      }
    }

    y += ConnectionSpacing;

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

  updateGeometry();
}

void PipelineStripWidget::connectPipeline()
{
  if (!m_pipeline) {
    return;
  }

  connect(m_pipeline, &Pipeline::nodeAdded, this, [this](Node* node) {
    connect(node, &Node::stateChanged, this,
            QOverload<>::of(&QWidget::update));
    connect(node, &Node::labelChanged, this,
            QOverload<>::of(&QWidget::update));
    connect(node, &Node::breakpointChanged, this,
            QOverload<>::of(&QWidget::update));
    connect(node, &Node::executionStarted, this, [this]() {
      if (!m_spinnerTimer.isActive()) {
        m_spinnerTimer.start();
      }
    });
    connect(node, &Node::executionFinished, this, [this](bool) {
      // Stop spinner if no nodes are executing
      // For simplicity, just keep it running and repaint
      // A more precise approach would track executing nodes
      m_spinnerTimer.stop();
      update();
    });
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

  connect(m_pipeline, &Pipeline::linkCreated, this, [this](Link*) {
    rebuildLayout();
    update();
  });

  connect(m_pipeline, &Pipeline::linkRemoved, this, [this](Link*) {
    rebuildLayout();
    update();
  });

  // Connect existing nodes
  for (auto* node : m_pipeline->nodes()) {
    connect(node, &Node::stateChanged, this,
            QOverload<>::of(&QWidget::update));
    connect(node, &Node::labelChanged, this,
            QOverload<>::of(&QWidget::update));
    connect(node, &Node::breakpointChanged, this,
            QOverload<>::of(&QWidget::update));
    connect(node, &Node::executionStarted, this, [this]() {
      if (!m_spinnerTimer.isActive()) {
        m_spinnerTimer.start();
      }
    });
    connect(node, &Node::executionFinished, this, [this](bool) {
      m_spinnerTimer.stop();
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
  if (index == m_selectedIndex && !m_selectedPort) {
    return;
  }
  m_selectedIndex = index;
  m_selectedPort = nullptr;
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

// --- Painting ---

void PipelineStripWidget::paintEvent(QPaintEvent* event)
{
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  // Paint connections first (behind cards)
  paintConnections(painter);

  // Paint cards
  for (int i = 0; i < m_layout.size(); ++i) {
    bool selected = (i == m_selectedIndex);
    bool hovered = false; // TODO: track hover state
    auto& item = m_layout[i];

    if (item.type == LayoutItem::NodeCard) {
      paintNodeCard(painter, item, selected, hovered);
      paintInputDots(painter, item);
      paintOutputDots(painter, item);
    } else {
      paintPortCard(painter, item, selected, hovered);
      paintPortCardDot(painter, item);
    }
  }
}

void PipelineStripWidget::paintNodeCard(QPainter& painter,
                                        const LayoutItem& item, bool selected,
                                        bool hovered)
{
  Q_UNUSED(hovered);
  auto r = item.rect;
  auto* node = item.node;

  // Node-type colored border; fill only when selected
  QColor fg = selected ? palette().highlightedText().color()
                       : palette().buttonText().color();
  QColor borderColor = badgeColor(node);

  QPainterPath path;
  path.addRoundedRect(QRectF(r), CardRadius, CardRadius);
  if (selected) {
    // Only fill the header row, not the full expanded area
    QRectF headerRect(r.left(), r.top(), r.width(), NodeCardHeight);
    QPainterPath headerPath;
    headerPath.addRoundedRect(headerRect, CardRadius, CardRadius);
    painter.fillPath(headerPath, palette().highlight());
  }
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(borderColor, 1.5));
  painter.drawPath(path);

  int x = r.left() + 6;
  int cy = r.top() + NodeCardHeight / 2; // header row center

  // Badge
  QColor badge = badgeColor(node);
  painter.setBrush(badge);
  painter.setPen(Qt::NoPen);
  painter.drawEllipse(QPoint(x + BadgeSize / 2, cy), BadgeSize / 2,
                      BadgeSize / 2);

  // Badge text
  QFont badgeFont = font();
  badgeFont.setPixelSize(8);
  badgeFont.setBold(true);
  painter.setFont(badgeFont);
  painter.setPen(Qt::white);
  painter.drawText(QRect(x, cy - BadgeSize / 2, BadgeSize, BadgeSize),
                   Qt::AlignCenter, badgeText(node));

  x += BadgeSize + 6;

  // Label
  QFont labelFont = font();
  labelFont.setPixelSize(12);
  painter.setFont(labelFont);
  painter.setPen(fg);
  QFontMetrics fm(labelFont);

  // Calculate available width for label
  int rightReserved = 40; // space for expand toggle + state
  int labelWidth = r.right() - rightReserved - x;
  QString elidedLabel =
    fm.elidedText(node->label(), Qt::ElideRight, labelWidth);
  int headerHeight = NodeCardHeight;
  painter.drawText(QRect(x, r.top(), labelWidth, headerHeight),
                   Qt::AlignVCenter | Qt::AlignLeft, elidedLabel);

  // Expand toggle for nodes with output ports (uses native style arrows)
  auto outputs = node->outputPorts();
  int toggleX = r.right() - 36;
  if (!outputs.isEmpty()) {
    bool expanded = m_expandedNodes.contains(node);
    QStyleOption opt;
    opt.initFrom(this);
    opt.rect = QRect(toggleX, r.top() + (headerHeight - 12) / 2, 12, 12);
    opt.state |= QStyle::State_Children;
    if (expanded) {
      opt.state |= QStyle::State_Open;
    }
    if (selected) {
      opt.state |= QStyle::State_Selected;
    }
    style()->drawPrimitive(QStyle::PE_IndicatorBranch, &opt, &painter, this);
  }

  // State indicator
  int stateX = r.right() - 18;
  QString st = stateText(node);
  QFont stateFont = font();
  stateFont.setPixelSize(10);
  painter.setFont(stateFont);

  if (m_spinnerTimer.isActive() && node->state() == NodeState::Stale) {
    // Draw spinner arc
    painter.setPen(QPen(fg, 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(QRect(stateX, cy - 5, 10, 10), m_spinnerAngle * 16,
                    270 * 16);
  } else {
    painter.setPen(fg);
    painter.drawText(QRect(stateX - 2, r.top(), 16, headerHeight),
                     Qt::AlignCenter, st);
  }

  // Breakpoint indicator
  if (node->hasBreakpoint()) {
    painter.setBrush(QColor(220, 50, 50));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPoint(r.left() - 8, cy), 4, 4);
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

  // Port-type colored border; fill only when selected
  QColor fg = selected ? palette().highlightedText().color()
                       : palette().text().color();
  QColor borderColor = portTypeColor(port);

  QPainterPath path;
  path.addRoundedRect(QRectF(r), CardRadius - 1, CardRadius - 1);
  if (selected) {
    painter.fillPath(path, palette().highlight());
  }
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(borderColor, 1.0));
  painter.drawPath(path);

  int x = r.left() + 6;

  // Port name
  QFont portFont = font();
  portFont.setPixelSize(11);
  painter.setFont(portFont);
  painter.setPen(fg);
  QFontMetrics fm(portFont);
  QString name = fm.elidedText(port->name(), Qt::ElideRight, r.width() - 24);
  painter.drawText(QRect(x, r.top(), r.width() - x + r.left(), r.height()),
                   Qt::AlignVCenter | Qt::AlignLeft, name);
}

QPoint PipelineStripWidget::inputDotPos(Node* node, int portIndex,
                                        const QRect& nodeRect) const
{
  auto inputs = node->inputPorts();
  int y = nodeRect.top();
  int startX = nodeRect.left() + PortIndent;
  if (inputs.size() <= 1) {
    return QPoint(startX, y);
  }
  return QPoint(startX + portIndex * DotSpacing, y);
}

QPoint PipelineStripWidget::outputDotPos(Node* node, int portIndex,
                                         const QRect& nodeRect) const
{
  auto outputs = node->outputPorts();
  int y = nodeRect.bottom();
  int startX = nodeRect.left() + PortIndent;
  if (outputs.size() <= 1) {
    return QPoint(startX, y);
  }
  return QPoint(startX + portIndex * DotSpacing, y);
}

void PipelineStripWidget::paintInputDots(QPainter& painter,
                                         const LayoutItem& item)
{
  auto* node = item.node;
  auto inputs = node->inputPorts();
  if (inputs.isEmpty()) {
    return;
  }

  for (int i = 0; i < inputs.size(); ++i) {
    auto* inPort = inputs[i];
    QColor color(120, 120, 120);
    if (inPort->link()) {
      color = portTypeColor(inPort->link()->from());
    }
    // Input dots filled with app background, stroke with port color
    painter.setBrush(palette().window());
    painter.setPen(QPen(color, 1.5));
    QPoint pos = inputDotPos(node, i, item.rect);
    painter.drawEllipse(pos, DotRadius, DotRadius);
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

  for (int i = 0; i < outputs.size(); ++i) {
    QColor color = portTypeColor(outputs[i]);
    bool isSelected = (m_selectedPort == outputs[i]);
    // Output dots are filled; draw larger with highlight ring when selected
    painter.setBrush(color);
    if (isSelected) {
      painter.setPen(QPen(palette().highlight().color(), 2.0));
      painter.drawEllipse(outputDotPos(node, i, item.rect),
                          DotRadius + 2, DotRadius + 2);
    } else {
      painter.setPen(Qt::NoPen);
      painter.drawEllipse(outputDotPos(node, i, item.rect),
                          DotRadius, DotRadius);
    }
  }
  painter.setBrush(Qt::NoBrush);
}

void PipelineStripWidget::paintPortCardDot(QPainter& painter,
                                           const LayoutItem& item)
{
  auto* port = item.port;
  QColor color = portTypeColor(port);
  // Port card dots are filled (output)
  painter.setBrush(color);
  painter.setPen(Qt::NoPen);
  QPoint pos(item.rect.left(), item.rect.center().y());
  painter.drawEllipse(pos, DotRadius, DotRadius);
  painter.setBrush(Qt::NoBrush);
}

void PipelineStripWidget::paintConnections(QPainter& painter)
{
  if (!m_pipeline) {
    return;
  }

  // Build lookup maps
  QHash<Node*, int> nodeCardIndex;
  QHash<OutputPort*, int> portCardIndex;

  for (int i = 0; i < m_layout.size(); ++i) {
    auto& item = m_layout[i];
    if (item.type == LayoutItem::NodeCard) {
      nodeCardIndex[item.node] = i;
    } else if (item.type == LayoutItem::PortCard) {
      portCardIndex[item.port] = i;
    }
  }

  // Build node order from layout for adjacency checks
  QList<Node*> nodeOrder;
  for (auto& item : m_layout) {
    if (item.type == LayoutItem::NodeCard) {
      nodeOrder.append(item.node);
    }
  }

  // Classify links as direct (straight line) or gutter-routed.
  // A link is direct when:
  // - source and destination nodes are adjacent in layout order
  // - source node's output ports are NOT expanded into port cards
  auto links = m_pipeline->links();

  auto isDirect = [&](Link* link) -> bool {
    auto* srcNode = link->from()->node();
    auto* dstNode = link->to()->node();
    int srcIdx = nodeOrder.indexOf(srcNode);
    int dstIdx = nodeOrder.indexOf(dstNode);
    if (dstIdx != srcIdx + 1) {
      return false;
    }
    // Check source has no expanded port cards
    for (auto* op : srcNode->outputPorts()) {
      if (portCardIndex.contains(op)) {
        return false;
      }
    }
    return true;
  };

  // Assign gutter lanes per unique output port (links from the same port share
  // a lane). First pass: assign lane indices to each gutter-routed output port.
  QMap<OutputPort*, int> portLaneIndex;
  int gutterLaneCount = 0;
  for (auto* link : links) {
    if (!isDirect(link) && !portLaneIndex.contains(link->from())) {
      portLaneIndex[link->from()] = gutterLaneCount++;
    }
  }

  int gutterCenter = GutterWidth / 2;

  for (auto* link : links) {
    auto* outPort = link->from();
    auto* inPort = link->to();
    auto* srcNode = outPort->node();
    auto* dstNode = inPort->node();

    if (!nodeCardIndex.contains(srcNode) ||
        !nodeCardIndex.contains(dstNode)) {
      continue;
    }

    auto& srcNodeItem = m_layout[nodeCardIndex[srcNode]];
    auto& dstNodeItem = m_layout[nodeCardIndex[dstNode]];

    // Source point: port card left dot if expanded, else output dot on bottom
    QPoint srcPt;
    if (portCardIndex.contains(outPort)) {
      auto& portItem = m_layout[portCardIndex[outPort]];
      srcPt = QPoint(portItem.rect.left(), portItem.rect.center().y());
    } else {
      int portIdx = srcNode->outputPorts().indexOf(outPort);
      srcPt = outputDotPos(srcNode, portIdx, srcNodeItem.rect);
    }

    // Destination point: input dot on top of node card
    int inIdx = dstNode->inputPorts().indexOf(inPort);
    QPoint dstPt = inputDotPos(dstNode, inIdx, dstNodeItem.rect);

    QColor lineColor = portTypeColor(outPort);
    painter.setPen(QPen(lineColor, 1.5));
    painter.setBrush(Qt::NoBrush);

    if (isDirect(link)) {
      // Straight line between adjacent nodes
      QPainterPath path;
      path.moveTo(srcPt);
      path.lineTo(dstPt);
      painter.drawPath(path);
    } else {
      // Route through gutter — lane shared by all links from the same port
      int laneIdx = portLaneIndex[outPort];
      int gutterX = gutterCenter +
        (2 * laneIdx - gutterLaneCount + 1) * LaneSpacing / 2;

      // Departure Y: offset below the output dot
      int outIdx = srcNode->outputPorts().indexOf(outPort);
      int departY = srcPt.y() + DotClearance + outIdx * LaneSpacing;
      // For expanded port cards depart at dot Y
      if (portCardIndex.contains(outPort)) {
        departY = srcPt.y();
      }

      // Approach Y: offset above the input dot
      int approachY = dstPt.y() - DotClearance - inIdx * LaneSpacing;

      QPainterPath path;
      path.moveTo(srcPt);
      if (departY != srcPt.y()) {
        path.lineTo(srcPt.x(), departY);
      }
      path.lineTo(gutterX, departY);
      path.lineTo(gutterX, approachY);
      path.lineTo(dstPt.x(), approachY);
      if (approachY != dstPt.y()) {
        path.lineTo(dstPt);
      }
      painter.drawPath(path);
    }
  }
  painter.setBrush(Qt::NoBrush);
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

QString PipelineStripWidget::badgeText(Node* node) const
{
  if (qobject_cast<SourceNode*>(node)) {
    return QStringLiteral("S");
  }
  if (qobject_cast<TransformNode*>(node)) {
    return QStringLiteral("T");
  }
  if (qobject_cast<SinkNode*>(node)) {
    return QStringLiteral("K");
  }
  return QStringLiteral("?");
}

QColor PipelineStripWidget::portTypeColor(OutputPort* port) const
{
  if (!port) {
    return QColor(158, 158, 158);
  }
  // Port type colors are chosen to be distinct from node badge colors
  // (Source=green, Transform=blue, Sink=orange).
  switch (port->type()) {
    case PortType::Volume:
      return QColor(121, 85, 196); // purple
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

QString PipelineStripWidget::stateText(Node* node) const
{
  switch (node->state()) {
    case NodeState::Current:
      return QStringLiteral("\u2713"); // checkmark
    case NodeState::Stale:
      return QStringLiteral("\u25CF"); // filled circle (yellow conceptually)
    case NodeState::New:
      return QStringLiteral("\u25CB"); // empty circle
    default:
      return QStringLiteral("?");
  }
}

// --- Interaction ---

void PipelineStripWidget::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton) {
    int idx = hitTest(event->pos());

    // Check for expand toggle click on node cards with output ports
    if (idx >= 0 && m_layout[idx].type == LayoutItem::NodeCard) {
      auto* node = m_layout[idx].node;
      auto outputs = node->outputPorts();
      if (!outputs.isEmpty()) {
        // Toggle area is near right side of card
        int toggleX = m_layout[idx].rect.right() - 36;
        if (event->pos().x() >= toggleX &&
            event->pos().x() <= toggleX + 16) {
          setExpanded(node, !isExpanded(node));
          return;
        }
      }
    }

    // Check for output dot click on collapsed node cards
    if (idx >= 0 && m_layout[idx].type == LayoutItem::NodeCard) {
      auto* node = m_layout[idx].node;
      if (!m_expandedNodes.contains(node)) {
        auto outputs = node->outputPorts();
        for (int i = 0; i < outputs.size(); ++i) {
          QPoint dotPos = outputDotPos(node, i, m_layout[idx].rect);
          int dx = event->pos().x() - dotPos.x();
          int dy = event->pos().y() - dotPos.y();
          if (dx * dx + dy * dy <= (DotRadius + 2) * (DotRadius + 2)) {
            m_selectedIndex = -1;
            m_selectedPort = outputs[i];
            update();
            emit portSelected(outputs[i]);
            return;
          }
        }
      }
    }

    // Check for breakpoint click in gutter area
    if (event->pos().x() < GutterWidth && idx < 0) {
      // Find closest node card
      for (int i = 0; i < m_layout.size(); ++i) {
        if (m_layout[i].type == LayoutItem::NodeCard) {
          auto r = m_layout[i].rect;
          if (event->pos().y() >= r.top() && event->pos().y() <= r.bottom()) {
            auto* node = m_layout[i].node;
            node->setBreakpoint(!node->hasBreakpoint());
            update();
            return;
          }
        }
      }
    }

    selectItem(idx);
  }
  QWidget::mousePressEvent(event);
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
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
      if (m_selectedIndex >= 0 && m_selectedIndex < m_layout.size()) {
        auto* node = m_layout[m_selectedIndex].node;
        // Emit context menu for deletion handling by the parent
        emit contextMenuRequested(node, mapToGlobal(QPoint(0, 0)));
      }
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
    emit contextMenuRequested(m_layout[idx].node, event->globalPos());
  }
}

} // namespace pipeline
} // namespace tomviz
