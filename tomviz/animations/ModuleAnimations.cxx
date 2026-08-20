/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include "ModuleAnimations.h"

#include "ClipAnimation.h"
#include "ContourAnimation.h"
#include "ModuleAnimation.h"
#include "OpacityAnimation.h"
#include "ScalarOpacityAnimation.h"
#include "SliceAnimation.h"

#include "pipeline/Pipeline.h"

#include <QDebug>
#include <QJsonArray>

namespace tomviz {

namespace {

// Rebuild one saved animation. Returns null for an entry naming a type
// we no longer have, or a node that cannot carry that animation.
ModuleAnimation* buildAnimation(const QString& type, pipeline::Node* node,
                                const QJsonObject& json)
{
  double start = json["start"].toDouble();
  double stop = json["stop"].toDouble();

  if (type == "contour") {
    if (auto* sink = qobject_cast<pipeline::ContourSink*>(node)) {
      return new ContourAnimation(sink, start, stop);
    }
  } else if (type == "slice") {
    if (auto* sink = qobject_cast<pipeline::SliceSink*>(node)) {
      return new SliceAnimation(sink, start, stop);
    }
  } else if (type == "clip") {
    if (auto* sink = qobject_cast<pipeline::ClipSink*>(node)) {
      auto unit = json["unit"].toString() == "distance"
                    ? ClipAnimation::Distance
                    : ClipAnimation::Slice;
      // A clip that changed orientation since it was saved cannot use
      // the saved unit: slice indices are meaningless once the plane is
      // off axis, and a distance sweep would be ignored while it is on
      // one. Drop it rather than move the plane somewhere arbitrary.
      if ((unit == ClipAnimation::Slice) != sink->isOrtho()) {
        qWarning() << "Dropping clip animation: the clip changed"
                   << "orientation since it was saved.";
        return nullptr;
      }
      return new ClipAnimation(sink, start, stop, unit);
    }
  } else if (type == "scalarOpacity") {
    if (auto* sink = qobject_cast<pipeline::VolumeSink*>(node)) {
      vtkNew<vtkPiecewiseFunction> from;
      vtkNew<vtkPiecewiseFunction> to;
      tomviz::deserialize(from.Get(), json["from"].toObject());
      tomviz::deserialize(to.Get(), json["to"].toObject());
      return new ScalarOpacityAnimation(sink, from, to);
    }
  } else if (type == "opacity") {
    if (OpacityAnimation::supports(node)) {
      return new OpacityAnimation(node, start, stop);
    }
  }

  return nullptr;
}

} // anonymous namespace

ModuleAnimations& ModuleAnimations::instance()
{
  static ModuleAnimations animations;
  return animations;
}

void ModuleAnimations::add(ModuleAnimation* animation)
{
  if (!animation) {
    return;
  }

  prune();
  m_animations.append(animation);
  emit changed();
}

void ModuleAnimations::removeForNode(pipeline::Node* node)
{
  bool removed = false;
  for (int i = m_animations.size() - 1; i >= 0; --i) {
    if (m_animations[i].isNull()) {
      m_animations.removeAt(i);
      removed = true;
    } else if (m_animations[i]->baseNode == node) {
      m_animations[i]->deleteLater();
      m_animations.removeAt(i);
      removed = true;
    }
  }

  if (removed) {
    emit changed();
  }
}

QList<ModuleAnimation*> ModuleAnimations::animations()
{
  prune();

  QList<ModuleAnimation*> alive;
  for (const auto& animation : m_animations) {
    alive.append(animation.data());
  }
  return alive;
}

bool ModuleAnimations::isEmpty()
{
  prune();
  return m_animations.isEmpty();
}

void ModuleAnimations::clear()
{
  if (m_animations.isEmpty()) {
    return;
  }

  for (const auto& animation : m_animations) {
    if (animation) {
      animation->deleteLater();
    }
  }
  m_animations.clear();
  emit changed();
}

void ModuleAnimations::prune()
{
  for (int i = m_animations.size() - 1; i >= 0; --i) {
    if (m_animations[i].isNull()) {
      m_animations.removeAt(i);
    }
  }
}

QJsonObject ModuleAnimations::serialize(pipeline::Pipeline* pipeline) const
{
  QJsonArray array;
  for (const auto& animation : m_animations) {
    if (!pipeline || !animation || !animation->baseNode) {
      continue;
    }

    auto type = animation->type();
    if (type.isEmpty()) {
      continue;
    }

    auto entry = animation->serialize();
    entry["type"] = type;
    entry["node"] = pipeline->nodeId(animation->baseNode);
    entry["segment"] = animation->segment;
    array.append(entry);
  }

  QJsonObject json;
  json["modules"] = array;
  return json;
}

void ModuleAnimations::deserialize(const QJsonObject& json,
                                   pipeline::Pipeline* pipeline)
{
  clear();

  if (!pipeline || !json["modules"].isArray()) {
    return;
  }

  for (const auto& value : json["modules"].toArray()) {
    auto entry = value.toObject();
    auto* node = pipeline->nodeById(entry["node"].toInt(-1));
    if (!node) {
      qWarning() << "Dropping a" << entry["type"].toString()
                 << "animation: its visualization is not in this state"
                 << "file.";
      continue;
    }

    if (auto* animation =
          buildAnimation(entry["type"].toString(), node, entry)) {
      animation->segment = entry["segment"].toInt(-1);
      m_animations.append(animation);
    }
  }

  emit changed();
}

} // namespace tomviz
