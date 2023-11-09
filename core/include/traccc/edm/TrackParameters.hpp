// This file is part of the Acts project.
//
// Copyright (C) 2016-2020 CERN for the benefit of the Acts project
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "traccc/edm/GenericBoundTrackParameters.hpp"
#include "traccc/edm/GenericCurvilinearTrackParameters.hpp"
#include "traccc/edm/GenericFreeTrackParameters.hpp"
#include "traccc/edm/ParticleHypothesis.hpp"

namespace traccc {

using SinglyChargedBoundTrackParameters =
    GenericBoundTrackParameters<SinglyChargedParticleHypothesis>;
using SinglyChargedCurvilinearTrackParameters =
    GenericCurvilinearTrackParameters<SinglyChargedParticleHypothesis>;
using SinglyChargedFreeTrackParameters =
    GenericFreeTrackParameters<SinglyChargedParticleHypothesis>;

using NeutralBoundTrackParameters =
    GenericBoundTrackParameters<NeutralParticleHypothesis>;
using NeutralCurvilinearTrackParameters =
    GenericCurvilinearTrackParameters<NeutralParticleHypothesis>;
using NeutralFreeTrackParameters =
    GenericFreeTrackParameters<NeutralParticleHypothesis>;

/// @brief BoundTrackParameters can hold any kind of charge
using BoundTrackParameters = GenericBoundTrackParameters<ParticleHypothesis>;
/// @brief CurvilinearTrackParameters can hold any kind of charge
using CurvilinearTrackParameters =
    GenericCurvilinearTrackParameters<ParticleHypothesis>;
/// @brief FreeTrackParameters can hold any kind of charge
using FreeTrackParameters = GenericFreeTrackParameters<ParticleHypothesis>;

}  // namespace
