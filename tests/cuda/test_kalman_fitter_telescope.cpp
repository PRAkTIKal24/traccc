/** TRACCC library, part of the ACTS project (R&D line)
 *
 * (c) 2022-2023 CERN for the benefit of the ACTS project
 *
 * Mozilla Public License Version 2.0
 */

// Project include(s).
#include "traccc/cuda/fitting/fitting_algorithm.hpp"
#include "traccc/device/container_d2h_copy_alg.hpp"
#include "traccc/device/container_h2d_copy_alg.hpp"
#include "traccc/edm/track_state.hpp"
#include "traccc/fitting/fitting_algorithm.hpp"
#include "traccc/io/utils.hpp"
#include "traccc/performance/details/is_same_object.hpp"
#include "traccc/resolution/fitting_performance_writer.hpp"
#include "traccc/simulation/simulator.hpp"
#include "traccc/utils/memory_resource.hpp"
#include "traccc/utils/ranges.hpp"
#include "traccc/utils/seed_generator.hpp"

// Test include(s).
#include "tests/kalman_fitting_telescope_test.hpp"

// detray include(s).
#include "detray/detectors/create_telescope_detector.hpp"
#include "detray/io/common/detector_reader.hpp"
#include "detray/io/common/detector_writer.hpp"
#include "detray/propagator/propagator.hpp"
#include "detray/simulation/event_generator/track_generators.hpp"

// VecMem include(s).
#include <vecmem/memory/cuda/device_memory_resource.hpp>
#include <vecmem/memory/cuda/managed_memory_resource.hpp>
#include <vecmem/memory/host_memory_resource.hpp>
#include <vecmem/utils/cuda/async_copy.hpp>

// GTest include(s).
#include <gtest/gtest.h>

// System include(s).
#include <filesystem>
#include <string>

using namespace traccc;

// This defines the local frame test suite
TEST_P(KalmanFittingTelescopeTests, Run) {

    // Get the parameters
    const std::string name = std::get<0>(GetParam());
    const std::array<scalar, 3u> origin = std::get<1>(GetParam());
    const std::array<scalar, 3u> origin_stddev = std::get<2>(GetParam());
    const std::array<scalar, 2u> mom_range = std::get<3>(GetParam());
    const std::array<scalar, 2u> eta_range = std::get<4>(GetParam());
    const std::array<scalar, 2u> theta_range = eta_to_theta_range(eta_range);
    const std::array<scalar, 2u> phi_range = std::get<5>(GetParam());
    const unsigned int n_truth_tracks = std::get<6>(GetParam());
    const unsigned int n_events = std::get<7>(GetParam());

    // Performance writer
    traccc::fitting_performance_writer::config fit_writer_cfg;
    fit_writer_cfg.file_path = "performance_track_fitting_" + name + ".root";
    traccc::fitting_performance_writer fit_performance_writer(fit_writer_cfg);

    /*****************************
     * Build a telescope geometry
     *****************************/

    // Memory resources used by the application.
    vecmem::host_memory_resource host_mr;
    vecmem::cuda::device_memory_resource device_mr;
    traccc::memory_resource mr{device_mr, &host_mr};
    vecmem::cuda::managed_memory_resource mng_mr;

    // Read back detector file
    detray::io::detector_reader_config reader_cfg{};
    reader_cfg.add_file("telescope_detector_geometry.json")
        .add_file("telescope_detector_homogeneous_material.json")
        .add_file("telescope_detector_surface_grids.json");

    auto [host_det, names] =
        detray::io::read_detector<host_detector_type>(mng_mr, reader_cfg);

    // Detector view object
    auto det_view = detray::get_data(host_det);

    auto field = detray::bfield::create_const_field(B);

    /***************************
     * Generate simulation data
     ***************************/

    // Track generator
    using generator_type =
        detray::random_track_generator<traccc::free_track_parameters,
                                       uniform_gen_t>;
    generator_type::configuration gen_cfg{};
    gen_cfg.n_tracks(n_truth_tracks);
    gen_cfg.origin(origin);
    gen_cfg.origin_stddev(origin_stddev);
    gen_cfg.phi_range(phi_range[0], phi_range[1]);
    gen_cfg.theta_range(theta_range[0], theta_range[1]);
    gen_cfg.mom_range(mom_range[0], mom_range[1]);
    generator_type generator(gen_cfg);

    // Smearing value for measurements
    traccc::measurement_smearer<transform3> meas_smearer(smearing[0],
                                                         smearing[1]);

    using writer_type =
        traccc::smearing_writer<traccc::measurement_smearer<transform3>>;

    typename writer_type::config smearer_writer_cfg{meas_smearer};

    // Run simulator
    const std::string path = name + "/";
    const std::string full_path = io::data_directory() + path;
    std::filesystem::create_directories(full_path);
    auto sim = traccc::simulator<host_detector_type, b_field_t, generator_type,
                                 writer_type>(
        n_events, host_det, field, std::move(generator),
        std::move(smearer_writer_cfg), full_path);
    sim.run();

    /***************
     * Run fitting
     ***************/

    // Stream object
    traccc::cuda::stream stream;

    // Copy objects
    vecmem::cuda::async_copy copy{stream.cudaStream()};

    traccc::device::container_h2d_copy_alg<
        traccc::track_candidate_container_types>
        track_candidate_h2d{mr, copy};

    traccc::device::container_d2h_copy_alg<traccc::track_state_container_types>
        track_state_d2h{mr, copy};

    // Seed generator
    seed_generator<host_detector_type> sg(host_det, stddevs);

    // Fitting algorithm object
    typename traccc::cuda::fitting_algorithm<device_fitter_type>::config_type
        fit_cfg;
    traccc::cuda::fitting_algorithm<device_fitter_type> device_fitting(
        fit_cfg, mr, copy, stream);

    // Iterate over events
    for (std::size_t i_evt = 0; i_evt < n_events; i_evt++) {
        // Event map
        traccc::event_map2 evt_map(i_evt, path, path, path);

        // Truth Track Candidates
        traccc::track_candidate_container_types::host track_candidates =
            evt_map.generate_truth_candidates(sg, mng_mr);

        // Instantiate cuda containers/collections
        traccc::track_state_container_types::buffer track_states_cuda_buffer{
            {{}, *(mr.host)}, {{}, *(mr.host), mr.host}};

        // n_trakcs = 100
        ASSERT_EQ(track_candidates.size(), n_truth_tracks);

        // Navigation buffer
        auto navigation_buffer = detray::create_candidates_buffer(
            host_det, track_candidates.size(), mr.main, mr.host);

        // track candidates buffer
        const traccc::track_candidate_container_types::buffer
            track_candidates_cuda_buffer =
                track_candidate_h2d(traccc::get_data(track_candidates));

        // Run fitting
        track_states_cuda_buffer = device_fitting(
            det_view, field, navigation_buffer, track_candidates_cuda_buffer);

        traccc::track_state_container_types::host track_states_cuda =
            track_state_d2h(track_states_cuda_buffer);

        ASSERT_EQ(track_states_cuda.size(), n_truth_tracks);

        for (std::size_t i_trk = 0; i_trk < n_truth_tracks; i_trk++) {

            const auto& track_states_per_track = track_states_cuda[i_trk].items;
            const auto& fit_info = track_states_cuda[i_trk].header;

            consistency_tests(track_states_per_track);

            ndf_tests(fit_info, track_states_per_track);

            fit_performance_writer.write(track_states_per_track, fit_info,
                                         host_det, evt_map);
        }
    }

    fit_performance_writer.finalize();

    /********************
     * Pull value test
     ********************/

    static const std::vector<std::string> pull_names{
        "pull_d0", "pull_z0", "pull_phi", "pull_theta", "pull_qop"};
    pull_value_tests(fit_writer_cfg.file_path, pull_names);

    /********************
     * Success rate test
     ********************/

    scalar success_rate =
        static_cast<scalar>(n_success) / (n_truth_tracks * n_events);

    ASSERT_FLOAT_EQ(success_rate, 1.00f);

    // Remove the data
    std::filesystem::remove_all(full_path);
}

INSTANTIATE_TEST_SUITE_P(
    KalmanFitTelescopeValidation0, KalmanFittingTelescopeTests,
    ::testing::Values(std::make_tuple(
        "1_GeV_0_phi", std::array<scalar, 3u>{0.f, 0.f, 0.f},
        std::array<scalar, 3u>{0.f, 0.f, 0.f}, std::array<scalar, 2u>{1.f, 1.f},
        std::array<scalar, 2u>{0.f, 0.f}, std::array<scalar, 2u>{0.f, 0.f}, 100,
        100)));

INSTANTIATE_TEST_SUITE_P(
    KalmanFitTelescopeValidation1, KalmanFittingTelescopeTests,
    ::testing::Values(std::make_tuple(
        "10_GeV_0_phi", std::array<scalar, 3u>{0.f, 0.f, 0.f},
        std::array<scalar, 3u>{0.f, 0.f, 0.f},
        std::array<scalar, 2u>{10.f, 10.f}, std::array<scalar, 2u>{0.f, 0.f},
        std::array<scalar, 2u>{0.f, 0.f}, 100, 100)));

INSTANTIATE_TEST_SUITE_P(
    KalmanFitTelescopeValidation2, KalmanFittingTelescopeTests,
    ::testing::Values(std::make_tuple(
        "100_GeV_0_phi", std::array<scalar, 3u>{0.f, 0.f, 0.f},
        std::array<scalar, 3u>{0.f, 0.f, 0.f},
        std::array<scalar, 2u>{100.f, 100.f}, std::array<scalar, 2u>{0.f, 0.f},
        std::array<scalar, 2u>{0.f, 0.f}, 100, 100)));
