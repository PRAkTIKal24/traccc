/** TRACCC library, part of the ACTS project (R&D line)
 *
 * (c) 2023 CERN for the benefit of the ACTS project
 *
 * Mozilla Public License Version 2.0
 */

#pragma once

// Project include(s).
#include "kalman_fitting_test.hpp"

// Detray include(s).
#include "detray/detectors/bfield.hpp"
#include "detray/detectors/create_wire_chamber.hpp"
#include "detray/io/common/detector_reader.hpp"
#include "detray/io/common/detector_writer.hpp"

namespace traccc {

/// Combinatorial Kalman Finding Test with Sparse tracks
class KalmanFittingWireChamberTests : public KalmanFittingTests {

    public:
    /// Number of layers
    static const inline unsigned int n_wire_layers{20u};

    /// Half z of cylinder
    static const inline scalar half_z{2000.f * detray::unit<scalar>::mm};

    /// B field value and its type
    static constexpr vector3 B{0, 0, 2 * detray::unit<scalar>::T};

    /// Step constraint
    static const inline scalar step_constraint = 2 * detray::unit<scalar>::mm;

    /// Overstep tolerance
    static const inline scalar overstep_tolerance =
        -100.f * detray::unit<scalar>::um;

    // Set mask tolerance to a large value not to miss the surface during KF
    static const inline scalar mask_tolerance = 50.f * detray::unit<scalar>::um;

    /// Measurement smearing parameters
    static constexpr std::array<scalar, 2u> smearing{
        50.f * detray::unit<scalar>::um, 50.f * detray::unit<scalar>::um};

    /// Standard deviations for seed track parameters
    static constexpr std::array<scalar, e_bound_size> stddevs = {
        0.01 * detray::unit<scalar>::mm,
        0.01 * detray::unit<scalar>::mm,
        0.001,
        0.001,
        0.01 / detray::unit<scalar>::GeV,
        0.01 * detray::unit<scalar>::ns};

    void consistency_tests(const track_state_collection_types::host&
                               track_states_per_track) const override {

        // The nubmer of track states is supposed be greater than or
        // equal to the number of layers
        ASSERT_GE(track_states_per_track.size(), n_wire_layers);
    }

    protected:
    static void SetUpTestCase() {
        vecmem::host_memory_resource host_mr;

        detray::wire_chamber_config wire_chamber_cfg;
        wire_chamber_cfg.n_layers(n_wire_layers);
        wire_chamber_cfg.half_z(half_z);

        // Create telescope detector
        auto [det, name_map] = create_wire_chamber(host_mr, wire_chamber_cfg);

        // Write detector file
        auto writer_cfg = detray::io::detector_writer_config{}
                              .format(detray::io::format::json)
                              .replace_files(true);
        detray::io::write_detector(det, name_map, writer_cfg);
    }
};

}  // namespace traccc