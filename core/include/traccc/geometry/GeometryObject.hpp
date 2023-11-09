// This file is part of the Acts project.
//
// Copyright (C) 2016-2020 CERN for the benefit of the Acts project
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "traccc/definitions/Algebra.hpp"
#include "traccc/geometry/GeometryContext.hpp"
#include "traccc/geometry/GeometryIdentifier.hpp"
#include "traccc/geometry/Polyhedron.hpp"
#include "traccc/utils/BinningType.hpp"
#include "traccc/utils/VectorHelpers.hpp"

namespace traccc {

/// Base class to provide GeometryIdentifier interface:
/// - simple set and get
///
/// It also provides the binningPosition method for
/// Geometry geometrical object to be binned in BinnedArrays
///
class GeometryObject {
 public:
  /// Defaulted constructor
  GeometryObject() = default;

  /// Defaulted copy constructor
  GeometryObject(const GeometryObject&) = default;

  /// Constructor from a value
  ///
  /// @param geometryId the geometry identifier of the object
  GeometryObject(const GeometryIdentifier& geometryId)
      : m_geometryId(geometryId) {}

  /// Assignment operator
  ///
  /// @param geometryId the source geometryId
  GeometryObject& operator=(const GeometryObject& geometryId) {
    if (&geometryId != this) {
      m_geometryId = geometryId.m_geometryId;
    }
    return *this;
  }

  /// @return the geometry id by reference
  const GeometryIdentifier& geometryId() const;

  /// Force a binning position method
  ///
  /// @param gctx The current geometry context object, e.g. alignment
  /// @param bValue is the value in which you want to bin
  ///
  /// @return vector 3D used for the binning schema
  virtual Vector3 binningPosition(const GeometryContext& gctx,
                                  BinningValue bValue) const = 0;

  /// Implement the binningValue
  ///
  /// @param gctx The current geometry context object, e.g. alignment
  /// @param bValue is the dobule in which you want to bin
  ///
  /// @return float to be used for the binning schema
  virtual double binningPositionValue(const GeometryContext& gctx,
                                      BinningValue bValue) const;

  /// Set the value
  ///
  /// @param geometryId the geometry identifier to be assigned
  void assignGeometryId(const GeometryIdentifier& geometryId);

 protected:
  GeometryIdentifier m_geometryId;
};

inline const GeometryIdentifier& GeometryObject::geometryId() const {
  return m_geometryId;
}

inline void GeometryObject::assignGeometryId(
    const GeometryIdentifier& geometryId) {
  m_geometryId = geometryId;
}

inline double GeometryObject::binningPositionValue(const GeometryContext& gctx,
                                                   BinningValue bValue) const {
  return VectorHelpers::cast(binningPosition(gctx, bValue), bValue);
}

}  // namespace
