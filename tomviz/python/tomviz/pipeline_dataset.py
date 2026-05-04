# -*- coding: utf-8 -*-

###############################################################################
# This source file is part of the Tomviz project, https://tomviz.org/.
# It is released under the 3-Clause BSD License, see "LICENSE".
###############################################################################

"""Standalone Dataset implementation for the new pipeline library.

This module provides a Dataset subclass that wraps a vtkImageData directly,
without requiring the TOMVIZ_APPLICATION environment variable or the old
internal_utils module. It uses vtk.numpy_interface.dataset_adapter for
VTK<->numpy conversion, the same approach as internal_dataset.py.
"""

import numpy as np
import vtk.numpy_interface.dataset_adapter as dsa
import vtk.util.numpy_support as np_s

from tomviz.dataset import Dataset as AbstractDataset


_DEFAULT_SCALARS_NAME = 'Scalars'


class PipelineDataset(AbstractDataset):
    """Dataset wrapping a vtkImageData for use in the new pipeline library."""

    def __init__(self, data_object):
        self._data_object = data_object
        self._tilt_axis = 2
        # Used by active_scalars setter on a still-empty vtkImageData
        # so the array doesn't default to "Scalars".
        self._inherited_active_name: str | None = None

    @property
    def active_scalars(self):
        do = dsa.WrapDataObject(self._data_object)
        rawarray = do.PointData.GetScalars()
        if rawarray is None:
            return None
        vtkarray = dsa.vtkDataArrayToVTKArray(rawarray, do)
        return np.reshape(vtkarray, self._data_object.GetDimensions(), order='F')

    @active_scalars.setter
    def active_scalars(self, v):
        if np.isfortran(v):
            arr = v.reshape(-1, order='F')
            vtkshape = v.shape
        else:
            vtkshape = v.shape
            tmp = np.asfortranarray(v)
            arr = tmp.reshape(-1, order='F')

        try:
            np_s.get_vtk_array_type(arr.dtype)
        except TypeError:
            arr = arr.astype(np.float32)

        do = dsa.WrapDataObject(self._data_object)
        oldscalars = do.PointData.GetScalars()
        if oldscalars is not None:
            arrayname = oldscalars.GetName()
        elif self._inherited_active_name:
            arrayname = self._inherited_active_name
        else:
            arrayname = _DEFAULT_SCALARS_NAME

        # Update extent if shape changed
        sameshape = list(vtkshape) == list(self._data_object.GetDimensions())
        if not sameshape:
            extent = [0, vtkshape[0] - 1, 0, vtkshape[1] - 1,
                      0, vtkshape[2] - 1]
            self._data_object.SetExtent(extent)

        do.PointData.append(arr, arrayname)
        do.PointData.SetActiveScalars(arrayname)

    @property
    def active_name(self):
        scalars = self._data_object.GetPointData().GetScalars()
        if scalars is None:
            return None
        return scalars.GetName()

    @active_name.setter
    def active_name(self, name):
        self._data_object.GetPointData().SetActiveScalars(name)

    @property
    def num_scalars(self):
        return len(self.scalars_names)

    @property
    def scalars_names(self):
        do = dsa.WrapDataObject(self._data_object)
        num_arrays = do.PointData.GetNumberOfArrays()
        return [do.PointData.GetArrayName(i) for i in range(num_arrays)]

    def scalars(self, name=None):
        do = dsa.WrapDataObject(self._data_object)
        if name is not None:
            rawarray = do.PointData.GetAbstractArray(name)
        else:
            rawarray = do.PointData.GetScalars()
        if rawarray is None:
            return None
        vtkarray = dsa.vtkDataArrayToVTKArray(rawarray, do)
        return np.reshape(vtkarray, self._data_object.GetDimensions(), order='F')

    def set_scalars(self, name, array):
        if np.isfortran(array):
            arr = array.reshape(-1, order='F')
            vtkshape = array.shape
        else:
            vtkshape = array.shape
            tmp = np.asfortranarray(array)
            arr = tmp.reshape(-1, order='F')

        try:
            np_s.get_vtk_array_type(arr.dtype)
        except TypeError:
            arr = arr.astype(np.float32)

        sameshape = list(vtkshape) == list(self._data_object.GetDimensions())
        if not sameshape:
            extent = [0, vtkshape[0] - 1, 0, vtkshape[1] - 1,
                      0, vtkshape[2] - 1]
            self._data_object.SetExtent(extent)

        do = dsa.WrapDataObject(self._data_object)
        do.PointData.append(arr, name)
        if do.PointData.GetNumberOfArrays() == 1:
            do.PointData.SetActiveScalars(name)

    def remove_scalars(self, name):
        pd = self._data_object.GetPointData()
        if pd.GetAbstractArray(name) is None:
            raise KeyError(f"No scalar array named '{name}'")
        pd.RemoveArray(name)
        if pd.GetScalars() is None and pd.GetNumberOfArrays() > 0:
            pd.SetActiveScalars(pd.GetArrayName(0))

    def rename_active(self, new_name: str):
        scalars = self._data_object.GetPointData().GetScalars()
        if scalars is not None:
            scalars.SetName(new_name)

    @property
    def spacing(self):
        return self._data_object.GetSpacing()

    @spacing.setter
    def spacing(self, v):
        self._data_object.SetSpacing(v[0], v[1], v[2])

    @property
    def tilt_angles(self):
        do = dsa.WrapDataObject(self._data_object)
        rawarray = do.FieldData.GetArray('tilt_angles')
        if isinstance(rawarray, dsa.VTKNoneArray):
            return None
        if rawarray is None:
            return None
        vtkarray = dsa.vtkDataArrayToVTKArray(rawarray, do)
        return vtkarray

    @tilt_angles.setter
    def tilt_angles(self, v):
        from vtkmodules.util.vtkConstants import VTK_DOUBLE
        vtkarray = np_s.numpy_to_vtk(v, deep=1, array_type=VTK_DOUBLE)
        vtkarray.SetName('tilt_angles')
        do = dsa.WrapDataObject(self._data_object)
        do.FieldData.RemoveArray('tilt_angles')
        do.FieldData.AddArray(vtkarray)

    @property
    def tilt_axis(self):
        return self._tilt_axis

    @tilt_axis.setter
    def tilt_axis(self, v):
        self._tilt_axis = v

    @property
    def scan_ids(self):
        do = dsa.WrapDataObject(self._data_object)
        rawarray = do.FieldData.GetArray('scan_ids')
        if isinstance(rawarray, dsa.VTKNoneArray):
            return None
        if rawarray is None:
            return None
        vtkarray = dsa.vtkDataArrayToVTKArray(rawarray, do)
        return vtkarray

    @scan_ids.setter
    def scan_ids(self, v):
        from vtkmodules.util.vtkConstants import VTK_INT
        if v is None:
            do = dsa.WrapDataObject(self._data_object)
            do.FieldData.RemoveArray('scan_ids')
            return
        vtkarray = np_s.numpy_to_vtk(v, deep=1, array_type=VTK_INT)
        vtkarray.SetName('scan_ids')
        do = dsa.WrapDataObject(self._data_object)
        do.FieldData.RemoveArray('scan_ids')
        do.FieldData.AddArray(vtkarray)

    @property
    def dark(self):
        return None

    @property
    def white(self):
        return None

    @property
    def file_name(self):
        return None

    @property
    def metadata(self):
        return {}

    def create_child_dataset(self):
        from vtk import vtkImageData
        new_child = vtkImageData()
        new_child.CopyStructure(self._data_object)
        input_spacing = self._data_object.GetSpacing()
        child_spacing = (input_spacing[0], input_spacing[1], input_spacing[0])
        new_child.SetSpacing(child_spacing)
        child = PipelineDataset(new_child)
        # Carry the parent's active-scalar name so the child's first
        # array doesn't default to "Scalars".
        parent_active = self._data_object.GetPointData().GetScalars()
        if parent_active is not None and parent_active.GetName():
            child._inherited_active_name = parent_active.GetName()
        return child
