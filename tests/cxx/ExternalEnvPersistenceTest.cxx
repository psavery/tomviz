/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include <gtest/gtest.h>

#include "EditNodeWidget.h"
#include "Pipeline.h"
#include "Utilities.h"
#include "transforms/LegacyPythonTransform.h"

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QSettings>

#include "TomvizTest.h"

using tomviz::readInJSONDescription;
using tomviz::pipeline::EditNodeWidget;
using tomviz::pipeline::LegacyPythonTransform;
using tomviz::pipeline::Pipeline;

namespace {

const char* SETTINGS_KEY = "externalEnvPaths/SAM2Segment3D";

void ensureApp()
{
  // Deterministic QSettings location for this test's key round-trip.
  QCoreApplication::setOrganizationName("tomviz-tests");
  QCoreApplication::setApplicationName("tomviz-tests");
  tomviz_test::ensureQApp();
}

EditNodeWidget* makeSam2Editor(Pipeline& pipeline)
{
  auto* transform = new LegacyPythonTransform();
  transform->setJSONDescription(readInJSONDescription("SAM2Segment3D"));
  transform->setScript("def transform(dataset):\n    pass\n");
  pipeline.addNode(transform);
  return transform->createPropertiesWidget(&pipeline, nullptr);
}

} // namespace

TEST(ExternalEnvPersistenceTest, SavesAppliedEnvPathAndPrefillsNextEditor)
{
  ensureApp();
  QSettings().remove(SETTINGS_KEY);

  Pipeline pipeline;

  // Fresh externalOnly node: External is forced, the env path row is
  // editable, and no path is remembered yet.
  auto* editor = makeSam2Editor(pipeline);
  auto* combo = editor->findChild<QComboBox*>("executorTypeCombo");
  auto* envEdit = editor->findChild<QLineEdit*>("executorEnvPathEdit");
  ASSERT_NE(combo, nullptr);
  ASSERT_NE(envEdit, nullptr);
  EXPECT_FALSE(combo->currentData().toString().isEmpty());
  EXPECT_TRUE(envEdit->isEnabled());
  EXPECT_TRUE(envEdit->text().isEmpty());

  // Applying with an environment remembers it for the operator type.
  envEdit->setText("/opt/envs/sam2-tomviz");
  editor->applyChangesToOperator();
  EXPECT_EQ(QSettings().value(SETTINGS_KEY).toString(),
            "/opt/envs/sam2-tomviz");
  delete editor;

  // A brand-new instance of the same operator starts prefilled.
  auto* editor2 = makeSam2Editor(pipeline);
  auto* envEdit2 = editor2->findChild<QLineEdit*>("executorEnvPathEdit");
  ASSERT_NE(envEdit2, nullptr);
  EXPECT_EQ(envEdit2->text(), "/opt/envs/sam2-tomviz");
  delete editor2;

  QSettings().remove(SETTINGS_KEY);
}

TEST(ExternalEnvPersistenceTest, NodeConfiguredPathWinsOverRemembered)
{
  ensureApp();
  QSettings().setValue(SETTINGS_KEY, "/opt/envs/remembered");

  Pipeline pipeline;
  auto* transform = new LegacyPythonTransform();
  transform->setJSONDescription(readInJSONDescription("SAM2Segment3D"));
  transform->setScript("def transform(dataset):\n    pass\n");
  pipeline.addNode(transform);

  // Configure the node's own executor, as deserialization would.
  auto* editor = transform->createPropertiesWidget(&pipeline, nullptr);
  auto* envEdit = editor->findChild<QLineEdit*>("executorEnvPathEdit");
  ASSERT_NE(envEdit, nullptr);
  envEdit->setText("/opt/envs/node-specific");
  editor->applyChangesToOperator();
  delete editor;

  // Reopening this node shows its own path, not the remembered one.
  auto* editor2 = transform->createPropertiesWidget(&pipeline, nullptr);
  auto* envEdit2 = editor2->findChild<QLineEdit*>("executorEnvPathEdit");
  ASSERT_NE(envEdit2, nullptr);
  EXPECT_EQ(envEdit2->text(), "/opt/envs/node-specific");
  delete editor2;

  QSettings().remove(SETTINGS_KEY);
}
