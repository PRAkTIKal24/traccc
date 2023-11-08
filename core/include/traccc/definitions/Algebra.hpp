// This file is part of the Acts project.
//
// Copyright (C) 2016-2020 CERN for the benefit of the Acts project
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

// for GNU: ignore this specific warning, otherwise just include Eigen/Dense
#if defined(__GNUC__) && !defined(__clang__) && !defined(__INTEL_COMPILER)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#if __GNUC__ == 13
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include <Eigen/Core>
#include <Eigen/Geometry>
#pragma GCC diagnostic pop
#else
#include <Eigen/Core>
#include <Eigen/Geometry>
#endif

namespace traccc {

/// @defgroup acts-algebra-types Vector/matrix types with a common scalar type
///
/// These are the default vector/matrix types that should be used throughout the
/// codebase. They all use the common Acts scalar type but support variable size
/// either at compile- or runtime.
///
/// Eigen does not have a distinct type for symmetric matrices. A typedef for
/// fixed-size matrices is still defined to simplify definition (one template
/// size vs two template size for generic matrices) and to clarify semantic
/// meaning in interfaces. It also ensures that the matrix is square. However,
/// the user is responsible for ensuring that the values are symmetric.
///
/// Without a distinct type for symmetric matrices, there is no way to provide
/// any conditions e.g. square size, for the dynamic-sized case. Consequently,
/// no dynamic-sized symmetric matrix type is defined. Use the
/// `TracccDynamicMatrix` instead.
///
/// @{

/// Common scalar (floating point type used for the default algebra types.
///
/// Defaults to `double` but can be customized by the user.
#ifdef TRACCC_CUSTOM_SCALARTYPE
using TracccScalar = TRACCC_CUSTOM_SCALARTYPE;
#else
using TracccScalar = double;
#endif

template <unsigned int kSize>
using TracccVector = Eigen::Matrix<TracccScalar, kSize, 1>;

template <unsigned int kRows, unsigned int kCols>
using TracccMatrix = Eigen::Matrix<TracccScalar, kRows, kCols>;

template <unsigned int kSize>
using TracccSquareMatrix = Eigen::Matrix<TracccScalar, kSize, kSize>;

using TracccDynamicVector = Eigen::Matrix<TracccScalar, Eigen::Dynamic, 1>;

using TracccDynamicMatrix =
    Eigen::Matrix<TracccScalar, Eigen::Dynamic, Eigen::Dynamic>;

/// @}

/// @defgroup coordinates-types Fixed-size vector/matrix types for coordinates
///
/// These predefined types should always be used when handling coordinate
/// vectors in different coordinate systems, i.e. on surfaces (2d), spatial
/// position (3d), or space-time (4d).
///
/// @{

// coordinate vectors
using Vector2 = TracccVector<2>;
using Vector3 = TracccVector<3>;
using Vector4 = TracccVector<4>;

// square matrices e.g. for coordinate covariance matrices
using SquareMatrix2 = TracccSquareMatrix<2>;
using SquareMatrix3 = TracccSquareMatrix<3>;
using SquareMatrix4 = TracccSquareMatrix<4>;

// pure translation transformations
using Translation2 = Eigen::Translation<TracccScalar, 2>;
using Translation3 = Eigen::Translation<TracccScalar, 3>;

// linear (rotation) matrices
using RotationMatrix2 = TracccMatrix<2, 2>;
using RotationMatrix3 = TracccMatrix<3, 3>;

// pure rotation defined by a rotation angle around a rotation axis
using AngleAxis3 = Eigen::AngleAxis<TracccScalar>;

// combined affine transformations. types are chosen for better data alignment:
// - 2d affine compact stored as 2x3 matrix
// - 3d affine stored as 4x4 matrix
using Transform2 = Eigen::Transform<TracccScalar, 2, Eigen::AffineCompact>;
using Transform3 = Eigen::Transform<TracccScalar, 3, Eigen::Affine>;

/// @}

}  // namespace Acts
