include(PythonTests.cmake)

# Tests for legacy operators (subclasses of tomviz.operators.Operator that
# expect a LegacyDataset with create_child_dataset) are disabled while we
# phase the legacy infrastructure out: multi_array, tilt_axis_shift,
# constraint_dft, random_particles, deconvolution_denoise.
add_python_test(operator)
add_python_test(pystackreg)
add_python_test(xcorr)
add_python_test(shift_rotation_center)
add_python_test(remove_arrays)
add_python_test(tomopy_recon)
add_python_test(normalize)
add_python_test(psd_fsc)
add_python_test(pipeline_runtime)
add_python_test(pipeline_transforms)
add_python_test(pipeline_cli)
add_python_test(pipeline_writers)
add_python_test(pipeline_runner)
add_python_test(pipeline_v2_nodes)
add_python_test(io_formats)
