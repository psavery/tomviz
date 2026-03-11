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
  ExecutionFuture.cxx
  ThreadedExecutor.cxx
  data/VolumeData.cxx
  sources/SphereSource.cxx
  sources/DataReader.cxx
  sources/ReaderSourceNode.cxx
  transforms/ThresholdTransform.cxx
  transforms/LegacyPythonTransform.cxx
  sinks/VolumeStatsSink.cxx
  sinks/LegacyModuleSink.cxx
  sinks/VolumeSink.cxx
  sinks/SliceSink.cxx
  sinks/ContourSink.cxx
  sinks/ThresholdSink.cxx
  sinks/SegmentSink.cxx
  sinks/OutlineSink.cxx
  sinks/ClipSink.cxx
  sinks/RulerSink.cxx
  sinks/ScaleCubeSink.cxx
  sinks/PlotSink.cxx
  sinks/MoleculeSink.cxx
  ParameterInterfaceBuilder.cxx
  TransformPropertiesWidget.cxx
  PipelineStripWidget.cxx
  VolumeScalarsModel.cxx
  VolumePropertiesWidget.cxx
  vtkNonOrthoImagePlaneWidget.cxx)

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
  ExecutionFuture.h
  ThreadedExecutor.h
  data/VolumeData.h
  sources/SphereSource.h
  sources/DataReader.h
  sources/ReaderSourceNode.h
  transforms/ThresholdTransform.h
  transforms/LegacyPythonTransform.h
  pybind11/PybindVTKTypeCaster.h
  sinks/VolumeStatsSink.h
  sinks/LegacyModuleSink.h
  sinks/VolumeSink.h
  sinks/SliceSink.h
  sinks/ContourSink.h
  sinks/ThresholdSink.h
  sinks/SegmentSink.h
  sinks/OutlineSink.h
  sinks/ClipSink.h
  sinks/RulerSink.h
  sinks/ScaleCubeSink.h
  sinks/PlotSink.h
  sinks/MoleculeSink.h
  ParameterInterfaceBuilder.h
  TransformPropertiesWidget.h
  PipelineStripWidget.h
  VolumeScalarsModel.h
  VolumePropertiesWidget.h
  vtkNonOrthoImagePlaneWidget.h)

qt_add_resources(PIPELINE_RESOURCES pipeline_icons.qrc)

add_library(tomviz_pipeline SHARED ${SOURCES} ${HEADERS} ${PIPELINE_RESOURCES})
target_compile_definitions(tomviz_pipeline PRIVATE tomviz_pipeline_EXPORTS)
generate_export_header(tomviz_pipeline)
target_link_libraries(tomviz_pipeline
  PUBLIC Qt6::Core Qt6::Widgets VTK::CommonCore VTK::CommonDataModel
  PRIVATE Python3::Python pybind11::embed VTK::WrappingPythonCore
          VTK::IOImage VTK::IOXML VTK::IOLegacy
          VTK::RenderingCore VTK::RenderingVolume
          VTK::RenderingVolumeOpenGL2
          VTK::FiltersCore VTK::FiltersSources VTK::FiltersGeneral
          VTK::FiltersModeling
          VTK::DomainsChemistry
          VTK::InteractionWidgets VTK::ImagingCore
          VTK::RenderingGridAxes
          VTK::ChartsCore VTK::ViewsContext2D
          ParaView::RemotingViews)
target_include_directories(tomviz_pipeline
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)

install(TARGETS tomviz_pipeline
  RUNTIME DESTINATION "${INSTALL_RUNTIME_DIR}"
  LIBRARY DESTINATION "${INSTALL_LIBRARY_DIR}"
  ARCHIVE DESTINATION "${INSTALL_ARCHIVE_DIR}")
