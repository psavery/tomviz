include(GenerateExportHeader)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

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
  DefaultExecutor.cxx)

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
  DefaultExecutor.h)

add_library(tomviz_pipeline SHARED ${SOURCES} ${HEADERS})
target_compile_definitions(tomviz_pipeline PRIVATE tomviz_pipeline_EXPORTS)
generate_export_header(tomviz_pipeline)
target_link_libraries(tomviz_pipeline PUBLIC Qt6::Core)
target_include_directories(tomviz_pipeline
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)

install(TARGETS tomviz_pipeline
  RUNTIME DESTINATION "${INSTALL_RUNTIME_DIR}"
  LIBRARY DESTINATION "${INSTALL_LIBRARY_DIR}"
  ARCHIVE DESTINATION "${INSTALL_ARCHIVE_DIR}")
