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
#include "detray/detectors/create_telescope_detector.hpp"
#include "detray/io/common/detector_reader.hpp"
#include "detray/io/common/detector_writer.hpp"

namespace traccc {

/// Combinatorial Kalman Finding Test with Sparse tracks
class KalmanFittingTelescopeTests : public KalmanFittingTests {

    public:
    /// Plane alignment direction (aligned to x-axis)
    static const inline detray::detail::ray<transform3> traj{
        {0, 0, 0}, 0, {1, 0, 0}, -1};

    /// Position of planes (in mm unit)
    static const inline std::vector<scalar> plane_positions = {
        20., 40., 60., 80., 100., 120., 140, 160, 180.};

    /// B field value and its type
    static constexpr vector3 B{2 * detray::unit<scalar>::T, 0, 0};

    /// Plane material and thickness
    static const inline detray::silicon_tml<scalar> mat = {};
    static constexpr scalar thickness = 0.5 * detray::unit<scalar>::mm;

    // Rectangle mask for the telescope geometry
    static constexpr detray::mask<detray::rectangle2D<>> rectangle{0u, 100000.f,
                                                                   100000.f};

    /// Measurement smearing parameters
    static constexpr std::array<scalar, 2u> smearing{
        50 * detray::unit<scalar>::um, 50 * detray::unit<scalar>::um};

    /// Standard deviations for seed track parameters
    static constexpr std::array<scalar, e_bound_size> stddevs = {
        0.03 * detray::unit<scalar>::mm,
        0.03 * detray::unit<scalar>::mm,
        0.017,
        0.017,
        0.001 / detray::unit<scalar>::GeV,
        1 * detray::unit<scalar>::ns};

    void consistency_tests(const track_state_collection_types::host&
                               track_states_per_track) const override {

        // The nubmer of track states is supposed be equal to the number
        // of planes
        ASSERT_EQ(track_states_per_track.size(), plane_positions.size());
    }

    protected:
    static void SetUpTestCase() {
        vecmem::host_memory_resource host_mr;

        detray::tel_det_config<> tel_cfg{rectangle};
        tel_cfg.positions(plane_positions);
        tel_cfg.module_material(mat);
        tel_cfg.mat_thickness(thickness);
        tel_cfg.pilot_track(traj);

        // Create telescope detector
        auto [det, name_map] = create_telescope_detector(host_mr, tel_cfg);

        // Write detector file
        auto writer_cfg = detray::io::detector_writer_config{}
                              .format(detray::io::format::json)
                              .replace_files(true);
        detray::io::write_detector(det, name_map, writer_cfg);
    }
};

}  // namespace traccc