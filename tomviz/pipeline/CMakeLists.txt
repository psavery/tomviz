include(GenerateExportHeader)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

set(SOURCES
  PortData.cxx
  Port.cxx
  InputPort.cxx
  OutputPort.cxx
  Link.cxx
  Node.cxx
  SourceNode.cxx
  TransformNode.cxx
  SinkNode.cxx
  Pipeline.cxx
  PipelineExecutor.cxx
  DefaultExecutor.cxx
  data/VolumeData.cxx
  sources/SphereSource.cxx
  sources/DataReader.cxx
  sources/ReaderSourceNode.cxx
  transforms/ThresholdTransform.cxx
  transforms/LegacyPythonTransform.cxx
  sinks/VolumeStatsSink.cxx
  PipelineStripWidget.cxx)

set(HEADERS
  PortType.h
  NodeState.h
  PortData.h
  Port.h
  InputPort.h
  OutputPort.h
  Link.h
  Node.h
  SourceNode.h
  TransformNode.h
  SinkNode.h
  Pipeline.h
  PipelineExecutor.h
  DefaultExecutor.h
  data/VolumeData.h
  sources/SphereSource.h
  sources/DataReader.h
  sources/ReaderSourceNode.h
  transforms/ThresholdTransform.h
  transforms/LegacyPythonTransform.h
  pybind11/PybindVTKTypeCaster.h
  sinks/VolumeStatsSink.h
  PipelineStripWidget.h)

add_library(tomviz_pipeline SHARED ${SOURCES} ${HEADERS})
target_compile_definitions(tomviz_pipeline PRIVATE tomviz_pipeline_EXPORTS)
generate_export_header(tomviz_pipeline)
target_link_libraries(tomviz_pipeline
  PUBLIC Qt6::Core Qt6::Widgets VTK::CommonCore VTK::CommonDataModel
  PRIVATE Python3::Python pybind11::embed VTK::WrappingPythonCore
          VTK::IOImage VTK::IOXML VTK::IOLegacy)
target_include_directories(tomviz_pipeline
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)

install(TARGETS tomviz_pipeline
  RUNTIME DESTINATION "${INSTALL_RUNTIME_DIR}"
  LIBRARY DESTINATION "${INSTALL_LIBRARY_DIR}"
  ARCHIVE DESTINATION "${INSTALL_ARCHIVE_DIR}")
