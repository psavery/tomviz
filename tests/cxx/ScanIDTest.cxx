/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#include <gtest/gtest.h>

#include <vtkImageData.h>
#include <vtkNew.h>
#include <vtkSmartPointer.h>

#include "data/VolumeData.h"

using tomviz::pipeline::VolumeData;

class ScanIDTest : public ::testing::Test
{
protected:
  void SetUp() override { image = vtkSmartPointer<vtkImageData>::New(); }

  vtkSmartPointer<vtkImageData> image;
};

TEST_F(ScanIDTest, scan_ids_not_present_by_default)
{
  ASSERT_FALSE(VolumeData::hasScanIds(image));
}

TEST_F(ScanIDTest, set_and_get_scan_ids)
{
  QVector<int> ids = { 1, 2, 3 };
  VolumeData::setScanIds(image, ids);

  ASSERT_TRUE(VolumeData::hasScanIds(image));

  auto retrieved = VolumeData::getScanIds(image);
  ASSERT_EQ(retrieved.size(), 3);
  ASSERT_EQ(retrieved[0], 1);
  ASSERT_EQ(retrieved[1], 2);
  ASSERT_EQ(retrieved[2], 3);
}

TEST_F(ScanIDTest, clear_scan_ids)
{
  QVector<int> ids = { 1, 2, 3 };
  VolumeData::setScanIds(image, ids);
  ASSERT_TRUE(VolumeData::hasScanIds(image));

  VolumeData::clearScanIds(image);
  ASSERT_FALSE(VolumeData::hasScanIds(image));
}

TEST_F(ScanIDTest, empty_scan_ids_are_treated_as_absent)
{
  // VolumeData treats an empty scan-id set as "no scan IDs" (hasScanIds is
  // true only when there is at least one tuple), so getScanIds is empty too.
  QVector<int> ids;
  VolumeData::setScanIds(image, ids);

  ASSERT_FALSE(VolumeData::hasScanIds(image));
  ASSERT_EQ(VolumeData::getScanIds(image).size(), 0);
}

TEST_F(ScanIDTest, large_scan_id_set)
{
  QVector<int> ids;
  for (int i = 0; i < 200; ++i) {
    ids.append(i * 10);
  }
  VolumeData::setScanIds(image, ids);

  ASSERT_TRUE(VolumeData::hasScanIds(image));
  auto retrieved = VolumeData::getScanIds(image);
  ASSERT_EQ(retrieved.size(), 200);
  for (int i = 0; i < 200; ++i) {
    ASSERT_EQ(retrieved[i], i * 10);
  }
}
