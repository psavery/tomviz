/* This source file is part of the Tomviz project, https://tomviz.org/.
   It is released under the 3-Clause BSD License, see "LICENSE". */

#ifndef tomvizOpacityInterpolation_h
#define tomvizOpacityInterpolation_h

class vtkPiecewiseFunction;

namespace tomviz {

/// Blend two scalar opacity curves, writing the result into @a out.
///
/// @a u runs 0 (all @a from) to 1 (all @a to) and is clamped to that.
/// @a range is the scalar window both curves are read over. It matters:
/// curves are stored in data coordinates and get rescaled when the data
/// range moves, so two curves captured at different times can describe the
/// same shape over different windows. Reading both over one window is what
/// makes them comparable. VolumeData::colorMapRange() is the window to
/// pass.
///
/// Curves with the same number of control points are blended point by
/// point, which keeps the result small and editable and carries midpoint
/// and sharpness across exactly. Otherwise there is no correspondence
/// between the points, so both curves are sampled over @a range and the
/// samples are blended instead; that is correct but returns a densely
/// sampled curve rather than an editable one.
///
/// @a out may alias @a from or @a to.
void interpolateOpacity(vtkPiecewiseFunction* from, vtkPiecewiseFunction* to,
                        double u, const double range[2],
                        vtkPiecewiseFunction* out);

} // namespace tomviz

#endif
