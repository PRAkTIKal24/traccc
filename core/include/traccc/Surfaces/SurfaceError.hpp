// This file is part of the Acts project.
//
// Copyright (C) 2020 CERN for the benefit of the Acts project
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include <system_error>
#include <type_traits>

namespace traccc {

enum class SurfaceError {
  // ensure all values are non-zero
  GlobalPositionNotOnSurface = 1,
};

std::error_code make_error_code(traccc::SurfaceError e);

}  // namespace

namespace std {
// register with STL
template <>
struct is_error_code_enum<traccc::SurfaceError> : std::true_type {};
}  // namespace std
