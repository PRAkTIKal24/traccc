// This file is part of the Acts project.
//
// Copyright (C) 2022 CERN for the benefit of the Acts project
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#if defined(TRACCC_CONCEPTS_SUPPORTED)

#define TRACCC_REQUIRES(x) requires(x)
#define TRACCC_CONCEPT(x) x
#define TRACCC_STATIC_CHECK_CONCEPT(check_concept, check_type) \
  static_assert(check_concept<check_type>,                   \
                #check_type " does not fulfill " #check_concept)

#else

#define TRACCC_REQUIRES(x)
#define TRACCC_CONCEPT(x) typename
#define TRACCC_STATIC_CHECK_CONCEPT(concept, type) \
  static_assert(true, "Dummy assertion")

#endif
